#include <array>
#include <cassert>
#include <cstdint>
#include <set>

#include "../components/quatt_odu_simulator/quatt_odu_profiles.generated.h"
#include "../components/quatt_odu_simulator/quatt_odu_registers.generated.h"

using namespace esphome::quatt_odu_simulator;

static bool covered(uint16_t address) {
  for (const auto& descriptor : REGISTER_DESCRIPTORS)
    if (descriptor.address == address) return true;
  for (const auto& descriptor : REGISTER_RANGE_DESCRIPTORS)
    if (address >= descriptor.start && address <= descriptor.end) return true;
  return false;
}

int main() {
  std::set<uint16_t> exact;
  for (const auto& descriptor : REGISTER_DESCRIPTORS) {
    assert(descriptor.width_words > 0U);
    assert(descriptor.scale > 0.0f);
    assert(exact.insert(descriptor.address).second);
  }
  for (uint16_t address = 2099U; address <= 2138U; address++) assert(exact.count(address) == 1U);
  for (uint16_t address = 2999U; address <= 3510U; address++) assert(covered(address));
  for (uint16_t address = 11004U; address <= 11009U; address++) assert(covered(address));
  for (uint16_t address = 11120U; address <= 11139U; address++) assert(covered(address));
  for (uint16_t address = 11160U; address <= 11179U; address++) assert(covered(address));
  for (uint16_t address = 11219U; address <= 11238U; address++) assert(covered(address));
  for (uint16_t address : {1999U, 2006U, 2010U, 2015U, 3999U}) assert(covered(address));

  for (const auto& descriptor : REGISTER_DESCRIPTORS) {
    const bool expected_write = descriptor.address == 1999U || descriptor.address == 2006U ||
                                descriptor.address == 2010U || descriptor.address == 2015U ||
                                descriptor.address == 3999U;
    assert(access_writable(descriptor.access) == expected_write);
  }

  const auto& v1 = profile_definition(Profile::V1);
  const auto& v15 = profile_definition(Profile::V1_5);
  const auto& v2_old = profile_definition(Profile::V2_OLD);
  const auto& v2_new = profile_definition(Profile::V2_NEW);
  assert(v1.max_physical_level == 10U);
  assert(v15.max_physical_level == 10U);
  assert(v2_old.max_physical_level == 10U);
  assert(v2_new.max_physical_level == 20U);
  assert(v2_new.factory_heating[20] == 110U);
  assert(v2_new.factory_cooling[20] == 71U);
  assert((v1.factory_heating == std::array<uint8_t, 21>{0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90}));
  assert((v15.factory_cooling == std::array<uint8_t, 21>{0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74}));
  assert((v2_old.factory_heating == std::array<uint8_t, 21>{0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90}));
  assert((v2_new.factory_heating == std::array<uint8_t, 21>{0,  20, 26, 30, 36, 40, 45, 48, 52,  55, 60,
                                                            65, 68, 72, 76, 82, 85, 90, 95, 102, 110}));

  const auto* v1_runtime = runtime_modified_preset(Profile::V1);
  const auto* v15_runtime = runtime_modified_preset(Profile::V1_5);
  assert(v1_runtime != nullptr && v15_runtime != nullptr);
  assert((v1_runtime->cooling == std::array<uint8_t, 21>{0, 20, 22, 24, 26, 28, 30, 30, 30, 30, 30}));
  assert((v15_runtime->cooling == std::array<uint8_t, 21>{0, 26, 28, 30, 32, 34, 36, 38, 40, 71, 74}));
  assert(runtime_modified_preset(Profile::V2_NEW) == nullptr);

  const auto extension = REGISTER_RANGE_DESCRIPTORS[2];
  assert(extension.start == 3050U && extension.end == 3069U);
  assert(extension.profile_mask == (1U << static_cast<uint8_t>(Profile::V2_NEW)));
  return 0;
}
