#include <cassert>
#include <limits>
#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"
using namespace esphome::quatt_odu_simulator;

static uint16_t read(QuattOduSimulatorModel &model, uint16_t address) {
  uint16_t value = 0;
  assert(model.read_register(address, value));
  return value;
}
static void heat(QuattOduSimulatorModel &model, Profile profile = Profile::V1_5) {
  model.configure(profile);
  model.mutable_settings().forced_defrost_duration_s = 30;
  assert(model.write_register(1999, 5, 0));
  assert(model.write_register(3999, 2, 0));
  assert(model.write_register(2010, 4096, 0));
  assert(model.write_register(2015, 150, 0));
  for (int i = 0; i < 60; ++i) model.update(0.5f);
  assert(read(model, 2099) == 2);
}
int main() {
  QuattOduSimulatorModel model;
  model.configure(Profile::V1_5);
  assert(!model.write_register(3999, 4, 0)); // idle cannot fabricate a cycle
  for (auto profile : {Profile::V1, Profile::V1_5, Profile::V2_OLD, Profile::V2_NEW}) {
    heat(model, profile);
    assert(model.write_register(3999, 4, 1));
    assert(read(model, 2099) == 4 && read(model, 2118) == 1);
    assert((read(model, 2108) & 0x0054) == 0x0054);
    model.set_defrost(false); // independent injection cannot cancel the cycle
    for (int i = 0; i < 29; ++i) model.update(1);
    const float remaining = model.state().forced_defrost_remaining_s;
    assert(model.write_register(3999, 4, 2)); // lost-ACK retry cannot extend/restart
    assert(model.state().forced_defrost_remaining_s == remaining);
    assert(model.state().defrost_started == 1);
    model.reset_diagnostics(); // generic diagnostics reset cannot erase cycle history
    model.update(1);
    assert(read(model, 2099) == 2 && read(model, 2118) == 0);
    assert((read(model, 2108) & 0x0054) == 0);
    assert(model.state().defrost_started == 1 && model.state().defrost_completed == 1);
    assert(model.state().defrost_aborted == 0);
    model.update(1);
    assert(model.state().defrost_completed == 1);
  }
  heat(model);
  model.mutable_state().flow_lph = 249;
  assert(!model.write_register(3999, 4, 0));
  model.mutable_state().flow_lph = 250;
  assert(model.write_register(3999, 4, 0));
  assert(model.write_register(1999, 0, 1));
  assert(model.state().defrost_aborted == 1 && model.state().accepted_physical_level == 0);
  assert(read(model, 2118) == 0);
  for (int i = 0; i < 30; ++i) model.update(1);
  assert(read(model, 2099) == 0 && read(model, 2103) == 0);
  assert(model.state().defrost_completed == 0);
  for (uint16_t mode : {0, 1, 2}) {
    heat(model);
    assert(model.write_register(3999, 4, 0));
    assert(model.write_register(3999, mode, 1));
    assert(model.state().defrost_aborted == 1 && model.state().defrost_completed == 0);
    assert(!model.state().forced_defrost && model.state().forced_defrost_remaining_s == 0);
  }
  for (int fault = 0; fault < 7; ++fault) {
    heat(model);
    auto invalidate = [&]() {
      if (fault < 3) model.mutable_state().fault_words[fault] = 1;
      if (fault == 3) model.set_manual_telemetry_enabled(true);
      if (fault == 4) model.mutable_state().flow_switch = false;
      if (fault == 5) model.mutable_settings().forced_defrost_duration_s = 0;
      if (fault == 6) model.mutable_settings().forced_defrost_duration_s = std::numeric_limits<float>::quiet_NaN();
    };
    invalidate();
    assert(!model.write_register(3999, 4, 0));
    assert(model.state().defrost_started == 0);
    heat(model);
    model.mutable_settings().forced_defrost_duration_s = 30;
    assert(model.write_register(3999, 4, 0));
    invalidate();
    model.update(1);
    assert(model.state().defrost_aborted == 1 && model.state().defrost_completed == 0);
  }
  heat(model);
  assert(model.write_register(3999, 4, 0));
  model.mutable_state().outside_temperature_c = -3;
  for (int i = 0; i < 30; ++i) model.update(1);
  assert((read(model, 2108) & 0x0004) != 0); // ordinary ambient heater remains active
  model.configure(Profile::V1_5); // reconfiguration starts a new history; never resume an active cycle
  assert(model.state().defrost_started == 0 && !model.state().forced_defrost);
}
