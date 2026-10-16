#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

static uint16_t read(QuattOduSimulatorModel& model, uint16_t address) {
  uint16_t value = 0;
  assert(model.read_register(address, value));
  return value;
}

static void start(QuattOduSimulatorModel& model, Profile profile, float outside, bool limits = true) {
  model.configure(profile);
  model.mutable_settings().heating_limits_enabled = limits;
  model.mutable_settings().compressor_start_delay_s = 0;
  model.mutable_settings().minimum_runtime_s = 0;
  model.mutable_state().outside_temperature_c = outside;
  model.mutable_state().water_in_temperature_c = 30;
  assert(model.write_register(3999, 2, 0));
  assert(model.write_register(1999, model.maximum_level(), 0));
  model.update(0.25f);
}

int main() {
  struct Case {
    Profile profile;
    std::array<uint8_t, 6> caps;
  };
  const Case cases[]{{Profile::V1, {10, 8, 5, 3, 2, 0}},
                     {Profile::V1_5, {10, 8, 5, 3, 2, 0}},
                     {Profile::V2_OLD, {7, 6, 5, 4, 3, 0}},
                     {Profile::V2_NEW, {14, 12, 10, 8, 6, 0}}};
  constexpr float centers[]{7, 16, 22, 26, 40, 50};

  for (const auto& test : cases) {
    QuattOduSimulatorModel model;
    model.configure(test.profile);
    assert(!model.settings().heating_limits_enabled && read(model, 3281) == 0);
    for (size_t zone = 0; zone < test.caps.size(); ++zone) assert(read(model, 3293 + zone) == test.caps[zone]);
    // These settings are profile readbacks, not newly supported Modbus writes.
    assert(!model.write_register(3281, 1, 0));
    assert(!model.write_register(3293, 1, 0));

    for (size_t zone = 0; zone < test.caps.size(); ++zone) {
      start(model, test.profile, centers[zone]);
      assert(read(model, 3281) == 1);
      assert(model.state().heating_limits_active);
      assert(read(model, 1999) == model.maximum_level());
      assert(model.state().prepared_physical_level == test.caps[zone]);
      assert(read(model, 2102) == model.state().heating_table[test.caps[zone]]);
      assert(model.state().heating_limit_reasons ==
             ((test.caps[zone] < model.maximum_level() ? HEATING_LIMIT_OUTSIDE : HEATING_LIMIT_NONE) |
              (test.profile == Profile::V2_NEW ? HEATING_LIMIT_HIGH_LEVEL_RELEASE : HEATING_LIMIT_NONE)));

      // Switch-off disables the policy and its enable readback immediately at
      // the next model update. The reference caps remain readable.
      model.mutable_settings().heating_limits_enabled = false;
      model.update(0.25f);
      assert(read(model, 3281) == 0 && !model.state().heating_limits_active);
      assert(read(model, 2102) == model.state().heating_table[model.maximum_level()]);
      assert(read(model, 3293 + zone) == test.caps[zone]);
    }

    // A cap selects a stand in the current table, never a hard-coded Hz value.
    start(model, test.profile, 16);
    const uint8_t level = test.caps[1];
    const uint16_t address = level <= 10 ? 3011 + level : 3050 + level - 11;
    const uint16_t frequency = read(model, 2102);
    assert(model.write_register(address, frequency - 1, 0));
    model.update(0.25f);
    assert(read(model, 2102) == frequency - 1);
    assert(read(model, 3294) == level);
    model.restore_factory_tables();
    model.update(0.25f);
    assert(read(model, 2102) == frequency);
    // Even below the cap, a request keeps its own lower physical stand.
    assert(model.write_register(1999, 1, 0));
    model.update(0.25f);
    assert(model.state().prepared_physical_level == 1 && model.state().heating_limit_reasons == 0);
    assert(model.write_register(1999, 0, 0));
    model.update(0.25f);
    assert(read(model, 2102) == 0 && !model.state().heating_limits_active);
    // Configured enable remains true during standby, cooling and defrost.
    assert(read(model, 3281) == 1);
    start(model, test.profile, 16);
    assert(model.write_register(3999, 0, 0));
    model.update(0.25f);
    assert(read(model, 3281) == 1 && !model.state().heating_limits_active);
    start(model, test.profile, 16);
    model.set_defrost(true);
    model.update(0.25f);
    assert(read(model, 3281) == 1 && !model.state().heating_limits_active);
    assert(read(model, 2102) == model.state().heating_table[model.maximum_level()]);
    model.set_defrost(false);
    model.update(0.25f);
    assert(model.state().prepared_physical_level == test.caps[1]);
    model.configure(test.profile);
    assert(model.write_register(3999, 1, 0));
    assert(model.write_register(1999, model.maximum_level(), 0));
    model.update(0.25f);
    assert(read(model, 3281) == 1 && !model.state().heating_limits_active);
    assert(read(model, 2102) == model.state().cooling_table[model.maximum_level()]);

    // A profile change replaces both the policy and readbacks, including
    // previous zone history. The lab switch keeps its current setting.
    model.configure(test.profile);
    assert(read(model, 3281) == 1);
    model.mutable_state().outside_temperature_c = 12.01f;
    assert(model.write_register(3999, 2, 0));
    assert(model.write_register(1999, model.maximum_level(), 0));
    model.update(0.25f);
    assert(model.state().prepared_physical_level == test.caps[1]);

    // Both edges of every warm band, including warm shutdown/restart. Starting
    // a new policy avoids inheriting a previous scenario's hysteresis.
    for (size_t boundary = 0; boundary < 5; ++boundary) {
      constexpr float thresholds[]{12, 20, 24, 28, 46};
      const float t = thresholds[boundary];
      HeatingLimits rising(test.profile);
      rising.apply(20, centers[boundary], 30, false);
      rising.apply(20, t + 1, 30, false);
      assert(rising.outside_level_limit() == test.caps[boundary]);
      rising.apply(20, t + 1.01f, 30, false);
      assert(rising.outside_level_limit() == test.caps[boundary + 1]);
      HeatingLimits falling(test.profile);
      falling.apply(20, centers[boundary + 1], 30, false);
      falling.apply(20, t - 1, 30, false);
      assert(falling.outside_level_limit() == test.caps[boundary + 1]);
      falling.apply(20, t - 1.01f, 30, false);
      assert(falling.outside_level_limit() == test.caps[boundary]);
    }

    // A performance-domain edge is not a physical compressor prohibition.
    start(model, test.profile, 12.01f);
    assert(read(model, 2102) > 0);
    assert(model.state().prepared_physical_level == test.caps[1]);
    start(model, test.profile, 21);
    assert(read(model, 2102) > 0);
    assert(model.state().prepared_physical_level == test.caps[2]);
    const float target = model.state().target_frequency_hz;
    model.mutable_state().water_in_temperature_c = 80;
    model.update(0.25f);
    assert(model.state().target_frequency_hz == target);  // no CiC model-water ceiling added

    model.mutable_state().outside_temperature_c = std::numeric_limits<float>::quiet_NaN();
    model.update(0.25f);
    assert(read(model, 2102) == 0 && model.state().heating_limit_reasons == HEATING_LIMIT_INVALID_TEMPERATURE);
  }

  // The V2 new cold/high-level/quiet rules must not leak into other profiles.
  for (const auto profile : {Profile::V1, Profile::V1_5, Profile::V2_OLD}) {
    QuattOduSimulatorModel model;
    start(model, profile, 7);
    assert(model.write_register(2006, 1, 0));
    model.mutable_state().water_in_temperature_c = std::numeric_limits<float>::quiet_NaN();
    model.update(0.25f);
    assert(model.state().heating_limit_reasons != HEATING_LIMIT_INVALID_TEMPERATURE);
    assert(!model.state().high_levels_released);
    assert(read(model, 2102) > 0);
    model.configure(profile);
    HeatingLimits limits(profile);
    limits.apply(20, -30, 30, false);
    assert(limits.outside_level_limit() == profile_definition(profile).heating_outside_levels[0]);
  }
  QuattOduSimulatorModel one, two;
  start(one, Profile::V1, 7);
  start(two, Profile::V2_OLD, 16);
  one.mutable_state().outside_temperature_c = 12.5f;
  one.update(0.25f);
  assert(one.state().prepared_physical_level == 10);  // retains the cool band's ceiling
  assert(two.state().prepared_physical_level == 6);
  assert(read(one, 3293) == 10 && read(two, 3293) == 7);
  assert(read(one, 3281) == 1 && read(two, 3281) == 1);
}
