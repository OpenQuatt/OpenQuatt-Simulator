#include <cassert>
#include <cstdint>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

static uint16_t read(QuattOduSimulatorModel& model, uint16_t address) {
  uint16_t value = 0xFFFFU;
  assert(model.read_register(address, value));
  return value;
}

int main() {
  QuattOduSimulatorModel model;
  model.configure(Profile::V1);
  assert(read(model, 2114U) == 0U);
  assert(read(model, 2122U) == 0x0119U);
  assert(read(model, 2127U) == 0x0037U);
  assert(read(model, 11160U) == 0U && read(model, 11161U) == 0U);
  assert(detect_profile(0U, 0x0119U, 0x0037U) == Profile::V1);

  model.configure(Profile::V1_5);
  assert(read(model, 2114U) == 0U);
  assert(read(model, 2122U) == 0x011EU);
  assert(read(model, 2127U) == 0x0E37U);
  assert(detect_profile(0U, 0x011EU, 0x0E37U) == Profile::V1_5);

  model.configure(Profile::V2_OLD);
  assert(read(model, 2114U) == 2825U);
  assert(read(model, 2122U) == 0x0122U);
  assert(read(model, 2127U) == 0x0E37U);
  assert(read(model, 11160U) == 0x414DU && read(model, 11161U) == 0x4836U);
  // Detection is based on the 14-register core identity block, so both an
  // empty and AMH6 customer-model response identify the old V2 profile.
  assert(detect_profile(2825U, 0x0122U, 0x0E37U) == Profile::V2_OLD);

  model.configure(Profile::V2_NEW);
  assert(read(model, 2114U) == 2825U);
  assert(read(model, 2122U) == 0x0201U);
  assert(read(model, 2127U) == 0x1037U);
  assert(detect_profile(2825U, 0x0201U, 0x1037U) == Profile::V2_NEW);
  assert(detect_profile(2825U, 0x0201U, 0x0037U) == Profile::DISABLED);
  return 0;
}
