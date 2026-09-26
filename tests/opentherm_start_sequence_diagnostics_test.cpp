#include <cassert>
#include <cstdint>
#include <limits>

#include "../components/hcq_ot_boiler_simulator/opentherm_start_sequence_diagnostics.h"

using hcq::ot_sim::OpenThermStartSequenceDiagnostics;
using RequestType = OpenThermStartSequenceDiagnostics::RequestType;

int main() {
  OpenThermStartSequenceDiagnostics diagnostics;
  assert(diagnostics.ch_enable_rising_count() == 0);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 0);
  assert(!diagnostics.has_last_start());

  diagnostics.observe_valid_request(1, RequestType::WRITE_DATA, 0x3C00,
                                    1000000ULL, false);
  diagnostics.observe_valid_request(0, RequestType::READ_DATA, 0x0100,
                                    1750000ULL, true);
  assert(diagnostics.ch_enable_rising_count() == 1);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 1);
  assert(diagnostics.has_last_start());
  assert(diagnostics.last_start_preceded_by_tset());
  assert(diagnostics.last_start_previous_request_id() == 1);
  assert(diagnostics.last_tset_to_ch_enable_interval_us() == 750000);
  assert(diagnostics.last_start_tset_data() == 0x3C00);

  // Repeated STATUS(CH=on) frames do not create another rising-edge event.
  diagnostics.observe_valid_request(0, RequestType::READ_DATA, 0x0100,
                                    2500000ULL, false);
  assert(diagnostics.ch_enable_rising_count() == 1);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 1);
  assert(diagnostics.last_start_previous_request_id() == 1);

  // An intervening valid request makes the next edge an explicit mismatch.
  diagnostics.observe_valid_request(1, RequestType::WRITE_DATA, 0x3200,
                                    3000000ULL, false);
  diagnostics.observe_valid_request(25, RequestType::READ_DATA, 0,
                                    3200000ULL, false);
  diagnostics.observe_valid_request(0, RequestType::READ_DATA, 0x0100,
                                    4000000ULL, true);
  assert(diagnostics.ch_enable_rising_count() == 2);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 1);
  assert(!diagnostics.last_start_preceded_by_tset());
  assert(diagnostics.last_start_previous_request_id() == 25);

  // ID1 must be WRITE_DATA; a read of the same ID does not qualify.
  diagnostics.observe_valid_request(1, RequestType::READ_DATA, 0, 4500000ULL,
                                    false);
  diagnostics.observe_valid_request(0, RequestType::READ_DATA, 0x0100,
                                    5000000ULL, true);
  assert(diagnostics.ch_enable_rising_count() == 3);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 1);
  assert(!diagnostics.last_start_preceded_by_tset());
  assert(diagnostics.last_start_previous_request_id() == 1);

  // Reset deliberately drops request history. Reset between ID1 and ID0
  // therefore fails closed instead of reporting a false match.
  diagnostics.observe_valid_request(1, RequestType::WRITE_DATA, 0x2800,
                                    5500000ULL, false);
  diagnostics.reset();
  diagnostics.observe_valid_request(0, RequestType::READ_DATA, 0x0100,
                                    6000000ULL, true);
  assert(diagnostics.ch_enable_rising_count() == 1);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 0);
  assert(diagnostics.last_start_previous_request_id() == -1);

  // STATUS must be READ_DATA. A parity-valid WRITE_DATA ID0 does not count.
  diagnostics.reset();
  diagnostics.observe_valid_request(1, RequestType::WRITE_DATA, 0x2800,
                                    6500000ULL, false);
  diagnostics.observe_valid_request(0, RequestType::WRITE_DATA, 0x0100,
                                    7000000ULL, true);
  assert(diagnostics.ch_enable_rising_count() == 0);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 0);

  // Frames captured before reset are ignored even if processed afterwards.
  diagnostics.reset(8000000ULL);
  diagnostics.observe_valid_request(1, RequestType::WRITE_DATA, 0x2800,
                                    7900000ULL, false);
  diagnostics.observe_valid_request(0, RequestType::READ_DATA, 0x0100,
                                    8500000ULL, true);
  assert(diagnostics.ch_enable_rising_count() == 1);
  assert(diagnostics.ch_enable_rising_after_tset_count() == 0);
  assert(diagnostics.last_start_previous_request_id() == -1);

  // Extremely large intervals saturate instead of wrapping.
  diagnostics.reset();
  diagnostics.observe_valid_request(1, RequestType::WRITE_DATA, 0x2300, 1ULL,
                                    false);
  diagnostics.observe_valid_request(
      0, RequestType::READ_DATA, 0x0100,
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 2ULL, true);
  assert(diagnostics.last_tset_to_ch_enable_interval_us() ==
         std::numeric_limits<uint32_t>::max());
  return 0;
}
