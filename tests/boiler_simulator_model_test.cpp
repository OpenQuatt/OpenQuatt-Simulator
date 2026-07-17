#include <cassert>
#include <cmath>

#include "../components/hcq_ot_boiler_simulator/boiler_simulator_model.h"

using hcq::ot_sim::BoilerSimulatorModel;
using hcq::ot_sim::Inputs;
using hcq::ot_sim::Mode;

int main() {
  BoilerSimulatorModel model;
  Inputs input;

  const auto &idle = model.step(input, 1.0f);
  assert(idle.mode == Mode::IDLE);
  assert(!idle.ch_active);
  assert(!idle.flame_on);

  input.master_status_valid = true;
  input.master_ch_enable = true;
  input.t_set_c = 50.0f;
  input.ignition_delay_s = 2.0f;
  const auto &ignition = model.step(input, 1.0f);
  assert(ignition.mode == Mode::IGNITION);
  assert(!ignition.ch_active);
  const auto &heating = model.step(input, 1.0f);
  assert(heating.mode == Mode::CENTRAL_HEATING);
  assert(heating.ch_active);
  assert(heating.flame_on);
  assert(heating.relative_modulation_pct >= input.minimum_modulation_pct);

  const float initial_boiler_temperature = heating.boiler_temperature_c;
  for (int i = 0; i < 10; i++) {
    model.step(input, 1.0f);
  }
  assert(model.state().boiler_temperature_c > initial_boiler_temperature);
  assert(model.state().return_temperature_c <= model.state().boiler_temperature_c);

  input.master_dhw_enable = true;
  input.dhw_demand = true;
  const auto &dhw = model.step(input, 1.0f);
  assert(dhw.mode == Mode::DOMESTIC_HOT_WATER);
  assert(dhw.dhw_active);
  assert(!dhw.ch_active);
  assert(dhw.flame_on);

  input.force_fault = true;
  const auto &fault = model.step(input, 1.0f);
  assert(fault.mode == Mode::FAULT);
  assert(fault.fault);
  assert(fault.diagnostic);
  assert(!fault.flame_on);

  input.force_fault = false;
  input.dhw_demand = false;
  input.master_status_valid = false;
  const auto &stale = model.step(input, 1.0f);
  assert(stale.mode == Mode::IDLE);
  assert(!stale.ch_active);

  input.automatic = false;
  input.manual_ch_active = true;
  input.manual_flame_on = false;
  const auto &manual = model.step(input, 1.0f);
  assert(manual.mode == Mode::CENTRAL_HEATING);
  assert(manual.ch_active);
  assert(!manual.flame_on);

  model.reset(35.0f);
  assert(std::fabs(model.state().boiler_temperature_c - 35.0f) < 0.001f);
  return 0;
}
