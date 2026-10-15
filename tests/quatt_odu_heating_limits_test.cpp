#include <cassert>
#include <cstring>
#include <limits>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

static uint16_t read(QuattOduSimulatorModel& model, uint16_t address) {
  uint16_t value = 0U;
  assert(model.read_register(address, value));
  return value;
}

static void heat(QuattOduSimulatorModel& model, Profile profile, uint16_t level, float outside, float inlet,
                 bool limits = true) {
  model.configure(profile);
  model.mutable_settings().heating_limits_enabled = limits;
  model.mutable_settings().compressor_start_delay_s = 0.0f;
  model.mutable_settings().minimum_runtime_s = 0.0f;
  model.mutable_state().outside_temperature_c = outside;
  model.mutable_state().water_in_temperature_c = inlet;
  assert(model.write_register(1999U, level, 0U));
  assert(model.write_register(3999U, 2U, 0U));
}

static void check_band_hysteresis() {
  // Hold at each edge of the previous band's window, then cross it. Separate
  // instances exercise both directions without an intervening band change.
  constexpr float centers[]{-30, -10, 0, 4, 10, 16, 22, 26, 40, 50};
  constexpr uint8_t levels[]{0, 20, 18, 16, 14, 12, 10, 8, 6, 0};
  constexpr float lower[]{-100, -25, -5, 0, 5, 11, 19, 23, 27, 45};
  constexpr float upper[]{-20, -3, 2, 7, 13, 21, 25, 29, 47, 100};
  const auto outside_limit = [](HeatingLimits& limits, float temperature) {
    limits.apply(20, temperature, 33, false);
    return limits.outside_level_limit();
  };
  for (size_t band = 0; band < 10U; band++) {
    HeatingLimits rising;
    assert(outside_limit(rising, centers[band]) == levels[band]);
    if (band < 9U) {
      assert(outside_limit(rising, upper[band]) == levels[band]);
      assert(outside_limit(rising, upper[band] + 0.01f) == levels[band + 1U]);
    }
    HeatingLimits falling;
    assert(outside_limit(falling, centers[band]) == levels[band]);
    if (band > 0U) {
      if (band == 1U) {
        assert(outside_limit(falling, -24.99f) == 20U);
        assert(outside_limit(falling, -25.0f) == 0U);
      } else {
        assert(outside_limit(falling, lower[band]) == levels[band]);
        assert(outside_limit(falling, lower[band] - 0.01f) == levels[band - 1U]);
      }
    }
  }
}

