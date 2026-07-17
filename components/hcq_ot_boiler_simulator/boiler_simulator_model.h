#pragma once

#include <algorithm>
#include <cmath>

namespace hcq::ot_sim {

enum class Mode {
  IDLE,
  IGNITION,
  CENTRAL_HEATING,
  DOMESTIC_HOT_WATER,
  FAULT,
};

struct Inputs {
  bool automatic = true;
  bool master_status_valid = false;
  bool master_ch_enable = false;
  bool master_dhw_enable = false;
  bool dhw_demand = false;
  bool force_fault = false;
  bool force_diagnostic = false;
  bool manual_ch_active = false;
  bool manual_dhw_active = false;
  bool manual_flame_on = false;
  float t_set_c = 0.0f;
  float ambient_c = 20.0f;
  float dhw_target_c = 55.0f;
  float return_delta_c = 8.0f;
  float minimum_t_set_c = 10.0f;
  float ignition_delay_s = 3.0f;
  float heat_rate_c_per_s = 0.35f;
  float cool_rate_c_per_s = 0.08f;
  float minimum_modulation_pct = 20.0f;
};

struct State {
  Mode mode = Mode::IDLE;
  bool ch_active = false;
  bool dhw_active = false;
  bool flame_on = false;
  bool fault = false;
  bool diagnostic = false;
  float boiler_temperature_c = 20.0f;
  float return_temperature_c = 20.0f;
  float relative_modulation_pct = 0.0f;
};

class BoilerSimulatorModel {
 public:
  explicit BoilerSimulatorModel(float initial_temperature_c = 20.0f) {
    reset(initial_temperature_c);
  }

  void reset(float initial_temperature_c = 20.0f) {
    state_ = State{};
    const float initial = finite_or_(initial_temperature_c, 20.0f);
    state_.boiler_temperature_c = initial;
    state_.return_temperature_c = initial;
    pending_mode_ = Mode::IDLE;
    ignition_elapsed_s_ = 0.0f;
  }

  const State &step(const Inputs &inputs, float delta_s) {
    const float dt = std::clamp(finite_or_(delta_s, 0.0f), 0.0f, 5.0f);
    const float ambient = finite_or_(inputs.ambient_c, 20.0f);
    const float minimum_t_set = std::max(0.0f, finite_or_(inputs.minimum_t_set_c, 10.0f));
    const float t_set = finite_or_(inputs.t_set_c, 0.0f);

    state_.fault = inputs.force_fault;
    state_.diagnostic = inputs.force_diagnostic || state_.fault;

    if (state_.fault) {
      set_mode_(Mode::FAULT);
    } else if (!inputs.automatic) {
      ignition_elapsed_s_ = 0.0f;
      pending_mode_ = Mode::IDLE;
      state_.mode = inputs.manual_dhw_active
                        ? Mode::DOMESTIC_HOT_WATER
                        : (inputs.manual_ch_active ? Mode::CENTRAL_HEATING : Mode::IDLE);
      state_.ch_active = inputs.manual_ch_active;
      state_.dhw_active = inputs.manual_dhw_active;
      state_.flame_on = inputs.manual_flame_on;
    } else {
      const bool status_valid = inputs.master_status_valid;
      const bool dhw_requested =
          status_valid && inputs.master_dhw_enable && inputs.dhw_demand;
      const bool ch_requested =
          status_valid && inputs.master_ch_enable && t_set >= minimum_t_set;
      const Mode requested_mode = dhw_requested
                                      ? Mode::DOMESTIC_HOT_WATER
                                      : (ch_requested ? Mode::CENTRAL_HEATING : Mode::IDLE);
      update_automatic_mode_(requested_mode, inputs.ignition_delay_s, dt);
    }

    update_temperatures_(inputs, ambient, t_set, dt);
    return state_;
  }

  const State &state() const { return state_; }

