#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "quatt_odu_performance.generated.h"
#include "quatt_odu_profiles.generated.h"
#include "quatt_odu_registers.generated.h"

namespace esphome::quatt_odu_simulator {

enum class WorkingMode : uint8_t { STANDBY = 0, COOLING = 1, HEATING = 2 };

struct WriteDiagnostics {
  uint16_t received_raw{0};
  uint16_t accepted_raw{0};
  uint32_t last_write_ms{0};
  uint32_t write_count{0};
  uint32_t invalid_write_count{0};
};

struct ProtocolDiagnostics {
  uint32_t request_count{0};
  uint32_t read_count{0};
  uint32_t write_count{0};
  uint32_t dropped_response_count{0};
  uint32_t exception_count{0};
  uint32_t invalid_address_count{0};
  uint32_t invalid_write_count{0};
  uint32_t level_capability_violations{0};
  uint32_t legacy_extension_read_violations{0};
  uint8_t last_function_code{0};
  uint16_t last_start_address{0};
  uint16_t last_register_count{0};
  uint16_t last_write_address{0};
  uint16_t last_write_value{0};
  uint8_t highest_requested_level{0};
  uint32_t last_request_ms{0};
};

struct ModelSettings {
  float compressor_start_delay_s{2.0f};
  float ramp_up_hz_s{12.0f};
  float ramp_down_hz_s{20.0f};
  float minimum_runtime_s{5.0f};
  float stop_delay_s{1.0f};
  float flow_start_delay_s{1.0f};
  float flow_offset_lph{950.0f};
  float flow_gain_lph_per_ipwm{-1.0f};
  float flow_max_lph{1200.0f};
  float water_response_tau_s{4.0f};
  float power_factor{0.92f};
  float demand_limiter{1.0f};
  float maximum_frequency_hz{120.0f};
  bool freeze_measured_frequency{false};
  bool hold_level_during_defrost{true};
  bool experimental_high_frequency_extrapolation{false};
};

struct OduState {
  Profile profile{Profile::DISABLED};
  WorkingMode requested_mode{WorkingMode::STANDBY};
  WorkingMode active_mode{WorkingMode::STANDBY};
  uint16_t requested_physical_level{0};
  uint8_t accepted_physical_level{0};
  uint8_t deferred_defrost_level{0};
  bool deferred_defrost_level_pending{false};
  bool silent_mode{false};
  bool pump_request{false};
  uint16_t pump_ipwm{0};
  uint16_t pump_feedback_override{0xFFFFU};
  float target_frequency_hz{0.0f};
  float measured_frequency_hz{0.0f};
  float outside_temperature_c{7.0f};
  float water_in_temperature_c{30.0f};
  float water_out_temperature_c{30.0f};
  float evaporator_coil_temperature_c{3.0f};
  float gas_discharge_temperature_c{35.0f};
  float gas_return_temperature_c{10.0f};
  float condensing_temperature_c{35.0f};
  float evaporating_temperature_c{2.0f};
  float inner_coil_temperature_c{30.0f};
  float evaporator_pressure_bar{4.0f};
  float condenser_pressure_bar{10.0f};
  float fan_speed_rpm{0.0f};
  float fan_speed_max_rpm{950.0f};
  float eev_steps{0.0f};
  float flow_lph{0.0f};
  float thermal_power_w{0.0f};
  float electrical_power_w{0.0f};
  float cop{0.0f};
  float ac_voltage_v{230.0f};
  float ac_current_a{0.0f};
  bool flow_switch{false};
  bool force_no_flow{false};
  bool force_flow_without_relay{false};
  bool force_flow_switch_off{false};
  bool force_flow_switch_on{false};
  bool defrost{false};
  bool high_frequency_performance_synthetic{false};
  std::array<uint16_t, 3> fault_words{};
  std::array<uint8_t, 21> cooling_table{};
  std::array<uint8_t, 21> heating_table{};
  bool table_dirty{false};
  bool table_write_enabled{true};
  uint32_t table_write_count{0};
  uint32_t table_rejected_write_count{0};
  uint16_t last_table_write_address{0};
  uint16_t last_table_write_value{0};
  float start_timer_s{0.0f};
  float running_timer_s{0.0f};
  float stop_timer_s{0.0f};
  float pump_timer_s{0.0f};
  ProtocolDiagnostics protocol{};
  std::array<WriteDiagnostics, 5> actuator_writes{};
};

struct PerformancePoint {
  float thermal_power_w{0.0f};
  float cop{0.0f};
  bool high_frequency_synthetic{false};
};

class QuattOduSimulatorModel {
 public:
  const OduState& state() const { return this->state_; }
  OduState& mutable_state() { return this->state_; }
  const ModelSettings& settings() const { return this->settings_; }
  ModelSettings& mutable_settings() { return this->settings_; }

