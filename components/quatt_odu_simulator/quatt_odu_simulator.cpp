#include "quatt_odu_simulator.h"

#include <algorithm>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart_component_esp_idf.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::quatt_odu_simulator {

static const char* const TAG = "quatt_odu_simulator";

void QuattOduModbusHub::setup() {
  modbus::Modbus::setup();
  this->rx_buffer_.reserve(modbus::MAX_FRAME_SIZE);
  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);
}

void QuattOduModbusHub::loop() {
  this->pump_tx_();
  if (this->tx_active_) return;
  modbus::Modbus::loop();
  if (!this->pending_.active || static_cast<int32_t>(millis() - this->pending_.due_ms) < 0) return;
  if (this->tx_active_ || this->available() || !this->rx_buffer_.empty() || this->tx_delay_remaining() > 0) return;
  const uint8_t address = this->pending_.address;
  const uint16_t pdu_size = this->pending_.pdu_size;
  std::array<uint8_t, modbus::MAX_PDU_SIZE> pdu = this->pending_.pdu;
  this->pending_.active = false;
  this->process_request_(address, std::span<const uint8_t>(pdu.data(), pdu_size), true);
}

void QuattOduModbusHub::on_shutdown() {
  this->pending_.active = false;
  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);
  this->tx_active_ = false;
  this->tx_frame_size_ = 0U;
  this->tx_frame_offset_ = 0U;
  this->tx_odu_index_ = 0xFFU;
}

void QuattOduModbusHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Quatt ODU Modbus RTU server:");
  ESP_LOGCONFIG(TAG, "  Response delay: %u ms", this->response_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Drop every Nth response: %u", this->drop_every_nth_);
  LOG_PIN("  Flow Control Pin: ", this->flow_control_pin_);
}

