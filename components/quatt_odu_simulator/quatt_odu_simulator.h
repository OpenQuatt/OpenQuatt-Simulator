#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "esphome/components/modbus/modbus.h"
#include "esphome/core/component.h"
#include "quatt_odu_simulator_model.h"

namespace esphome::quatt_odu_simulator {

enum class MalformedResponseMode : uint8_t { NONE = 0, WRONG_BYTE_COUNT = 1, INCOMPLETE_RESPONSE = 2 };

class QuattOduSimulator;

class QuattOduModbusHub final : public modbus::Modbus {
 public:
  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;

  void set_parent(QuattOduSimulator* parent) { this->parent_simulator_ = parent; }
  void set_response_delay_ms(uint16_t value) { this->response_delay_ms_ = value; }
  void set_drop_every_nth(uint16_t value) { this->drop_every_nth_ = value; }
  void set_timeout_injection(bool enabled, uint16_t start_address) {
    this->timeout_enabled_ = enabled;
    this->timeout_start_address_ = start_address;
  }
  void set_exception_injection(bool enabled, uint16_t start_address, uint8_t exception_code) {
    this->exception_enabled_ = enabled;
    this->exception_start_address_ = start_address;
    this->exception_code_ = exception_code;
  }
  void set_malformed_injection(MalformedResponseMode mode, uint16_t start_address) {
    this->malformed_mode_ = mode;
    this->malformed_start_address_ = start_address;
  }
  void set_reboot_injection(bool enabled, uint16_t start_address) {
    this->reboot_enabled_ = enabled;
    this->reboot_start_address_ = start_address;
  }
  void reset_diagnostics() { this->request_sequence_ = 0U; }

 protected:
  struct PendingRequest {
    bool active{false};
    uint8_t address{0};
    uint16_t pdu_size{0};
    uint32_t due_ms{0};
    std::array<uint8_t, modbus::MAX_PDU_SIZE> pdu{};
  };

  void parse_modbus_frames() override;
  void process_modbus_server_frame(uint8_t, std::span<const uint8_t>) override {}
  void queue_request_(uint8_t address, std::span<const uint8_t> pdu);
  void process_request_(uint8_t address, std::span<const uint8_t> pdu, bool respond);
  void process_read_(uint8_t address, uint8_t function_code, uint16_t start_address, uint16_t count, uint8_t odu_index);
  void process_write_single_(uint8_t address, std::span<const uint8_t> pdu, uint16_t start_address, uint8_t odu_index,
                             bool respond);
  void process_write_multiple_(uint8_t address, std::span<const uint8_t> pdu, uint16_t start_address, uint16_t count,
                               uint8_t odu_index, bool respond);
  void send_exception_(uint8_t address, uint8_t function_code, modbus::ExceptionCode exception);
  bool send_pdu_(uint8_t address, const uint8_t* pdu, uint16_t pdu_size);
  void pump_tx_();
  void record_drop_(uint8_t odu_index);

  QuattOduSimulator* parent_simulator_{nullptr};
  PendingRequest pending_{};
  uint32_t request_sequence_{0};
  uint16_t response_delay_ms_{0};
  uint16_t drop_every_nth_{0};
  bool timeout_enabled_{false};
  uint16_t timeout_start_address_{0};
  bool exception_enabled_{false};
  uint16_t exception_start_address_{0};
  uint8_t exception_code_{static_cast<uint8_t>(modbus::ExceptionCode::SERVICE_DEVICE_FAILURE)};
  MalformedResponseMode malformed_mode_{MalformedResponseMode::NONE};
  uint16_t malformed_start_address_{0};
  bool reboot_enabled_{false};
  uint16_t reboot_start_address_{0};
  bool tx_active_{false};
  std::array<uint8_t, modbus::MAX_FRAME_SIZE> tx_frame_{};
  uint16_t tx_frame_size_{0};
  uint16_t tx_frame_offset_{0};
  uint32_t tx_deadline_ms_{0};
  uint8_t tx_odu_index_{0xFFU};
};

class QuattOduSimulator final : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void set_hub(QuattOduModbusHub* hub) { this->hub_ = hub; }
  QuattOduSimulatorModel& model(uint8_t index) { return this->models_[index < 2U ? index : 0U]; }
  const QuattOduSimulatorModel& model(uint8_t index) const { return this->models_[index < 2U ? index : 0U]; }