  void configure(Profile profile) {
    this->state_ = {};
    this->state_.profile = profile;
    const auto& definition = profile_definition(profile);
    this->state_.cooling_table = definition.factory_cooling;
    this->state_.heating_table = definition.factory_heating;
  }

  bool enabled() const { return profile_definition(this->state_.profile).enabled; }
  uint8_t maximum_level() const { return profile_definition(this->state_.profile).max_physical_level; }

  void reset_diagnostics() {
    this->state_.protocol = {};
    this->state_.actuator_writes = {};
    this->state_.table_write_count = 0U;
    this->state_.table_rejected_write_count = 0U;
    this->state_.last_table_write_address = 0U;
    this->state_.last_table_write_value = 0U;
  }

  void record_rejected_write(uint16_t address, uint16_t value = 0U, uint32_t now_ms = 0U) {
    this->state_.protocol.invalid_write_count++;
    this->state_.protocol.exception_count++;
    if (address >= 3000U && address <= 3069U) {
      this->state_.table_rejected_write_count++;
      return;
    }
    if (address != 1999U && address != 2006U && address != 2010U && address != 2015U && address != 3999U) return;
    auto& diagnostics = this->state_.actuator_writes[actuator_diagnostic_index_(address)];
    diagnostics.received_raw = value;
    diagnostics.last_write_ms = now_ms;
    diagnostics.invalid_write_count++;
  }

  void begin_request(uint8_t function_code, uint16_t start_address, uint16_t count, uint32_t now_ms) {
    auto& diagnostics = this->state_.protocol;
    diagnostics.request_count++;
    diagnostics.last_function_code = function_code;
    diagnostics.last_start_address = start_address;
    diagnostics.last_register_count = count;
    diagnostics.last_request_ms = now_ms;
  }

  static bool valid_frequency_table(const std::array<uint8_t, 21>& table, uint8_t level_count) {
    if (level_count != 11U && level_count != 21U) return false;
    if (table[0] != 0U) return false;
    uint8_t previous = 0U;
    for (uint8_t level = 1U; level < level_count; level++) {
      const uint8_t frequency = table[level];
      if (frequency == 0U || frequency > 120U || frequency < previous) return false;
      previous = frequency;
    }
    return true;
  }

  bool runtime_table_valid(WorkingMode mode) const {
    return valid_frequency_table(mode == WorkingMode::COOLING ? this->state_.cooling_table : this->state_.heating_table,
                                 static_cast<uint8_t>(this->maximum_level() + 1U));
  }

  void restore_factory_tables() {
    const auto& definition = profile_definition(this->state_.profile);
    this->state_.cooling_table = definition.factory_cooling;
    this->state_.heating_table = definition.factory_heating;
    this->state_.table_dirty = false;
  }

  bool apply_runtime_modified_preset() {
    const auto* preset = runtime_modified_preset(this->state_.profile);
    if (preset == nullptr) return false;
    this->state_.cooling_table = preset->cooling;
    this->state_.heating_table = preset->heating;
    this->state_.table_dirty = true;
    return true;
  }

  bool can_read_register(uint16_t address) const {
    if (!this->enabled()) return false;
    for (const auto& descriptor : REGISTER_DESCRIPTORS) {
      if (descriptor.address == address) return access_readable(descriptor.access);
    }
    for (const auto& descriptor : REGISTER_RANGE_DESCRIPTORS) {
      if (address >= descriptor.start && address <= descriptor.end) return access_readable(descriptor.access);
    }
    return false;
  }

  bool can_write_register(uint16_t address, uint16_t value) const {
    if (!this->enabled()) return false;
    if (address == 1999U) return true;
    if (address == 2006U) return value <= 1U;
    if (address == 2010U) return value == 0U || value == 4096U;
    if (address == 2015U) return value <= 1000U;
    if (address == 3999U) return value <= 2U;
    if (address >= 3000U && address <= 3021U) return this->state_.table_write_enabled && value <= 120U;
    if (address >= 3050U && address <= 3069U)
      return this->state_.profile == Profile::V2_NEW && this->state_.table_write_enabled && value <= 120U;
    return false;
  }

