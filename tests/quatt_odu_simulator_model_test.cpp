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
  assert(model.write_register(2015U, 800U, 13U));
  model.mutable_settings().compressor_start_delay_s = 0.0f;
  model.mutable_settings().minimum_runtime_s = 0.0f;
  model.mutable_settings().ramp_up_hz_s = 200.0f;
  for (int i = 0; i < 20; i++) model.update(0.25f);
  assert(model.state().target_frequency_hz == 110.0f);
  assert(model.state().measured_frequency_hz == 110.0f);
  assert(model.state().flow_lph > 600.0f);
  assert(model.state().flow_switch);
  assert(model.state().water_out_temperature_c > model.state().water_in_temperature_c);
  assert(read(model, 2138U) > 0U);
  assert(read(model, 2137U) >= 50U && read(model, 2137U) <= 750U);

  model.set_defrost(true);
  assert(read(model, 2118U) == 1U);
  assert((read(model, 2108U) & 0x0010U) != 0U);
  assert(model.write_register(1999U, 5U, 20U));
  assert(model.state().accepted_physical_level == 20U);
  model.set_defrost(false);
  assert(model.state().accepted_physical_level == 5U);

  model.set_defrost(true);
  assert(model.write_register(1999U, 0U, 21U));
  assert(model.state().accepted_physical_level == 5U);
  model.set_defrost(false);
  assert(model.state().accepted_physical_level == 0U);

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
  assert(model.state().target_frequency_hz == 36.0f);
  assert(model.write_register(2010U, 0U, 42U));
  for (int i = 0; i < 20; i++) model.update(0.25f);
  assert(model.state().flow_lph < 10.0f);
  assert(!model.state().flow_switch);
  return 0;
}
