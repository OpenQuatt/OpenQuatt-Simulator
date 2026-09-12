#include "hcq_ot_thermostat_simulator.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::hcq_ot_thermostat_simulator {

static const char *const TAG = "hcq_ot_thermostat";

static uint16_t encode_f88(float value) {
  const long raw = std::clamp<long>(lroundf(value * 256.0f), -32768L, 32767L);
  return static_cast<uint16_t>(static_cast<int16_t>(raw));
}

void HCQOTThermostatSimulator::setup() {
  if (!enabled_) {
    return;
  }
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  start_();
}

void HCQOTThermostatSimulator::start_() {
  if (started_ || !enabled_ || ota_active_) {
    return;
  }
  opentherm_ = new OpenTherm(in_pin_, out_pin_, false);
  if (!opentherm_->begin(nullptr, response_callback_, this)) {
    ESP_LOGE(TAG, "OpenTherm thermostat master startup failed");
    delete opentherm_;
    opentherm_ = nullptr;
    return;
  }
  started_ = true;
  ESP_LOGI(TAG, "OpenTherm thermostat master started");
}

void HCQOTThermostatSimulator::loop() {
  if (!started_ || opentherm_ == nullptr) {
    return;
  }
  opentherm_->process();
  const uint32_t now = millis();
  if (opentherm_->isReady() && now - last_request_ms_ >= poll_interval_ms_)
    send_next_request_();
}

void HCQOTThermostatSimulator::send_next_request_() {
  uint16_t data = 0U;
  OpenThermMessageID id = OpenThermMessageID::Status;
  OpenThermMessageType type = OpenThermMessageType::READ_DATA;
  switch (next_request_) {
  case Request::STATUS:
    data = static_cast<uint16_t>((ch_enable_ ? (1U << 8U) : 0U) |
                                 (dhw_enable_ ? (1U << 9U) : 0U));
    break;
  case Request::T_SET:
    id = OpenThermMessageID::TSet;
    type = OpenThermMessageType::WRITE_DATA;
    data = encode_f88(t_set_c_);
    break;
  case Request::T_ROOM_SET:
    id = OpenThermMessageID::TrSet;
    type = OpenThermMessageType::WRITE_DATA;
    data = encode_f88(t_room_set_c_);
    break;
  case Request::T_ROOM:
    id = OpenThermMessageID::Tr;
    type = OpenThermMessageType::WRITE_DATA;
    data = encode_f88(t_room_c_);
    break;
  }
  const unsigned long frame = opentherm_->buildRequest(type, id, data);
  if (opentherm_->sendResponse(frame)) {
    request_count_++;
    last_request_ms_ = millis();
    next_request_ =
        static_cast<Request>((static_cast<uint8_t>(next_request_) + 1U) % 4U);
  }
}

void HCQOTThermostatSimulator::response_callback_(
    unsigned long frame, OpenThermResponseStatus status, void *context) {
  auto *self = static_cast<HCQOTThermostatSimulator *>(context);
  if (self != nullptr)
    self->process_response_(frame, status);
}

void HCQOTThermostatSimulator::process_response_(
    unsigned long frame, OpenThermResponseStatus status) {
  if (status == OpenThermResponseStatus::TIMEOUT) {
    timeout_count_++;
    return;
  }
  if (status != OpenThermResponseStatus::SUCCESS) {
    invalid_response_count_++;
    return;
  }
  response_count_++;
  last_response_ms_ = millis();
  const OpenThermMessageID id = opentherm_->getDataID(frame);
  last_response_id_ = static_cast<int>(id);
  if (id != OpenThermMessageID::Status)
    return;
  const uint16_t data = opentherm_->getUInt(frame);
  controller_fault_ = (data & (1U << 0U)) != 0U;
  controller_ch_active_ = (data & (1U << 1U)) != 0U;
  controller_dhw_active_ = (data & (1U << 2U)) != 0U;
  controller_flame_ = (data & (1U << 3U)) != 0U;
}

uint32_t HCQOTThermostatSimulator::last_response_age_ms() const {
  return last_response_ms_ == 0U ? UINT32_MAX : millis() - last_response_ms_;
}

uint32_t HCQOTThermostatSimulator::rx_queue_overflow_count() const {
  return opentherm_ == nullptr ? 0U : opentherm_->getRxQueueOverflowCount();
}

uint32_t HCQOTThermostatSimulator::tx_error_count() const {
  return opentherm_ == nullptr ? 0U : opentherm_->getTxErrorCount();
}

void HCQOTThermostatSimulator::reset_diagnostics() {
  request_count_ = 0U;
  response_count_ = 0U;
  timeout_count_ = 0U;
  invalid_response_count_ = 0U;
  last_response_id_ = -1;
  last_response_ms_ = 0U;
  if (opentherm_ != nullptr)
    opentherm_->resetDiagnostics();
}

void HCQOTThermostatSimulator::dump_config() {
  ESP_LOGCONFIG(TAG, "HCQ OpenTherm thermostat simulator:");
  ESP_LOGCONFIG(TAG, "  Master input: GPIO%u", in_pin_);
  ESP_LOGCONFIG(TAG, "  Master output: GPIO%u", out_pin_);
  ESP_LOGCONFIG(TAG, "  Poll interval: %lu ms",
                static_cast<unsigned long>(poll_interval_ms_));
}

void HCQOTThermostatSimulator::stop_() {
  if (opentherm_ != nullptr) {
    opentherm_->end();
    delete opentherm_;
    opentherm_ = nullptr;
  }
  started_ = false;
}

void HCQOTThermostatSimulator::on_shutdown() { stop_(); }

#ifdef USE_OTA_STATE_LISTENER
void HCQOTThermostatSimulator::on_ota_global_state(
    ota::OTAState state, float progress, uint8_t error,
    ota::OTAComponent *component) {
  if (state == ota::OTA_STARTED) {
    ota_active_ = true;
    stop_();
  } else if (state == ota::OTA_ABORT || state == ota::OTA_ERROR ||
             state == ota::OTA_COMPLETED) {
    ota_active_ = false;
    start_();
  }
}
#endif

} // namespace esphome::hcq_ot_thermostat_simulator