  bool read_register(uint16_t address, uint16_t& value) {
    if (!this->can_read_register(address)) return false;
    const auto& definition = profile_definition(this->state_.profile);
    switch (address) {
      case 1999:
        value = this->state_.accepted_physical_level;
        return true;
      case 2006:
        value = this->state_.silent_mode ? 1U : 0U;
        return true;
      case 2010:
        value = this->state_.pump_request ? 4096U : 0U;
        return true;
      case 2015:
        value = this->state_.pump_ipwm;
        return true;
      case 2099:
        value = static_cast<uint16_t>(this->state_.active_mode);
        return true;
      case 2100:
        value = encode_unsigned_(this->state_.ac_voltage_v);
        return true;
      case 2101:
        value = encode_unsigned_(this->state_.ac_current_a * 10.0f);
        return true;
      case 2102:
        value = encode_unsigned_(this->state_.target_frequency_hz);
        return true;
      case 2103:
        value = encode_unsigned_(this->state_.measured_frequency_hz);
        return true;
      case 2104:
        value = encode_unsigned_(this->state_.fan_speed_max_rpm);
        return true;
      case 2105:
        value = encode_unsigned_(this->state_.fan_speed_rpm);
        return true;
      case 2106:
        value = 0U;
        return true;
      case 2107:
        value = encode_unsigned_(this->state_.eev_steps);
        return true;
      case 2108:
        value = this->status_2108_();
        return true;
      case 2109:
        value = 0U;
        return true;
      case 2110:
        value = encode_temperature_(this->state_.outside_temperature_c);
        return true;
      case 2111:
        value = encode_temperature_(this->state_.evaporator_coil_temperature_c);
        return true;
      case 2112:
        value = encode_temperature_(this->state_.gas_discharge_temperature_c);
        return true;
      case 2113:
        value = encode_temperature_(this->state_.gas_return_temperature_c);
        return true;
      case 2114:
        value = definition.compressor_code;
        return true;
      case 2115:
        value = this->state_.flow_switch ? 0x2000U : 0U;
        return true;
      case 2116:
        value = encode_unsigned_(this->state_.evaporator_pressure_bar * 10.0f);
        return true;
      case 2117:
        value = encode_unsigned_(this->state_.condenser_pressure_bar * 10.0f);
        return true;
      case 2118:
        value = this->state_.defrost ? 1U : 0U;
        return true;
      case 2119:
        value = this->state_.fault_words[0];
        return true;
      case 2120:
        value = this->state_.fault_words[1];
        return true;
      case 2121:
        value = this->state_.fault_words[2];
        return true;
      case 2122:
        value = definition.pcb_program;
        return true;
      case 2123:
        value = 0x0001U;
        return true;
      case 2124:
        value = 0x0000U;
        return true;
      case 2125:
        value = definition.pcb_program;
        return true;
      case 2126:
        value = 0x0000U;
        return true;
      case 2127:
        value = definition.control_board_item;
        return true;
      case 2128:
      case 2129:
      case 2130:
      case 2136:
        value = 0U;
        return true;
      case 2131:
        value = encode_temperature_(this->state_.condensing_temperature_c);
        return true;
      case 2132:
        value = encode_temperature_(this->state_.evaporating_temperature_c);
        return true;
      case 2133:
        value = encode_temperature_(this->state_.water_in_temperature_c);
        return true;
      case 2134:
        value = encode_temperature_(this->state_.water_out_temperature_c);
        return true;
      case 2135:
        value = encode_temperature_(this->state_.inner_coil_temperature_c);
        return true;
      case 2137:
        value = this->pump_feedback_raw_();
        return true;
      case 2138:
        value = encode_unsigned_(this->state_.flow_lph / 0.618f);
        return true;
      case 3999:
        value = static_cast<uint16_t>(this->state_.requested_mode);
        return true;
      case 11160:
        value = definition.customer_model_words[0];
        return true;
      case 11161:
        value = definition.customer_model_words[1];
        return true;
      default:
        break;
    }

    if (address >= 3000U && address <= 3010U) {
      value = this->state_.cooling_table[address - 3000U];
      return true;
    }
    if (address >= 3011U && address <= 3021U) {
      value = this->state_.heating_table[address - 3011U];
      return true;
    }
    if (address >= 3050U && address <= 3059U) {
      if (this->state_.profile == Profile::V2_NEW)
        value = this->state_.heating_table[11U + address - 3050U];
      else
        value = LEGACY_HEATING_EXTENSION[address - 3050U];
      return true;
    }
    if (address >= 3060U && address <= 3069U) {
      if (this->state_.profile == Profile::V2_NEW)
        value = this->state_.cooling_table[11U + address - 3060U];
      else
        value = LEGACY_COOLING_EXTENSION[address - 3060U];
      return true;
    }
    if (address >= 2999U && address <= 3510U) {
      value = address == 2999U ? 0x51A1U : 0U;
      return true;
    }
    if (address >= 11004U && address <= 11009U) {
      value = 0U;
      return true;
    }
    if (address >= 11120U && address <= 11139U)
      return encode_ascii_word_(profile_model_text_(), address - 11120U, value);
    if (address >= 11160U && address <= 11179U)
      return encode_ascii_word_(profile_customer_text_(), address - 11160U, value);
    if (address >= 11219U && address <= 11238U) return encode_ascii_word_("SIM-ODU-REDACTED", address - 11219U, value);
    return false;
  }