int main() {
  QuattOduSimulatorModel model;
  struct Sample {
    float outside;
    float inlet;
    uint16_t requested;
    uint16_t frequency;
    uint8_t level;
  };
  const Sample samples[]{{7, 30, 20, 76, 14},   {20, 30, 17, 68, 12}, {-10, 33, 20, 110, 20},
                         {-10, 36, 20, 95, 18}, {-25, 30, 20, 0, 0},  {47, 30, 20, 0, 0}};
  for (const auto& sample : samples) {
    heat(model, Profile::V2_NEW, sample.requested, sample.outside, sample.inlet);
    model.update(0.25f);
    assert(model.state().heating_limits_active);
    assert(read(model, 1999U) == sample.requested);  // received level is not rewritten by the limiter
    assert(read(model, 2102U) == sample.frequency);
    assert(model.state().driver_frequency_hz == sample.frequency);
    assert(model.state().prepared_physical_level == sample.level);
  }

  // Default contract and other variants retain table-based behavior even
  // if the switch is enabled. Cooling is not given unverified heating limits.
  for (const auto profile : {Profile::V1, Profile::V1_5, Profile::V2_OLD, Profile::V2_NEW}) {
    heat(model, profile, 10, 7, 30, profile != Profile::V2_NEW);
    assert(model.write_register(2006U, 1U, 0U));
    model.update(0.25f);
    assert(read(model, 2102U) == profile_definition(profile).factory_heating[10]);
    assert(!model.state().heating_limits_active);
    assert(model.state().heating_limit_reasons == HEATING_LIMIT_NONE);
  }
  heat(model, Profile::V2_NEW, 20, 7, 30, false);
  model.mutable_settings().demand_limiter = 0.5f;
  model.update(0.25f);
  assert(read(model, 2102U) == 55U);
  model.mutable_settings().demand_limiter = 1.0f;

  heat(model, Profile::V2_NEW, 20, 7, 30);
  model.mutable_settings().compressor_start_delay_s = 2.0f;
  model.update(0.25f);
  assert(model.state().requested_table_frequency_hz == 110.0f);
  assert(read(model, 2102U) == 0U && !model.state().heating_limits_active);
  model.update(2.0f);
  assert(read(model, 2102U) == 76U && model.state().heating_limits_active);
  model.mutable_settings().compressor_start_delay_s = 0.0f;
  heat(model, Profile::V2_NEW, 20, 7, 30);
  model.mutable_settings().freeze_measured_frequency = true;
  model.update(0.25f);
  assert(read(model, 2102U) == 76U && read(model, 2103U) == 0U);
  model.mutable_settings().freeze_measured_frequency = false;
  heat(model, Profile::V2_NEW, 20, 7, 30);
  assert(model.write_register(3999U, 1U, 0U));
  assert(model.write_register(2006U, 1U, 0U));
  model.update(0.25f);
  assert(read(model, 2102U) == 71U && !model.state().heating_limits_active);

  // Quiet mode round-trips and limits by physical level, then uses the current
  // RAM table rather than a fixed Hz ceiling.
  heat(model, Profile::V2_NEW, 20, -10, 33);
  assert(model.write_register(2006U, 1U, 0U));
  model.update(0.25f);
  assert(read(model, 2006U) == 1U && read(model, 2102U) == 76U);
  assert(model.state().heating_limit_reasons == HEATING_LIMIT_QUIET);
  assert(model.write_register(3053U, 75U, 0U));  // heating level 14
  model.update(0.25f);
  assert(read(model, 2102U) == 75U);
  model.restore_factory_tables();
  model.update(0.25f);
  assert(read(model, 2102U) == 76U);
  assert(model.write_register(2006U, 0U, 0U));
  model.update(0.25f);
  assert(read(model, 2102U) == 110U);

  // The extra-high-level release has independent outside/inlet hysteresis.
  HeatingLimits release;
  assert(release.apply(20, -12.01f, 33, false).physical_level == 18U);
  assert(release.apply(20, -12, 34, false).physical_level == 20U);
  assert(release.apply(20, -13, 35, false).physical_level == 20U);
  assert(release.high_levels_released());
  assert(release.apply(20, -13.01f, 33, false).physical_level == 18U);
  assert(!release.high_levels_released());
  release.apply(20, -1, 34, false);
  assert(release.high_levels_released());
  release.apply(20, 0, 35, false);
  assert(release.high_levels_released());
  release.apply(20, 0.01f, 33, false);
  assert(!release.high_levels_released());
  release.apply(20, -10, 34.01f, false);
  assert(!release.high_levels_released());
  release.apply(20, -10, 34, false);
  assert(release.high_levels_released());
  release.apply(20, -10, 35.01f, false);
  assert(!release.high_levels_released());
  check_band_hysteresis();

  // All applicable reasons are reported; requests below a cap remain unchanged.
  HeatingLimits reasons;
  const auto combined = reasons.apply(20, 7, 36, true);
  assert(combined.physical_level == 14U && combined.reasons == 7U);
  assert(std::strstr(heating_limit_reason_text(combined.reasons), "quiet") != nullptr);
  assert(reasons.apply(5, 7, 30, true).reasons == HEATING_LIMIT_NONE);

  // Prepared demand, downstream lab command and feedback are distinct.
  heat(model, Profile::V2_NEW, 20, 7, 30);
  model.mutable_settings().demand_limiter = 0.5f;
  model.update(0.25f);
  assert(model.state().requested_table_frequency_hz == 110.0f);
  assert(read(model, 2102U) == 76U);
  assert(model.state().driver_frequency_hz == 38.0f);
  assert(read(model, 2103U) == 3U);  // existing 12 Hz/s ramp over 250 ms
  model.mutable_settings().demand_limiter = 1.0f;

  // The two ODU policies carry separate history. Disable, standby and profile
  // changes reset it; normal heating limits do not take over defrost injection.
  QuattOduSimulatorModel other;
  heat(model, Profile::V2_NEW, 20, -10, 33);
  heat(other, Profile::V2_NEW, 20, -13, 33);
  model.update(0.25f);
  other.update(0.25f);
  assert(model.state().high_levels_released && !other.state().high_levels_released);
  model.mutable_state().outside_temperature_c = -13;
  model.update(0.25f);
  assert(model.state().high_levels_released);
  model.mutable_settings().heating_limits_enabled = false;
  model.update(0.25f);
  assert(!model.state().high_levels_released && !model.state().heating_limits_active);
  model.mutable_settings().heating_limits_enabled = true;
  model.update(0.25f);
  assert(read(model, 2102U) == 95U);
  model.mutable_state().outside_temperature_c = -10;
  model.update(0.25f);
  assert(model.state().high_levels_released);
  assert(model.write_register(3999U, 0U, 0U));
  model.update(0.25f);
  assert(!model.state().high_levels_released);
  model.configure(Profile::V2_NEW);
  assert(!model.state().high_levels_released && model.state().prepared_physical_level == 0U);
  heat(model, Profile::V2_NEW, 20, 7, 30);
  model.set_defrost(true);
  model.update(0.25f);
  assert(!model.state().heating_limits_active && read(model, 2102U) == 110U);
  model.set_defrost(false);
  model.update(0.25f);
  assert(model.state().heating_limits_active && read(model, 2102U) == 76U);

  assert(model.write_register(2010U, 4096U, 0U));
  for (int i = 0; i < 40; i++) model.update(0.25f);
  assert(model.write_register(3999U, 4U, 0U));
  model.update(0.25f);
  assert(read(model, 2099U) == 4U && !model.state().heating_limits_active);
  assert(read(model, 2102U) == 110U);
  assert(model.write_register(3999U, 2U, 0U));
  model.update(0.25f);
  assert(model.state().heating_limits_active && read(model, 2102U) == 76U);

  // Manual telemetry continues to override readbacks, without rewriting policy.
  model.set_manual_telemetry_enabled(true);
  model.set_manual_compressor_frequency_raw(50U);
  model.update(0.25f);
  assert(read(model, 2103U) == 50U && read(model, 2102U) == 76U);

  // Invalid temperatures are a visible lab condition; invalid tables still
  // prevent a compressor target. F0 cannot acquire a nonzero target.
  model.mutable_state().outside_temperature_c = std::numeric_limits<float>::quiet_NaN();
  model.update(0.25f);
  assert(read(model, 2102U) == 0U);
  assert(model.state().heating_limit_reasons == HEATING_LIMIT_INVALID_TEMPERATURE);
  model.mutable_state().outside_temperature_c = 7;
  model.mutable_state().water_in_temperature_c = std::numeric_limits<float>::infinity();
  model.update(0.25f);
  assert(read(model, 2102U) == 0U);
  heat(model, Profile::V2_NEW, 20, 7, 30);
  model.mutable_state().heating_table[3] = 1U;
  model.update(0.25f);
  assert(read(model, 2102U) == 0U);
  model.restore_factory_tables();
  assert(model.write_register(1999U, 0U, 0U));
  model.update(0.25f);
  assert(read(model, 2102U) == 0U && model.state().prepared_physical_level == 0U);
  assert(!model.state().heating_limits_active && !model.state().high_levels_released);
}
