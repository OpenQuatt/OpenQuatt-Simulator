#include "hcq_ot_boiler_simulator.h"

#include <algorithm>
#include <cmath>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esphome/core/log.h"

namespace esphome::hcq_ot_boiler_simulator {
namespace {

static const char *const TAG = "hcq.ot_boiler_simulator";
static constexpr unsigned long MASTER_STATUS_TIMEOUT_MS = 10000;
static constexpr unsigned long STARTUP_SETTLE_MS = 1000;
static constexpr unsigned long BUS_IDLE_MIN_MS = 100;
static constexpr unsigned long STARTUP_FORCE_MS = 5000;

unsigned long now_millis() {
  return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}

uint16_t set_bit(uint16_t data, uint8_t bit, bool value) {
  const uint16_t mask = static_cast<uint16_t>(1U << bit);
  return value ? static_cast<uint16_t>(data | mask)
               : static_cast<uint16_t>(data & static_cast<uint16_t>(~mask));
}

float decode_f88(uint16_t data) {
  return (data & 0x8000U) != 0
             ? -static_cast<float>(0x10000UL - data) / 256.0f
             : static_cast<float>(data) / 256.0f;
}

uint16_t encode_f88(float value) {
  return static_cast<uint16_t>(static_cast<int32_t>(std::lround(value * 256.0f)));
}

}  // namespace

HCQOTBoilerSimulator::HCQOTBoilerSimulator() : PollingComponent(500) {}

HCQOTBoilerSimulator::~HCQOTBoilerSimulator() {
  if (opentherm_ != nullptr) {
    delete opentherm_;
    opentherm_ = nullptr;
  }
}

void HCQOTBoilerSimulator::set_response_enabled(bool value) {
  if (response_enabled_ == value) {
    return;
  }
  response_enabled_ = value;
  if (!response_enabled_) {
    response_scheduler_.cancel_pending(true);
  }
}

bool HCQOTBoilerSimulator::get_master_status_valid() const {
  const unsigned long now_ms = now_millis();
  return enabled_ && last_master_status_ms_ != 0 &&
         (now_ms - last_master_status_ms_) <= MASTER_STATUS_TIMEOUT_MS;
}

uint32_t HCQOTBoilerSimulator::get_last_request_age_ms() const {
  if (last_request_ms_ == 0) {
    return UINT32_MAX;
  }
  return static_cast<uint32_t>(now_millis() - last_request_ms_);
}

float HCQOTBoilerSimulator::get_boiler_temperature() const {
  return manual_telemetry_ ? manual_boiler_temperature_c_
                           : model_.state().boiler_temperature_c;
}

float HCQOTBoilerSimulator::get_return_temperature() const {
  return manual_telemetry_ ? manual_return_temperature_c_
                           : model_.state().return_temperature_c;
}

float HCQOTBoilerSimulator::get_relative_modulation() const {
  return manual_telemetry_ ? manual_modulation_pct_
                           : model_.state().relative_modulation_pct;
}

const char *HCQOTBoilerSimulator::get_mode_name() const {
  return hcq::ot_sim::BoilerSimulatorModel::mode_name(model_.state().mode);
}

void HCQOTBoilerSimulator::reset_model() {
  model_.reset(model_inputs_.ambient_c);
  last_model_update_ms_ = now_millis();
}

void HCQOTBoilerSimulator::reset_protocol_diagnostics() {
  request_count_ = 0;
  invalid_request_count_ = 0;
  last_request_id_ = -1;
  consecutive_duplicate_request_count_ = 0;
  last_consecutive_duplicate_request_id_ = -1;
  last_request_ms_ = 0;
  response_scheduler_.reset_diagnostics();
  if (opentherm_ != nullptr) {
    opentherm_->resetDiagnostics();
  }
}

void HCQOTBoilerSimulator::setup() {
  opentherm_ = new OpenTherm(in_pin_, out_pin_, true);
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  reset_model();
  if (enabled_) {
    schedule_start_();
  }
}

void HCQOTBoilerSimulator::dump_config() {
  ESP_LOGCONFIG(TAG, "HCQ OpenTherm boiler simulator:");
  ESP_LOGCONFIG(TAG, "  HCQ revision: 1.0");
  ESP_LOGCONFIG(TAG, "  Slave input: GPIO%u", in_pin_);
  ESP_LOGCONFIG(TAG, "  Slave output: GPIO%u", out_pin_);
  ESP_LOGCONFIG(TAG, "  DHW present: %s", YESNO(capabilities_.dhw_present));
  ESP_LOGCONFIG(TAG, "  Capacity: %u kW", capabilities_.max_capacity_kw);
  ESP_LOGCONFIG(TAG, "  Minimum modulation: %u%%", capabilities_.min_modulation_level);
  ESP_LOGCONFIG(TAG, "  Responses enabled: %s", YESNO(response_enabled_));
  ESP_LOGCONFIG(TAG, "  Response delay: %lu ms",
                static_cast<unsigned long>(response_scheduler_.delay_ms()));
}

void HCQOTBoilerSimulator::on_shutdown() { stop_(); }

#ifdef USE_OTA_STATE_LISTENER
void HCQOTBoilerSimulator::on_ota_global_state(ota::OTAState state, float progress,
                                                uint8_t error,
                                                ota::OTAComponent *component) {
  if (state == ota::OTA_STARTED) {
    ota_active_ = true;
    stop_();
  } else if (state == ota::OTA_ABORT || state == ota::OTA_ERROR ||
             state == ota::OTA_COMPLETED) {
    ota_active_ = false;
    if (enabled_) {
      schedule_start_();
    }
  }
}
#endif

void HCQOTBoilerSimulator::schedule_start_() {
  if (opentherm_ == nullptr || started_ || ota_active_ || !enabled_) {
    return;
  }
  start_pending_ = true;
  start_not_before_ms_ = now_millis() + STARTUP_SETTLE_MS;
  bus_idle_since_ms_ = 0;
}

void HCQOTBoilerSimulator::try_start_() {
  if (!start_pending_ || opentherm_ == nullptr || started_ || ota_active_ ||
      !enabled_) {
    return;
  }

  const unsigned long now_ms = now_millis();
  if (now_ms < start_not_before_ms_) {
    return;
  }
  if ((now_ms - start_not_before_ms_) >= STARTUP_FORCE_MS) {
    start_();
    return;
  }

  const bool bus_idle = gpio_get_level(static_cast<gpio_num_t>(in_pin_)) != 0;
  if (!bus_idle) {
    bus_idle_since_ms_ = 0;
    return;
  }
  if (bus_idle_since_ms_ == 0) {
    bus_idle_since_ms_ = now_ms;
    return;
  }
  if ((now_ms - bus_idle_since_ms_) >= BUS_IDLE_MIN_MS) {
    start_();
  }
}

void HCQOTBoilerSimulator::start_() {
  if (opentherm_ == nullptr || started_ || ota_active_ || !enabled_) {
    return;
  }
  if (!opentherm_->begin(nullptr, process_request_callback_, this)) {
    ESP_LOGE(TAG, "OpenTherm RMT startup failed");
    start_pending_ = false;
    mark_failed();
    return;
  }
  started_ = true;
  start_pending_ = false;
  bus_idle_since_ms_ = 0;
  ESP_LOGI(TAG, "OpenTherm slave started");
}

void HCQOTBoilerSimulator::stop_() {
  start_pending_ = false;
  bus_idle_since_ms_ = 0;
  response_scheduler_.cancel_pending(false);
  if (opentherm_ != nullptr && started_) {
    opentherm_->end();
  }
  started_ = false;
}

void HCQOTBoilerSimulator::loop() {
  if (!ota_active_ && enabled_ && !started_) {
    if (!start_pending_) {
      schedule_start_();
    }
    try_start_();
  }
  if (started_ && opentherm_ != nullptr) {
    opentherm_->process();
    try_send_pending_response_();
  }
}

void HCQOTBoilerSimulator::update() {
  const unsigned long now_ms = now_millis();
  const float delta_s = last_model_update_ms_ == 0
                            ? 0.5f
                            : static_cast<float>(now_ms - last_model_update_ms_) / 1000.0f;
  last_model_update_ms_ = now_ms;
  refresh_model_(delta_s);
}

void HCQOTBoilerSimulator::refresh_model_(float delta_s) {
  auto inputs = model_inputs_;
  inputs.automatic = automatic_mode_;
  inputs.master_status_valid = get_master_status_valid();
  inputs.master_ch_enable = master_state_.ch_enable;
  inputs.master_dhw_enable = master_state_.dhw_enable;
  inputs.dhw_demand = dhw_demand_;
  inputs.manual_ch_active = manual_ch_active_;
  inputs.manual_dhw_active = manual_dhw_active_;
  inputs.manual_flame_on = manual_flame_on_;
  inputs.t_set_c = master_state_.t_set_c;
  inputs.minimum_modulation_pct = capabilities_.min_modulation_level;
  const bool protocol_fault = fault_flags_.low_water_pressure || fault_flags_.flame_fault ||
                              fault_flags_.air_pressure_fault || fault_flags_.water_over_temp;
  inputs.force_fault = fault_active_ || protocol_fault;
  inputs.force_diagnostic = diagnostic_active_ || fault_flags_.service_request ||
                            inputs.force_fault;
  model_.step(inputs, delta_s);
}

void HCQOTBoilerSimulator::process_request_callback_(unsigned long request,
                                                      OpenThermResponseStatus status,
                                                      void *context) {
  if (context != nullptr) {
    static_cast<HCQOTBoilerSimulator *>(context)->process_request_(request, status);
  }
}

void HCQOTBoilerSimulator::process_request_(unsigned long request,
                                             OpenThermResponseStatus status) {
  if (status != OpenThermResponseStatus::SUCCESS) {
    invalid_request_count_++;
    return;
  }
  if (opentherm_ == nullptr) {
    return;
  }

  const auto type = opentherm_->getMessageType(request);
  const auto id = opentherm_->getDataID(request);
  const uint16_t data = static_cast<uint16_t>(request);
  last_request_ms_ = now_millis();
  request_count_++;
  if (last_request_id_ == static_cast<int>(id)) {
    consecutive_duplicate_request_count_++;
    last_consecutive_duplicate_request_id_ = static_cast<int>(id);
  }
  last_request_id_ = static_cast<int>(id);
  parse_request_(type, id, data);

  if (!response_enabled_) {
    response_scheduler_.mark_suppressed();
    return;
  }
  const unsigned long response = build_response_(type, id, data);
  if (response != 0) {
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    const uint32_t rx_age_us = opentherm_->getLastRxFrameAgeUs();
    const uint64_t request_end_us = rx_age_us <= now_us ? now_us - rx_age_us : now_us;
    response_scheduler_.schedule(static_cast<uint32_t>(response), request_end_us);
  }
}

void HCQOTBoilerSimulator::try_send_pending_response_() {
  if (!response_scheduler_.pending() || opentherm_ == nullptr || !started_) {
    return;
  }
  if (!response_enabled_) {
    response_scheduler_.cancel_pending(true);
    return;
  }

  const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
  if (!response_scheduler_.due(now_us)) {
    return;
  }

  if (opentherm_->sendResponse(response_scheduler_.pending_frame())) {
    response_scheduler_.mark_queued(now_us);
  } else {
    response_scheduler_.mark_queue_failed();
  }
}

void HCQOTBoilerSimulator::parse_request_(OpenThermMessageType type,
                                           OpenThermMessageID id,
                                           uint16_t data) {
  if (id == OpenThermMessageID::Status) {
    master_state_.ch_enable = (data & (1U << 8)) != 0;
    master_state_.dhw_enable = (data & (1U << 9)) != 0;
    last_master_status_ms_ = now_millis();
  }
  if (type != OpenThermMessageType::WRITE_DATA) {
    return;
  }
  if (id == OpenThermMessageID::TSet) {
    master_state_.t_set_c = decode_f88(data);
  } else if (id == OpenThermMessageID::MaxRelModLevelSetting) {
    master_state_.max_relative_modulation = decode_f88(data);
  }
}

uint16_t HCQOTBoilerSimulator::build_status_(uint16_t master_status) const {
  uint16_t status = static_cast<uint16_t>(master_status & 0xFF00U);
  status = set_bit(status, 0, model_.state().fault);
  status = set_bit(status, 1, model_.state().ch_active);
  status = set_bit(status, 2, model_.state().dhw_active);
  status = set_bit(status, 3, model_.state().flame_on);
  status = set_bit(status, 6, model_.state().diagnostic);
  return status;
}

uint16_t HCQOTBoilerSimulator::build_config_() const {
  uint16_t config = capabilities_.member_id;
  config = set_bit(config, 8, capabilities_.dhw_present);
  config = set_bit(config, 9, capabilities_.control_type_modulating);
  config = set_bit(config, 10, capabilities_.cooling_supported);
  return config;
}

uint16_t HCQOTBoilerSimulator::build_fault_flags_() const {
  uint16_t flags = fault_flags_.oem_fault_code;
  flags = set_bit(flags, 8, fault_flags_.service_request);
  flags = set_bit(flags, 9, fault_flags_.lockout_reset);
  flags = set_bit(flags, 10, fault_flags_.low_water_pressure);
  flags = set_bit(flags, 11, fault_flags_.flame_fault);
  flags = set_bit(flags, 12, fault_flags_.air_pressure_fault);
  flags = set_bit(flags, 13, fault_flags_.water_over_temp);
  return flags;
}

unsigned long HCQOTBoilerSimulator::build_response_(OpenThermMessageType type,
                                                     OpenThermMessageID id,
                                                     uint16_t data) {
  OpenThermMessageType response_type =
      type == OpenThermMessageType::WRITE_DATA ? OpenThermMessageType::WRITE_ACK
                                               : OpenThermMessageType::READ_ACK;
  uint16_t response_data = data;

  if (type == OpenThermMessageType::READ_DATA) {
    switch (id) {
      case OpenThermMessageID::Status:
        response_data = build_status_(data);
        break;
      case OpenThermMessageID::SConfigSMemberIDcode:
        response_data = build_config_();
        break;
      case OpenThermMessageID::ASFflags:
        response_data = build_fault_flags_();
        break;
      case OpenThermMessageID::RBPflags:
        response_data = 0x0200;  // MaxTSet is available and read-only.
        break;
      case OpenThermMessageID::MaxCapacityMinModLevel:
        response_data = (static_cast<uint16_t>(capabilities_.max_capacity_kw) << 8) |
                        capabilities_.min_modulation_level;
        break;
      case OpenThermMessageID::RelModLevel:
        if (!modulation_valid_) {
          response_type = OpenThermMessageType::DATA_INVALID;
          response_data = 0;
        } else {
          response_data = encode_f88(get_relative_modulation());
        }
        break;
      case OpenThermMessageID::CHPressure:
        if (!pressure_valid_) {
          response_type = OpenThermMessageType::DATA_INVALID;
          response_data = 0;
        } else {
          response_data = encode_f88(pressure_bar_);
        }
        break;
      case OpenThermMessageID::Tboiler:
        if (!boiler_temperature_valid_) {
          response_type = OpenThermMessageType::DATA_INVALID;
          response_data = 0;
        } else {
          response_data = encode_f88(get_boiler_temperature());
        }
        break;
      case OpenThermMessageID::Tret:
        if (!return_temperature_valid_) {
          response_type = OpenThermMessageType::DATA_INVALID;
          response_data = 0;
        } else {
          response_data = encode_f88(get_return_temperature());
        }
        break;
      case OpenThermMessageID::Tdhw:
      case OpenThermMessageID::Tdhw2:
        if (!dhw_temperature_valid_) {
          response_type = OpenThermMessageType::DATA_INVALID;
          response_data = 0;
        } else {
          response_data = encode_f88(dhw_temperature_c_);
        }
        break;
      case OpenThermMessageID::TdhwSetUBTdhwSetLB:
        response_data = 0x3C0A;
        break;
      case OpenThermMessageID::TdhwSet:
        response_data = encode_f88(model_inputs_.dhw_target_c);
        break;
      case OpenThermMessageID::MaxTSetUBMaxTSetLB:
        response_data = 0x5A0A;
        break;
      case OpenThermMessageID::MaxTSet:
        response_data = encode_f88(max_t_set_c_);
        break;
      case OpenThermMessageID::OEMDiagnosticCode:
        response_data = fault_flags_.oem_diagnostic_code;
        break;
      case OpenThermMessageID::OpenThermVersionSlave:
        response_data = encode_f88(capabilities_.ot_version);
        break;
      case OpenThermMessageID::SlaveVersion:
        response_data = (static_cast<uint16_t>(capabilities_.product_version) << 8) |
                        capabilities_.product_type;
        break;
      case OpenThermMessageID::TflowCH2:
      case OpenThermMessageID::DHWFlowRate:
      case OpenThermMessageID::Toutside:
      case OpenThermMessageID::Texhaust:
        response_type = OpenThermMessageType::DATA_INVALID;
        response_data = 0;
        break;
      default:
        response_type = OpenThermMessageType::UNKNOWN_DATA_ID;
        response_data = 0;
        break;
    }
  }

  return opentherm_->buildResponse(response_type, id, response_data);
}

}  // namespace esphome::hcq_ot_boiler_simulator