  bool write_register(uint16_t address, uint16_t value, uint32_t now_ms) {
    if (!this->can_write_register(address, value)) {
      this->state_.protocol.invalid_write_count++;
      if (address >= 3000U && address <= 3069U) this->state_.table_rejected_write_count++;
      return false;
    }

    this->state_.protocol.write_count++;
    this->state_.protocol.last_write_address = address;
    this->state_.protocol.last_write_value = value;
    if (address >= 3000U && address <= 3069U) {
      uint8_t* target = nullptr;
      size_t index = 0U;
      if (address <= 3010U) {
        target = this->state_.cooling_table.data();
        index = address - 3000U;
      } else if (address <= 3021U) {
        target = this->state_.heating_table.data();
        index = address - 3011U;
      } else if (address <= 3059U) {
        target = this->state_.heating_table.data();
        index = 11U + address - 3050U;
      } else {
        target = this->state_.cooling_table.data();
        index = 11U + address - 3060U;
      }
      target[index] = static_cast<uint8_t>(value);
      this->state_.table_dirty = true;
      this->state_.table_write_count++;
      this->state_.last_table_write_address = address;
      this->state_.last_table_write_value = value;
      return true;
    }

    const size_t diagnostic_index = actuator_diagnostic_index_(address);
    auto& diagnostics = this->state_.actuator_writes[diagnostic_index];
    diagnostics.received_raw = value;
    diagnostics.last_write_ms = now_ms;
    diagnostics.write_count++;
    switch (address) {
      case 1999: {
        this->state_.requested_physical_level = value;
        this->state_.protocol.highest_requested_level = std::max<uint8_t>(
            this->state_.protocol.highest_requested_level, static_cast<uint8_t>(std::min<uint16_t>(value, 255U)));
        const uint8_t accepted = static_cast<uint8_t>(std::min<uint16_t>(value, this->maximum_level()));
        if (value > this->maximum_level()) this->state_.protocol.level_capability_violations++;
        if (this->state_.defrost && this->settings_.hold_level_during_defrost) {
          this->state_.deferred_defrost_level = accepted;
          this->state_.deferred_defrost_level_pending = true;
        } else {
          this->state_.accepted_physical_level = accepted;
        }
        diagnostics.accepted_raw = accepted;
        return true;
      }
      case 2006:
        this->state_.silent_mode = value != 0U;
        diagnostics.accepted_raw = value;
        return true;
      case 2010:
        this->state_.pump_request = value == 4096U;
        diagnostics.accepted_raw = value;
        return true;
      case 2015:
        this->state_.pump_ipwm = value;
        diagnostics.accepted_raw = value;
        return true;
      case 3999:
        this->state_.requested_mode = static_cast<WorkingMode>(value);
        diagnostics.accepted_raw = value;
        return true;
      default:
        diagnostics.invalid_write_count++;
        return false;
    }
  }