  void set_profile(uint8_t index, uint8_t profile) {
    if (index >= this->models_.size()) return;
    if (profile > static_cast<uint8_t>(Profile::V2_NEW)) profile = static_cast<uint8_t>(Profile::DISABLED);
    this->models_[index].configure(static_cast<Profile>(profile));
  }
  uint8_t get_profile(uint8_t index) const { return static_cast<uint8_t>(this->model(index).state().profile); }

  void set_address(uint8_t index, uint8_t address) {
    if (index >= this->addresses_.size() || address == 0U || address > 247U) return;
    const uint8_t other = index == 0U ? 1U : 0U;
    if (this->addresses_[other] == address) return;
    this->addresses_[index] = address;
  }
  uint8_t get_address(uint8_t index) const { return this->addresses_[index < 2U ? index : 0U]; }
  int8_t find_odu_by_address(uint8_t address) const {
    for (uint8_t index = 0; index < this->addresses_.size(); index++)
      if (this->addresses_[index] == address) return static_cast<int8_t>(index);
    return -1;
  }

  void set_simulation_enabled(bool enabled) { this->simulation_enabled_ = enabled; }
  void set_responses_enabled(bool enabled) { this->responses_enabled_ = enabled; }
  void set_odu_responses_enabled(uint8_t index, bool enabled) {
    if (index < this->odu_responses_enabled_.size()) this->odu_responses_enabled_[index] = enabled;
  }
  bool get_simulation_enabled() const { return this->simulation_enabled_; }
  bool get_responses_enabled() const { return this->responses_enabled_; }
  bool should_respond(uint8_t index) const {
    return index < this->models_.size() && this->simulation_enabled_ && this->responses_enabled_ &&
           this->odu_responses_enabled_[index] && this->models_[index].enabled();
  }

  void set_outside_temperature(uint8_t index, float value) {
    this->model(index).mutable_state().outside_temperature_c = value;
  }
  void set_water_in_temperature(uint8_t index, float value) {
    this->model(index).mutable_state().water_in_temperature_c = value;
  }
  void set_defrost(uint8_t index, bool value) { this->model(index).set_defrost(value); }
  void set_fault_word(uint8_t index, uint8_t word, uint16_t value) {
    if (word < 3U) this->model(index).mutable_state().fault_words[word] = value;
  }
  void set_table_write_enabled(uint8_t index, bool value) {
    this->model(index).mutable_state().table_write_enabled = value;
  }
  void set_freeze_frequency(uint8_t index, bool value) {
    this->model(index).mutable_settings().freeze_measured_frequency = value;
  }
  void set_force_no_flow(uint8_t index, bool value) { this->model(index).mutable_state().force_no_flow = value; }
  void set_pump_feedback_override(uint8_t index, uint16_t value) {
    this->model(index).mutable_state().pump_feedback_override = value;
  }
  void set_hold_level_during_defrost(uint8_t index, bool value) {
    this->model(index).mutable_settings().hold_level_during_defrost = value;
  }
  void restore_factory_tables(uint8_t index) { this->model(index).restore_factory_tables(); }
  bool apply_runtime_modified_preset(uint8_t index) { return this->model(index).apply_runtime_modified_preset(); }
  void reset_diagnostics() {
    for (auto& model : this->models_) model.reset_diagnostics();
  }
  uint32_t free_internal_heap() const;
  uint32_t minimum_internal_heap() const;
  uint32_t largest_internal_heap_block() const;
  uint32_t loop_stack_high_watermark_bytes() const;

 protected:
  std::array<QuattOduSimulatorModel, 2> models_{};
  std::array<uint8_t, 2> addresses_{{1U, 2U}};
  std::array<bool, 2> odu_responses_enabled_{{true, true}};
  QuattOduModbusHub* hub_{nullptr};
  bool simulation_enabled_{true};
  bool responses_enabled_{true};
  uint32_t last_update_ms_{0};
};

}  // namespace esphome::quatt_odu_simulator