void QuattOduModbusHub::parse_modbus_frames() {
  while (!this->rx_buffer_.empty()) {
    const size_t available = this->rx_buffer_.size();
    uint16_t frame_length = modbus::helpers::client_frame_length(this->rx_buffer_.data(), available);
    if (available < frame_length) break;

    const uint8_t function_code = available > 1U ? this->rx_buffer_[1] : 0U;
    if (modbus::helpers::is_function_code_unknown_length(function_code)) {
      frame_length = this->find_frame_end_by_crc_(frame_length);
      if (frame_length == 0U) break;
    } else if (crc16(this->rx_buffer_.data(), frame_length) != 0U) {
      this->clear_rx_buffer_(LOG_STR("CRC failed"), true);
      continue;
    }

    const uint8_t address = this->rx_buffer_[0];
    const uint16_t pdu_size = frame_length - 3U;
    std::array<uint8_t, modbus::MAX_PDU_SIZE> pdu{};
    std::memcpy(pdu.data(), this->rx_buffer_.data() + 1U, pdu_size);
    this->clear_rx_buffer_(LOG_STR("parse succeeded"), false, frame_length);

    if (!modbus::helpers::is_client_pdu_standard(pdu.data(), pdu_size)) {
      if (address != modbus::BROADCAST_ADDRESS)
        this->send_exception_(address, function_code, modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
      continue;
    }
    this->queue_request_(address, std::span<const uint8_t>(pdu.data(), pdu_size));
  }
  if (this->timeout_()) this->clear_rx_buffer_(LOG_STR("timeout after partial request"), true);
}

void QuattOduModbusHub::queue_request_(uint8_t address, std::span<const uint8_t> pdu) {
  if (address == modbus::BROADCAST_ADDRESS) {
    this->process_request_(address, pdu, false);
    return;
  }
  if (this->pending_.active) {
    const int8_t index =
        this->parent_simulator_ == nullptr ? -1 : this->parent_simulator_->find_odu_by_address(address);
    if (index >= 0) this->record_drop_(static_cast<uint8_t>(index));
    return;
  }
  this->pending_.active = true;
  this->pending_.address = address;
  this->pending_.pdu_size = static_cast<uint16_t>(pdu.size());
  std::copy(pdu.begin(), pdu.end(), this->pending_.pdu.begin());
  this->pending_.due_ms = millis() + this->response_delay_ms_;
}

void QuattOduModbusHub::process_request_(uint8_t address, std::span<const uint8_t> pdu, bool respond) {
  if (this->parent_simulator_ == nullptr || pdu.empty()) return;
  const uint8_t function_code = pdu[0];
  const uint16_t start_address = pdu.size() >= 3U ? modbus::helpers::get_data<uint16_t>(pdu.data(), 1U) : 0U;
  uint16_t count = 1U;
  if ((function_code == 0x03U || function_code == 0x04U || function_code == 0x10U) && pdu.size() >= 5U)
    count = modbus::helpers::get_data<uint16_t>(pdu.data(), 3U);

  if (!respond) {
    for (uint8_t index = 0U; index < 2U; index++) {
      auto& model = this->parent_simulator_->model(index);
      if (!model.enabled()) continue;
      model.begin_request(function_code, start_address, count, millis());
      if (function_code == 0x06U)
        this->process_write_single_(address, pdu, start_address, index, false);
      else if (function_code == 0x10U)
        this->process_write_multiple_(address, pdu, start_address, count, index, false);
    }
    return;
  }

  const int8_t found = this->parent_simulator_->find_odu_by_address(address);
  if (found < 0) return;
  const uint8_t index = static_cast<uint8_t>(found);
  auto& model = this->parent_simulator_->model(index);
  model.begin_request(function_code, start_address, count, millis());

  this->request_sequence_++;
  if (!this->parent_simulator_->should_respond(index) ||
      (this->drop_every_nth_ > 0U && this->request_sequence_ % this->drop_every_nth_ == 0U) ||
      (this->timeout_enabled_ && start_address == this->timeout_start_address_)) {
    this->record_drop_(index);
    return;
  }
  if (this->reboot_enabled_ && start_address == this->reboot_start_address_) {
    this->reboot_enabled_ = false;
    this->record_drop_(index);
    App.safe_reboot();
    return;
  }
  if (this->exception_enabled_ && start_address == this->exception_start_address_) {
    model.mutable_state().protocol.exception_count++;
    this->send_exception_(address, function_code, static_cast<modbus::ExceptionCode>(this->exception_code_));
    return;
  }

  switch (function_code) {
    case 0x03:
    case 0x04:
      this->process_read_(address, function_code, start_address, count, index);
      return;
    case 0x06:
      this->process_write_single_(address, pdu, start_address, index, true);
      return;
    case 0x10:
      this->process_write_multiple_(address, pdu, start_address, count, index, true);
      return;
    default:
      model.mutable_state().protocol.exception_count++;
      this->send_exception_(address, function_code, modbus::ExceptionCode::ILLEGAL_FUNCTION);
  }
}

void QuattOduModbusHub::process_read_(uint8_t address, uint8_t function_code, uint16_t start_address, uint16_t count,
                                      uint8_t odu_index) {
  auto& model = this->parent_simulator_->model(odu_index);
  auto& diagnostics = model.mutable_state().protocol;
  diagnostics.read_count++;
  if (start_address <= 3069U && static_cast<uint32_t>(start_address) + count > 3050U &&
      model.state().profile != Profile::V2_NEW)
    diagnostics.legacy_extension_read_violations++;

  if (count == 0U || count > modbus::MAX_NUM_OF_REGISTERS_TO_READ ||
      !modbus::helpers::address_range_fits(start_address, count)) {
    diagnostics.exception_count++;
    this->send_exception_(address, function_code, modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    return;
  }
  for (uint32_t register_address = start_address; register_address < static_cast<uint32_t>(start_address) + count;
       register_address++) {
    if (!model.can_read_register(static_cast<uint16_t>(register_address))) {
      diagnostics.invalid_address_count++;
      diagnostics.exception_count++;
      this->send_exception_(address, function_code, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
      return;
    }
  }

  std::array<uint8_t, modbus::MAX_PDU_SIZE> response{};
  response[0] = function_code;
  response[1] = static_cast<uint8_t>(count * 2U);
  uint16_t response_size = 2U;
  for (uint32_t register_address = start_address; register_address < static_cast<uint32_t>(start_address) + count;
       register_address++) {
    uint16_t value = 0U;
    if (!model.read_register(static_cast<uint16_t>(register_address), value)) {
      diagnostics.exception_count++;
      this->send_exception_(address, function_code, modbus::ExceptionCode::SERVICE_DEVICE_FAILURE);
      return;
    }
    response[response_size++] = static_cast<uint8_t>(value >> 8U);
    response[response_size++] = static_cast<uint8_t>(value & 0xFFU);
  }

  if (start_address == this->malformed_start_address_) {
    if (this->malformed_mode_ == MalformedResponseMode::WRONG_BYTE_COUNT)
      response[1] = static_cast<uint8_t>(response[1] + 2U);
    else if (this->malformed_mode_ == MalformedResponseMode::INCOMPLETE_RESPONSE && response_size > 2U)
      response_size = static_cast<uint16_t>(response_size - std::min<uint16_t>(2U, response_size - 2U));
  }
  this->send_pdu_(address, response.data(), response_size);
}

void QuattOduModbusHub::process_write_single_(uint8_t address, std::span<const uint8_t> pdu, uint16_t start_address,
                                              uint8_t odu_index, bool respond) {
  auto& model = this->parent_simulator_->model(odu_index);
  const uint16_t value = modbus::helpers::get_data<uint16_t>(pdu.data(), 3U);
  if (!model.can_write_register(start_address, value)) {
    model.record_rejected_write(start_address, value, millis());
    if (respond) this->send_exception_(address, pdu[0], modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    return;
  }
  model.write_register(start_address, value, millis());
  if (respond) this->send_pdu_(address, pdu.data(), static_cast<uint16_t>(pdu.size()));
}

void QuattOduModbusHub::process_write_multiple_(uint8_t address, std::span<const uint8_t> pdu, uint16_t start_address,
                                                uint16_t count, uint8_t odu_index, bool respond) {
  auto& model = this->parent_simulator_->model(odu_index);
  if (count == 0U || count > modbus::MAX_NUM_OF_REGISTERS_TO_WRITE || pdu.size() != 6U + count * 2U ||
      pdu[5] != count * 2U || !modbus::helpers::address_range_fits(start_address, count)) {
    model.record_rejected_write(start_address);
    if (respond) this->send_exception_(address, pdu[0], modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    return;
  }
  for (uint16_t offset = 0U; offset < count; offset++) {
    const uint16_t value = modbus::helpers::get_data<uint16_t>(pdu.data(), 6U + offset * 2U);
    if (!model.can_write_register(static_cast<uint16_t>(start_address + offset), value)) {
      model.record_rejected_write(static_cast<uint16_t>(start_address + offset), value, millis());
      if (respond) this->send_exception_(address, pdu[0], modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
      return;
    }
  }
  for (uint16_t offset = 0U; offset < count; offset++) {
    const uint16_t value = modbus::helpers::get_data<uint16_t>(pdu.data(), 6U + offset * 2U);
    model.write_register(static_cast<uint16_t>(start_address + offset), value, millis());
  }
  if (respond) {
    uint8_t response[5] = {pdu[0], static_cast<uint8_t>(start_address >> 8U),
                           static_cast<uint8_t>(start_address & 0xFFU), static_cast<uint8_t>(count >> 8U),
                           static_cast<uint8_t>(count & 0xFFU)};
    this->send_pdu_(address, response, sizeof(response));
  }
}

void QuattOduModbusHub::send_exception_(uint8_t address, uint8_t function_code, modbus::ExceptionCode exception) {
  const uint8_t response[2] = {static_cast<uint8_t>(function_code | modbus::FUNCTION_CODE_EXCEPTION_MASK),
                               static_cast<uint8_t>(exception)};
  this->send_pdu_(address, response, sizeof(response));
}

bool QuattOduModbusHub::send_pdu_(uint8_t address, const uint8_t* pdu, uint16_t pdu_size) {
  if (pdu_size == 0U || pdu_size > modbus::MAX_PDU_SIZE || this->tx_active_ || this->available() ||
      !this->rx_buffer_.empty() || this->tx_delay_remaining() > 0)
    return false;

  this->tx_frame_[0] = address;
  std::memcpy(this->tx_frame_.data() + 1U, pdu, pdu_size);
  const uint16_t crc = crc16(this->tx_frame_.data(), pdu_size + 1U);
  this->tx_frame_[pdu_size + 1U] = static_cast<uint8_t>(crc & 0xFFU);
  this->tx_frame_[pdu_size + 2U] = static_cast<uint8_t>(crc >> 8U);
  const uint16_t frame_size = pdu_size + 3U;

  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(true);
  const uint32_t transmit_us =
      (static_cast<uint32_t>(frame_size) * 11U * 1000000U + this->parent_->get_baud_rate() - 1U) /
      this->parent_->get_baud_rate();
  this->tx_frame_size_ = frame_size;
  this->tx_frame_offset_ = 0U;
  this->tx_active_ = true;
  this->last_send_ = millis();
  this->last_send_tx_offset_ = (transmit_us + 999U) / 1000U;
  this->tx_deadline_ms_ = this->last_send_ + this->last_send_tx_offset_ + 50U;
  const int8_t odu_index =
      this->parent_simulator_ == nullptr ? -1 : this->parent_simulator_->find_odu_by_address(address);
  this->tx_odu_index_ = odu_index < 0 ? 0xFFU : static_cast<uint8_t>(odu_index);
  this->pump_tx_();
  return true;
}

void QuattOduModbusHub::pump_tx_() {
  if (!this->tx_active_) return;
  const auto abort_tx = [this](const char* reason) {
    ESP_LOGE(TAG, "%s", reason);
    if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);
    if (this->tx_odu_index_ < 2U) this->record_drop_(this->tx_odu_index_);
    this->tx_active_ = false;
    this->tx_odu_index_ = 0xFFU;
  };
  auto* idf_uart = static_cast<uart::IDFUARTComponent*>(this->parent_);
  const auto uart_num = static_cast<uart_port_t>(idf_uart->get_hw_serial_number());
  if (this->tx_frame_offset_ < this->tx_frame_size_) {
    const int written =
        uart_tx_chars(uart_num, reinterpret_cast<const char*>(this->tx_frame_.data()) + this->tx_frame_offset_,
                      this->tx_frame_size_ - this->tx_frame_offset_);
    if (written < 0) {
      abort_tx("Failed to queue Modbus response bytes");
      return;
    }
    this->tx_frame_offset_ += static_cast<uint16_t>(written);
  }
  if (this->tx_frame_offset_ < this->tx_frame_size_) {
    if (static_cast<int32_t>(millis() - this->tx_deadline_ms_) >= 0)
      abort_tx("Timed out while queueing Modbus response");
    return;
  }

  const esp_err_t status = uart_wait_tx_done(uart_num, 0);
  if (status == ESP_OK) {
    if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);
    this->tx_active_ = false;
    this->tx_odu_index_ = 0xFFU;
  } else if (status != ESP_ERR_TIMEOUT) {
    abort_tx("Failed while waiting for Modbus UART TX completion");
  } else if (static_cast<int32_t>(millis() - this->tx_deadline_ms_) >= 0) {
    abort_tx("Timed out while transmitting Modbus response");
  }
}

void QuattOduModbusHub::record_drop_(uint8_t odu_index) {
  this->parent_simulator_->model(odu_index).mutable_state().protocol.dropped_response_count++;
}

void QuattOduSimulator::setup() {
  for (auto& model : this->models_) model.configure(Profile::DISABLED);
  this->last_update_ms_ = millis();
}

void QuattOduSimulator::update() {
  const uint32_t now = millis();
  const float dt_s =
      this->last_update_ms_ == 0U ? this->get_update_interval() / 1000.0f : (now - this->last_update_ms_) / 1000.0f;
  this->last_update_ms_ = now;
  if (this->simulation_enabled_) {
    for (auto& model : this->models_) model.update(dt_s);
  }
}

void QuattOduSimulator::dump_config() {
  ESP_LOGCONFIG(TAG, "Quatt dual ODU simulator:");
  ESP_LOGCONFIG(TAG, "  Simulation enabled: %s", YESNO(this->simulation_enabled_));
  ESP_LOGCONFIG(TAG, "  Responses enabled: %s", YESNO(this->responses_enabled_));
  for (size_t index = 0; index < this->models_.size(); index++) {
    ESP_LOGCONFIG(TAG, "  ODU %u: address %u, profile %s", static_cast<unsigned>(index + 1U), this->addresses_[index],
                  profile_definition(this->models_[index].state().profile).label);
  }
}

uint32_t QuattOduSimulator::free_internal_heap() const {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

uint32_t QuattOduSimulator::minimum_internal_heap() const {
  return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

uint32_t QuattOduSimulator::largest_internal_heap_block() const {
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

uint32_t QuattOduSimulator::loop_stack_high_watermark_bytes() const {
  return uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
}

}  // namespace esphome::quatt_odu_simulator