  void set_defrost(bool defrost) {
    if (this->state_.defrost && !defrost && this->state_.deferred_defrost_level_pending) {
      this->state_.accepted_physical_level = this->state_.deferred_defrost_level;
      this->state_.deferred_defrost_level = 0U;
      this->state_.deferred_defrost_level_pending = false;
    }
    this->state_.defrost = defrost;
  }

  void update(float dt_s) {
    if (!this->enabled() || dt_s <= 0.0f) return;
    dt_s = std::min(dt_s, 2.0f);
    this->update_pump_(dt_s);
    this->update_compressor_(dt_s);
    this->update_thermodynamics_(dt_s);
  }

  PerformancePoint performance_at(float frequency_hz, float ambient_c, float supply_c) const {
    if (frequency_hz <= 0.0f) return {};
    const bool v2 = this->state_.profile == Profile::V2_OLD || this->state_.profile == Profile::V2_NEW;
    const auto& grid = v2 ? V2_GRID : V1_GRID;
    const bool high = frequency_hz > grid.frequency_hz.back();
    const float used_frequency =
        high && !this->settings_.experimental_high_frequency_extrapolation ? grid.frequency_hz.back() : frequency_hz;
    const bool extrapolate_frequency = high && this->settings_.experimental_high_frequency_extrapolation;
    const float power =
        interpolate_grid_(grid, grid.thermal_power_w, used_frequency, ambient_c, supply_c, extrapolate_frequency);
    const float cop = interpolate_grid_(grid, grid.cop, used_frequency, ambient_c, supply_c, extrapolate_frequency);
    return {std::max(0.0f, power), std::max(0.1f, cop), high};
  }

 private:
  static uint16_t encode_unsigned_(float value) {
    return static_cast<uint16_t>(std::clamp<long>(lroundf(value), 0L, 65535L));
  }
  static uint16_t encode_temperature_(float value) { return encode_unsigned_(value * 100.0f + 3000.0f); }

  static float interpolate_grid_(const PerformanceGrid& grid, const float* values, float frequency, float ambient,
                                 float supply, bool extrapolate_frequency) {
    const auto interval = [](const float* points, size_t count, float value) {
      if (value <= points[0]) return size_t{0};
      if (value >= points[count - 1U]) return count - 2U;
      for (size_t index = 0; index + 1U < count; index++)
        if (value <= points[index + 1U]) return index;
      return count - 2U;
    };
    const auto fraction = [](float value, float low, float high, bool clamp) {
      const float result = high == low ? 0.0f : (value - low) / (high - low);
      return clamp ? std::clamp(result, 0.0f, 1.0f) : result;
    };
    const size_t fi = interval(grid.frequency_hz.data(), grid.frequency_hz.size(), frequency);
    const size_t ai = interval(grid.ambient_c.data(), grid.ambient_c.size(), ambient);
    const size_t si = interval(grid.supply_c.data(), grid.supply_c.size(), supply);
    const float ft = fraction(frequency, grid.frequency_hz[fi], grid.frequency_hz[fi + 1U], !extrapolate_frequency);
    const float at = fraction(ambient, grid.ambient_c[ai], grid.ambient_c[ai + 1U], true);
    const float st = fraction(supply, grid.supply_c[si], grid.supply_c[si + 1U], true);
    const auto sample = [values](size_t ambient_index, size_t supply_index, size_t frequency_index) {
      return values[(ambient_index * 2U + supply_index) * 10U + frequency_index];
    };
    const auto lerp = [](float low, float high, float amount) { return low + (high - low) * amount; };
    const float a00 = lerp(sample(ai, si, fi), sample(ai, si, fi + 1U), ft);
    const float a01 = lerp(sample(ai, si + 1U, fi), sample(ai, si + 1U, fi + 1U), ft);
    const float a10 = lerp(sample(ai + 1U, si, fi), sample(ai + 1U, si, fi + 1U), ft);
    const float a11 = lerp(sample(ai + 1U, si + 1U, fi), sample(ai + 1U, si + 1U, fi + 1U), ft);
    return lerp(lerp(a00, a10, at), lerp(a01, a11, at), st);
  }