  static const char *mode_name(Mode mode) {
    switch (mode) {
      case Mode::IGNITION:
        return "Ontsteken";
      case Mode::CENTRAL_HEATING:
        return "CV actief";
      case Mode::DOMESTIC_HOT_WATER:
        return "Tapwater actief";
      case Mode::FAULT:
        return "Storing";
      case Mode::IDLE:
      default:
        return "Uit";
    }
  }

 private:
  static float finite_or_(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
  }

  static float approach_(float current, float target, float max_step) {
    if (current < target) {
      return std::min(target, current + max_step);
    }
    return std::max(target, current - max_step);
  }

  void set_mode_(Mode mode) {
    state_.mode = mode;
    state_.ch_active = mode == Mode::CENTRAL_HEATING;
    state_.dhw_active = mode == Mode::DOMESTIC_HOT_WATER;
    state_.flame_on = state_.ch_active || state_.dhw_active;
    if (mode == Mode::IDLE || mode == Mode::FAULT) {
      ignition_elapsed_s_ = 0.0f;
      pending_mode_ = Mode::IDLE;
    }
  }

  void update_automatic_mode_(Mode requested_mode, float ignition_delay_s, float delta_s) {
    if (requested_mode == Mode::IDLE) {
      set_mode_(Mode::IDLE);
      return;
    }

    if (state_.mode == requested_mode) {
      set_mode_(requested_mode);
      return;
    }

    // A burning combi boiler can switch between CH and DHW without a new
    // ignition sequence; DHW keeps priority through requested_mode above.
    if ((state_.mode == Mode::CENTRAL_HEATING || state_.mode == Mode::DOMESTIC_HOT_WATER) &&
        (requested_mode == Mode::CENTRAL_HEATING || requested_mode == Mode::DOMESTIC_HOT_WATER)) {
      set_mode_(requested_mode);
      return;
    }

    if (state_.mode != Mode::IGNITION || pending_mode_ != requested_mode) {
      state_.mode = Mode::IGNITION;
      state_.ch_active = false;
      state_.dhw_active = false;
      state_.flame_on = false;
      pending_mode_ = requested_mode;
      ignition_elapsed_s_ = 0.0f;
    }

    ignition_elapsed_s_ += delta_s;
    const float delay = std::max(0.0f, finite_or_(ignition_delay_s, 3.0f));
    if (ignition_elapsed_s_ >= delay) {
      set_mode_(pending_mode_);
    }
  }

  void update_temperatures_(const Inputs &inputs, float ambient, float t_set, float delta_s) {
    const float dhw_target = finite_or_(inputs.dhw_target_c, 55.0f);
    const float target = state_.dhw_active
                             ? dhw_target
                             : (state_.flame_on ? std::max(ambient, t_set) : ambient);
    const float rate = target > state_.boiler_temperature_c
                           ? std::max(0.0f, finite_or_(inputs.heat_rate_c_per_s, 0.35f))
                           : std::max(0.0f, finite_or_(inputs.cool_rate_c_per_s, 0.08f));
    state_.boiler_temperature_c =
        approach_(state_.boiler_temperature_c, target, rate * delta_s);

    const float return_delta =
        std::max(0.0f, finite_or_(inputs.return_delta_c, 8.0f));
    const float return_target = state_.flame_on
                                    ? std::max(ambient, state_.boiler_temperature_c - return_delta)
                                    : state_.boiler_temperature_c;
    state_.return_temperature_c = approach_(
        state_.return_temperature_c, return_target, std::max(rate, 0.1f) * delta_s);

    if (!state_.flame_on) {
      state_.relative_modulation_pct = 0.0f;
      return;
    }

    const float error = std::max(0.0f, target - state_.boiler_temperature_c);
    const float minimum_modulation = std::clamp(
        finite_or_(inputs.minimum_modulation_pct, 20.0f), 0.0f, 100.0f);
    state_.relative_modulation_pct =
        std::clamp(minimum_modulation + error * 3.0f, minimum_modulation, 100.0f);
  }

  State state_{};
  Mode pending_mode_ = Mode::IDLE;
  float ignition_elapsed_s_ = 0.0f;
};

}  // namespace hcq::ot_sim
