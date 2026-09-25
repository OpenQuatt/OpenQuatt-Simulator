#include <cassert>
#include <cmath>
#include <cstdint>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

static uint16_t read(QuattOduSimulatorModel& model, uint16_t address) {
  uint16_t value = 0U;
  assert(model.read_register(address, value));
  return value;
}

int main() {
  QuattOduSimulatorModel model;
  model.configure(Profile::V2_NEW);
  assert(read(model, 3000U) == 0U);
  assert(read(model, 3011U) == 0U);
  assert(read(model, 3059U) == 110U);
  assert(read(model, 3069U) == 71U);
  assert(model.runtime_table_valid(WorkingMode::HEATING));
  assert(model.runtime_table_valid(WorkingMode::COOLING));

  assert(model.write_register(1999U, 25U, 10U));
  assert(model.state().requested_physical_level == 25U);
  assert(model.state().accepted_physical_level == 20U);
  assert(model.state().protocol.level_capability_violations == 1U);
  assert(model.write_register(3999U, 2U, 11U));
  assert(model.write_register(2010U, 4096U, 12U));
  assert(model.write_register(2015U, 150U, 13U));
  model.mutable_settings().compressor_start_delay_s = 0.0f;
  model.mutable_settings().minimum_runtime_s = 0.0f;
  model.mutable_settings().ramp_up_hz_s = 200.0f;
  for (int i = 0; i < 40; i++) model.update(0.25f);
  assert(model.state().target_frequency_hz == 110.0f);
  assert(model.state().measured_frequency_hz == 110.0f);
  assert(model.state().flow_lph > 750.0f);
  assert(model.state().flow_switch);
  assert(model.state().water_out_temperature_c > model.state().water_in_temperature_c);
  assert(read(model, 2138U) > 0U);
  const uint16_t high_pump_power = read(model, 2137U);
  assert(high_pump_power >= 50U && high_pump_power <= 750U);
  assert(model.write_register(2015U, 800U, 14U));
  for (int i = 0; i < 40; i++) model.update(0.25f);
  assert(model.state().flow_lph < 200.0f);
  assert(read(model, 2137U) < high_pump_power);

  assert((read(model, 2108U) & 0x0040U) == 0U);
  model.set_defrost(true);
  assert(read(model, 2118U) == 1U);
  assert((read(model, 2108U) & 0x0010U) != 0U);
  assert((read(model, 2108U) & 0x0040U) != 0U);
  assert(model.write_register(1999U, 5U, 20U));
  assert(model.state().accepted_physical_level == 20U);
  model.set_defrost(false);
  assert((read(model, 2108U) & 0x0040U) == 0U);
  assert(model.state().accepted_physical_level == 5U);

  model.set_defrost(true);
  assert(model.write_register(1999U, 0U, 21U));
  assert(model.state().accepted_physical_level == 5U);
  model.set_defrost(false);
  assert(model.state().accepted_physical_level == 0U);

  model.configure(Profile::V2_NEW);
  model.set_manual_working_mode_raw(2U);
  model.set_manual_compressor_frequency_raw(50U);
  model.set_manual_ac_voltage_raw(230U);
  model.set_manual_ac_current_raw(13U);
  model.set_manual_fan_speed_raw(200U);
  model.set_manual_operating_status_raw(0x0004U);
  model.set_manual_pump_feedback_raw(300U);
  model.set_manual_water_in_temperature_raw(5250U);
  model.set_manual_water_out_temperature_raw(5300U);
  model.set_manual_water_flow_raw(1650U);
  model.set_manual_telemetry_enabled(true);
  model.set_defrost(true);
  for (int i = 0; i < 8; i++) model.update(0.25f);
  assert(read(model, 2099U) == 2U);
  assert(read(model, 2103U) == 50U);
  assert(read(model, 2100U) == 230U);
  assert(read(model, 2101U) == 13U);
  assert(read(model, 2105U) == 200U);
  assert(read(model, 2108U) == 0x0004U);
  assert(read(model, 2118U) == 1U);
  assert(read(model, 2133U) == 5250U);
  assert(read(model, 2134U) == 5300U);
  assert(read(model, 2137U) == 300U);
  assert(read(model, 2138U) == 1650U);
  model.set_manual_telemetry_enabled(false);
  assert((read(model, 2108U) & 0x0010U) != 0U);

  assert(model.write_register(3001U, 25U, 30U));
  assert(read(model, 3001U) == 25U);
  assert(model.state().table_dirty);
  model.restore_factory_tables();
  assert(read(model, 3001U) == 20U);

  model.configure(Profile::V1);
  assert(model.apply_runtime_modified_preset());
  assert(read(model, 3001U) == 20U);
  assert(read(model, 3007U) == 30U);
  assert(read(model, 3050U) == 0xA501U);
  assert(!model.can_write_register(3050U, 10U));

  auto invalid = model.mutable_state().heating_table;
  invalid[3] = 10U;
  model.mutable_state().heating_table = invalid;
  assert(!model.runtime_table_valid(WorkingMode::HEATING));

  auto equal = profile_definition(Profile::V1).factory_heating;
  equal[4] = equal[3];
  assert(QuattOduSimulatorModel::valid_frequency_table(equal, 11U));
  equal[0] = 1U;
  assert(!QuattOduSimulatorModel::valid_frequency_table(equal, 11U));
  equal = profile_definition(Profile::V1).factory_heating;
  equal[10] = 121U;
  assert(!QuattOduSimulatorModel::valid_frequency_table(equal, 11U));

  model.configure(Profile::V1);
  assert(model.write_register(3999U, 1U, 40U));
  assert(model.write_register(1999U, 2U, 41U));
  model.mutable_settings().compressor_start_delay_s = 0.0f;
  model.mutable_settings().ramp_up_hz_s = 200.0f;
  model.update(0.25f);
  assert(model.state().active_mode == WorkingMode::COOLING);
  assert((read(model, 2108U) & 0x0040U) != 0U);
  assert(model.state().target_frequency_hz == 36.0f);
  assert(model.write_register(2010U, 0U, 42U));
  for (int i = 0; i < 40; i++) model.update(0.25f);
  assert(model.state().flow_lph < 10.0f);
  assert(!model.state().flow_switch);

  model.configure(Profile::V1_5);
  assert(model.write_register(2015U, 150U, 50U));
  model.mutable_state().force_flow_without_relay = true;
  for (int i = 0; i < 40; i++) model.update(0.25f);
  assert(!model.state().pump_request);
  assert(model.state().flow_lph > 750.0f);
  assert(model.state().flow_switch);

  model.configure(Profile::V1_5);
  assert(read(model, 3275U) == 0U);
  assert(read(model, 3270U) == 45U);
  assert(read(model, 3275U) == 0U);
  assert(model.write_register(3275U, 4U, 60U));
  assert(read(model, 3275U) == 4U);
  assert(!model.write_register(3275U, 2U, 61U));
  assert(read(model, 3270U) == 45U);
  assert(read(model, 3280U) == 61U);
  assert(read(model, 3307U) == 5U);
  assert(read(model, 3336U) == 27U);
  assert(read(model, 3414U) == 18U);
  assert(model.write_register(3999U, 4U, 63U));
  assert(read(model, 2099U) == 4U);
  assert(read(model, 2118U) == 1U);
  // Turning the injection switch off must NOT cancel a forced (3999=4) cycle.
  model.set_defrost(false);
  assert(read(model, 2099U) == 4U);
  assert(read(model, 2118U) == 1U);
  // A normal-mode write is the other way out of the forced cycle.
  assert(model.write_register(3999U, 2U, 64U));
  assert(read(model, 2099U) != 4U);
  assert(read(model, 2118U) == 0U);
  model.configure(Profile::V1);
  assert(!model.write_register(3275U, 4U, 62U));

  // A forced defrost cycle self-ends after its modelled duration and returns the
  // ODU to its requested working mode, without any switch or normal-mode write.
  model.configure(Profile::V1_5);
  model.mutable_settings().forced_defrost_duration_s = 5.0f;
  model.mutable_settings().compressor_start_delay_s = 0.0f;
  model.mutable_settings().minimum_runtime_s = 0.0f;
  assert(model.write_register(1999U, 20U, 100U));
  assert(model.write_register(3999U, 2U, 101U));   // request heating
  model.update(0.5f);
  const uint16_t settled_mode = read(model, 2099U);
  assert(settled_mode == 2U);
  assert(model.write_register(3999U, 4U, 110U));   // force defrost
  assert(read(model, 2099U) == 4U);
  assert(read(model, 2118U) == 1U);
  model.update(2.0f);
  assert(read(model, 2099U) == 4U);               // still inside the cycle
  assert(read(model, 2118U) == 1U);
  model.update(2.0f);
  assert(read(model, 2099U) == 4U);               // still inside the cycle
  assert(read(model, 2118U) == 1U);
  model.update(2.0f);                              // crosses the 5 s boundary
  assert(read(model, 2118U) == 0U);               // defrost bit cleared
  assert(read(model, 2099U) == settled_mode);     // back to heating on its own
  return 0;
}