  void update_pump_(float dt_s) {
    const bool relay = this->state_.pump_request && !this->state_.force_flow_without_relay;
    this->state_.pump_timer_s = relay ? this->state_.pump_timer_s + dt_s : 0.0f;
    float target_flow = 0.0f;
    if ((relay || this->state_.force_flow_without_relay) && !this->state_.force_no_flow) {
      target_flow =
          std::clamp(this->settings_.flow_offset_lph + this->settings_.flow_gain_lph_per_ipwm * this->state_.pump_ipwm,
                     0.0f, this->settings_.flow_max_lph);
    }
    const float amount = std::clamp(dt_s / 2.0f, 0.0f, 1.0f);
    this->state_.flow_lph += (target_flow - this->state_.flow_lph) * amount;
    this->state_.flow_switch =
        this->state_.force_flow_switch_on || (!this->state_.force_flow_switch_off && this->state_.flow_lph >= 250.0f &&
                                              this->state_.pump_timer_s >= this->settings_.flow_start_delay_s);
  }

  void update_compressor_(float dt_s) {
    const bool demand =
        this->state_.requested_mode != WorkingMode::STANDBY && this->state_.accepted_physical_level > 0U;
    if (demand && this->state_.active_mode == WorkingMode::STANDBY) {
      this->state_.start_timer_s += dt_s;
      if (this->state_.start_timer_s >= this->settings_.compressor_start_delay_s) {
        this->state_.active_mode = this->state_.requested_mode;
        this->state_.running_timer_s = 0.0f;
      }
    } else if (!demand) {
      this->state_.start_timer_s = 0.0f;
    }
    if (this->state_.active_mode != WorkingMode::STANDBY) {
      this->state_.running_timer_s += dt_s;
      const bool mode_change = demand && this->state_.requested_mode != this->state_.active_mode;
      if ((!demand || mode_change) && this->state_.running_timer_s >= this->settings_.minimum_runtime_s) {
        this->state_.stop_timer_s += dt_s;
        if (this->state_.stop_timer_s >= this->settings_.stop_delay_s && this->state_.measured_frequency_hz <= 0.1f) {
          this->state_.active_mode = WorkingMode::STANDBY;
          this->state_.stop_timer_s = 0.0f;
          this->state_.start_timer_s = 0.0f;
        }
      } else {
        this->state_.stop_timer_s = 0.0f;
      }
    }

    float target = 0.0f;
    if (this->state_.active_mode != WorkingMode::STANDBY && this->state_.active_mode == this->state_.requested_mode &&
        demand && this->runtime_table_valid(this->state_.active_mode)) {
      const auto& table =
          this->state_.active_mode == WorkingMode::COOLING ? this->state_.cooling_table : this->state_.heating_table;
      target = table[this->state_.accepted_physical_level] * this->settings_.demand_limiter;
      target = std::min(target, this->settings_.maximum_frequency_hz);
    }
    this->state_.target_frequency_hz = target;
    if (!this->settings_.freeze_measured_frequency) {
      const float rate =
          target >= this->state_.measured_frequency_hz ? this->settings_.ramp_up_hz_s : this->settings_.ramp_down_hz_s;
      const float change = rate * dt_s;
      if (std::fabs(target - this->state_.measured_frequency_hz) <= change)
        this->state_.measured_frequency_hz = target;
      else
        this->state_.measured_frequency_hz += target > this->state_.measured_frequency_hz ? change : -change;
    }
  }

  void update_thermodynamics_(float dt_s) {
    const auto performance =
        this->performance_at(this->state_.measured_frequency_hz, this->state_.outside_temperature_c,
                             std::max(this->state_.water_in_temperature_c, this->state_.water_out_temperature_c));
    this->state_.high_frequency_performance_synthetic = performance.high_frequency_synthetic;
    this->state_.thermal_power_w = performance.thermal_power_w;
    this->state_.cop = performance.cop;
    this->state_.electrical_power_w =
        performance.thermal_power_w <= 0.0f ? 0.0f : performance.thermal_power_w / performance.cop;
    this->state_.ac_current_a =
        this->state_.electrical_power_w / std::max(1.0f, this->state_.ac_voltage_v * this->settings_.power_factor);

    float target_out = this->state_.water_in_temperature_c;
    if (this->state_.flow_lph > 1.0f && this->state_.thermal_power_w > 0.0f) {
      const float delta = this->state_.thermal_power_w / ((this->state_.flow_lph / 3600.0f) * 4180.0f);
      const float sign = this->state_.active_mode == WorkingMode::COOLING ? -1.0f : 1.0f;
      target_out += sign * delta;
      if (this->state_.defrost) target_out -= 4.0f;
    }
    this->state_.water_out_temperature_c += (target_out - this->state_.water_out_temperature_c) *
                                            std::clamp(dt_s / this->settings_.water_response_tau_s, 0.0f, 1.0f);
    this->state_.condensing_temperature_c =
        this->state_.water_out_temperature_c + 5.0f + this->state_.measured_frequency_hz * 0.03f;
    this->state_.evaporating_temperature_c =
        this->state_.outside_temperature_c - 5.0f - this->state_.measured_frequency_hz * 0.02f;
    this->state_.evaporator_coil_temperature_c = this->state_.evaporating_temperature_c + 1.0f;
    this->state_.gas_return_temperature_c = this->state_.evaporating_temperature_c + 6.0f;
    this->state_.gas_discharge_temperature_c =
        this->state_.condensing_temperature_c + 12.0f + this->state_.measured_frequency_hz * 0.25f;
    this->state_.inner_coil_temperature_c =
        (this->state_.water_in_temperature_c + this->state_.water_out_temperature_c) * 0.5f;
    this->state_.evaporator_pressure_bar = std::max(1.0f, 4.0f + this->state_.evaporating_temperature_c * 0.08f);
    this->state_.condenser_pressure_bar = std::max(2.0f, 8.0f + this->state_.condensing_temperature_c * 0.15f);
    this->state_.fan_speed_rpm = std::min(this->state_.fan_speed_max_rpm, this->state_.measured_frequency_hz * 10.0f);
    this->state_.eev_steps =
        this->state_.measured_frequency_hz <= 0.0f ? 0.0f : 90.0f + this->state_.measured_frequency_hz * 2.2f;
  }

  uint16_t status_2108_() const {
    uint16_t status = 0U;
    if (this->state_.fan_speed_rpm > 0.0f && this->state_.fan_speed_rpm < 400.0f) status |= 0x0001U;
    if (this->state_.outside_temperature_c < 2.0f) status |= 0x0004U;
    if (this->state_.outside_temperature_c < 5.0f) status |= 0x0008U;
    if (this->state_.defrost) status |= 0x0010U;
    if (this->state_.fan_speed_rpm > 700.0f) status |= 0x0020U;
    if (this->state_.active_mode == WorkingMode::HEATING && !this->state_.defrost) status |= 0x0040U;
    if (this->state_.pump_request) status |= 0x0800U;
    return status;
  }

  uint16_t pump_feedback_raw_() const {
    if (this->state_.pump_feedback_override != 0xFFFFU) return this->state_.pump_feedback_override;
    if (!this->state_.pump_request) return 20U;
    const long running_ipwm = std::clamp<long>(this->state_.pump_ipwm, 50L, 850L);
    return static_cast<uint16_t>(std::clamp<long>(50L + lroundf((850L - running_ipwm) * 0.875f), 50L, 750L));
  }

  const char* profile_model_text_() const {
    switch (this->state_.profile) {
      case Profile::V1:
        return "QUATT-V1-SIM";
      case Profile::V1_5:
        return "QUATT-V1.5-SIM";
      case Profile::V2_OLD:
        return "QUATT-V2-OLD-SIM";
      case Profile::V2_NEW:
        return "QUATT-V2-NEW-SIM";
      default:
        return "";
    }
  }
  const char* profile_customer_text_() const {
    return this->state_.profile == Profile::V2_OLD || this->state_.profile == Profile::V2_NEW ? "AMH6" : "";
  }
  static bool encode_ascii_word_(const char* text, size_t word_index, uint16_t& value) {
    const size_t byte_index = word_index * 2U;
    const size_t length = std::strlen(text);
    const uint8_t high = byte_index < length ? static_cast<uint8_t>(text[byte_index]) : 0U;
    const uint8_t low = byte_index + 1U < length ? static_cast<uint8_t>(text[byte_index + 1U]) : 0U;
    value = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low);
    return true;
  }
  static size_t actuator_diagnostic_index_(uint16_t address) {
    switch (address) {
      case 1999:
        return 0U;
      case 2006:
        return 1U;
      case 2010:
        return 2U;
      case 2015:
        return 3U;
      default:
        return 4U;
    }
  }

  OduState state_{};
  ModelSettings settings_{};
};

}  // namespace esphome::quatt_odu_simulator
