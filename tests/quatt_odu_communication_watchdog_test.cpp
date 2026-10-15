#include <cassert>
#include <cmath>
#include <cstdint>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

namespace {
uint16_t read(QuattOduSimulatorModel& model, uint16_t address) {
  uint16_t value = 0U;
  assert(model.read_register(address, value));
  return value;
}

// A controllable monotonic clock. Requests do not implicitly run the control
// update, so both legal interleavings at the timeout boundary can be tested.
struct Lab {
  uint32_t now_ms{0U};
  QuattOduSimulatorModel model;

  Lab() { model.configure(Profile::V1_5, now_ms); }
  void request(uint8_t function_code = 0x03U, uint8_t address = 1U, bool crc_valid = true) {
    model.observe_modbus_request(1U, address, function_code, crc_valid, now_ms);
  }
  void advance(uint32_t duration_ms) {
    while (duration_ms != 0U) {
      const uint32_t step = std::min(duration_ms, 250U);
      now_ms += step;
      model.advance_communication_watchdog(now_ms);
      model.update(step / 1000.0f);
      duration_ms -= step;
    }
  }
  void start() {
    request(0x06U);
    assert(model.write_register(2010U, 4096U, now_ms));
    assert(model.write_register(2015U, 150U, now_ms));
    assert(model.write_register(1999U, 5U, now_ms));
    assert(model.write_register(3999U, 2U, now_ms));
    advance(5000U);
    request();
    assert(model.state().measured_frequency_hz > 0.0f);
  }
};

void test_compare_before_increment_and_saturation() {
  CommunicationWatchdog watchdog;
  watchdog.reset(0U);
  watchdog.advance(119990U);
  assert(watchdog.counter() == 11999U && !watchdog.timed_out());
  watchdog.advance(120000U);
  assert(watchdog.counter() == 12000U && !watchdog.timed_out());
  watchdog.advance(120009U);
  assert(!watchdog.timed_out());
  watchdog.advance(120010U);
  assert(watchdog.counter() == 12000U && watchdog.timed_out());
  watchdog.advance(900000U);
  assert(watchdog.counter() == 12000U && watchdog.timed_out());
}

void test_reset_keeps_fault_until_next_tick_and_preserves_phase() {
  CommunicationWatchdog watchdog;
  watchdog.reset(0U);
  watchdog.advance(120010U);
  assert(watchdog.observe_request(1U, 1U, 0x03U, true, 120015U));
  assert(watchdog.counter() == 0U && watchdog.timed_out());
  watchdog.advance(120019U);
  assert(watchdog.timed_out());
  watchdog.advance(120020U);
  assert(watchdog.counter() == 1U && !watchdog.timed_out());
}

void test_request_filter_and_invalid_register_range() {
  for (const uint8_t function_code : {0x03U, 0x06U, 0x10U}) {
    CommunicationWatchdog watchdog;
    watchdog.advance(100000U);
    assert(watchdog.observe_request(2U, 2U, function_code, true, 100005U));
    assert(watchdog.counter() == 0U);
  }
  for (int scenario = 0; scenario < 4; ++scenario) {
    CommunicationWatchdog watchdog;
    watchdog.advance(100000U);
    const uint8_t address = scenario == 0 ? 2U : scenario == 1 ? 0U : 1U;
    const bool crc_valid = scenario != 2;
    const uint8_t function_code = scenario == 3 ? 0x04U : 0x03U;
    assert(!watchdog.observe_request(1U, address, function_code, crc_valid, 100000U));
    watchdog.advance(120010U);
    assert(watchdog.timed_out());
  }
  Lab lab;
  lab.advance(100000U);
  lab.request();  // Reception precedes register-range validation.
  uint16_t value = 0U;
  assert(!lab.model.read_register(65000U, value));
  assert(lab.model.communication_watchdog_counter() == 0U);
  lab.advance(10000U);
  lab.request(0x06U);
  assert(!lab.model.write_register(65000U, 1U, lab.now_ms));
  assert(lab.model.communication_watchdog_counter() == 0U);
}

void test_polling_without_writes_and_without_acknowledgements() {
  Lab lab;
  lab.start();
  const uint32_t writes = lab.model.state().protocol.write_count;
  for (int i = 0; i < 30; ++i) {
    lab.advance(10000U);
    lab.request();  // No answer/acknowledgement is required by the watchdog.
    assert(read(lab.model, 3999U) == 2U);
    assert(read(lab.model, 1999U) == 5U);
    assert((read(lab.model, 2121U) & 1U) == 0U);
  }
  assert(lab.model.state().protocol.write_count == writes);
  assert(lab.model.state().measured_frequency_hz > 0.0f);
}

void test_timeout_stops_requests_then_ramps_and_reads_do_not_restart() {
  Lab lab;
  lab.start();
  lab.model.set_fault_word(0U, 4U);
  lab.model.set_fault_word(1U, 64U);
  lab.model.set_fault_word(2U, 0xA100U);
  lab.advance(120000U);
  assert(read(lab.model, 3999U) == 2U && (read(lab.model, 2121U) & 1U) == 0U);
  const float running_hz = lab.model.state().measured_frequency_hz;
  lab.advance(10U);
  assert(read(lab.model, 2121U) == 0xA101U);
  assert(read(lab.model, 2119U) == 4U && read(lab.model, 2120U) == 64U);
  assert(read(lab.model, 3999U) == 0U && read(lab.model, 1999U) == 0U);
  assert(lab.model.state().requested_table_frequency_hz == 0.0f);
  assert(lab.model.state().target_frequency_hz == 0.0f);
  assert(lab.model.state().driver_frequency_hz == 0.0f);
  assert(lab.model.state().measured_frequency_hz > 0.0f);
  assert(lab.model.state().measured_frequency_hz < running_hz);
  assert(lab.model.state().pump_request);  // Pump state is not indiscriminately erased.

  lab.model.set_fault_word(2U, 0xA100U);  // UI refresh cannot erase the generated fault.
  assert(read(lab.model, 2121U) == 0xA101U);
  lab.request();
  assert(read(lab.model, 2121U) == 0xA101U);
  lab.advance(10U);
  assert(read(lab.model, 2121U) == 0xA100U);
  for (int i = 0; i < 20; ++i) {
    lab.advance(1000U);
    lab.request();
  }
  assert(read(lab.model, 3999U) == 0U && read(lab.model, 1999U) == 0U);
  assert(lab.model.state().measured_frequency_hz == 0.0f);
  assert(lab.model.state().active_mode == WorkingMode::STANDBY);
  assert(lab.model.write_register(3999U, 2U, lab.now_ms));
  lab.advance(5000U);
  assert(lab.model.state().measured_frequency_hz == 0.0f);  // Level was not restored.
  assert(lab.model.write_register(1999U, 5U, lab.now_ms));
  lab.advance(5000U);
  assert(lab.model.state().measured_frequency_hz > 0.0f);
}

void test_request_at_timeout_and_control_interleavings() {
  for (bool control_before_clear : {false, true}) {
    Lab lab;
    lab.start();
    lab.now_ms += 120010U;
    lab.request();  // Compare before receipt raises the fault, then resets only the counter.
    assert((read(lab.model, 2121U) & 1U) != 0U);
    assert(lab.model.communication_watchdog_counter() == 0U);
    if (control_before_clear) lab.model.update(0.01f);
    lab.now_ms += 10U;
    lab.model.advance_communication_watchdog(lab.now_ms);
    lab.model.update(0.01f);
    assert((read(lab.model, 2121U) & 1U) == 0U);
    assert(read(lab.model, 3999U) == (control_before_clear ? 0U : 2U));
  }
  Lab at_limit;
  at_limit.start();
  at_limit.now_ms += 120000U;
  at_limit.request();
  assert((read(at_limit.model, 2121U) & 1U) == 0U);
  assert(read(at_limit.model, 3999U) == 2U);
}

void test_duo_isolation_rollover_and_diagnostics_reset() {
  QuattOduSimulatorModel first, second;
  const uint32_t origin = UINT32_MAX - 50000U;
  first.configure(Profile::V1_5, origin);
  second.configure(Profile::V1_5, origin);
  for (uint32_t elapsed = 10000U; elapsed <= 130000U; elapsed += 10000U) {
    const uint32_t now = origin + elapsed;
    first.observe_modbus_request(1U, 2U, 0x03U, true, now);
    second.observe_modbus_request(2U, 2U, 0x03U, true, now);
  }
  assert((read(first, 2121U) & 1U) != 0U);
  assert((read(second, 2121U) & 1U) == 0U);
  first.reset_diagnostics();
  assert(first.communication_watchdog_counter() == CommunicationWatchdog::LIMIT);
  first.configure(Profile::V1_5, 123456U);
  first.advance_communication_watchdog(123465U);
  assert(first.communication_watchdog_counter() == 0U);
  first.advance_communication_watchdog(123466U);
  assert(first.communication_watchdog_counter() == 1U);
}

void test_forced_defrost_and_deferred_level_cannot_resurrect() {
  Lab lab;
  lab.start();
  lab.model.mutable_settings().forced_defrost_duration_s = 300.0f;
  assert(lab.model.write_register(3999U, 4U, lab.now_ms));
  assert(lab.model.write_register(1999U, 7U, lab.now_ms));
  assert(lab.model.state().deferred_defrost_level_pending);
  lab.advance(120010U);
  assert(!lab.model.state().forced_defrost);
  assert(lab.model.state().defrost_aborted == 1U);
  assert(!lab.model.state().deferred_defrost_level_pending);
  lab.request();
  lab.advance(10000U);
  assert(read(lab.model, 3999U) == 0U && read(lab.model, 1999U) == 0U);
  assert(lab.model.state().measured_frequency_hz == 0.0f);
}

void test_injected_fault_survives_watchdog_clear_and_pending_start_is_stopped() {
  Lab lab;
  lab.model.set_fault_word(2U, 0x8001U);
  lab.request();
  lab.advance(10U);
  assert(read(lab.model, 2121U) == 0x8001U);
  lab.model.set_fault_word(2U, 0x8000U);
  assert(read(lab.model, 2121U) == 0x8000U);
  assert(lab.model.write_register(1999U, 5U, lab.now_ms));
  assert(lab.model.write_register(3999U, 2U, lab.now_ms));
  lab.now_ms += 120010U;
  lab.model.advance_communication_watchdog(lab.now_ms);
  lab.model.update(0.25f);
  assert(read(lab.model, 3999U) == 0U && read(lab.model, 1999U) == 0U);
  assert(lab.model.state().measured_frequency_hz == 0.0f);
}
}  // namespace

int main() {
  test_compare_before_increment_and_saturation();
  test_reset_keeps_fault_until_next_tick_and_preserves_phase();
  test_request_filter_and_invalid_register_range();
  test_polling_without_writes_and_without_acknowledgements();
  test_timeout_stops_requests_then_ramps_and_reads_do_not_restart();
  test_request_at_timeout_and_control_interleavings();
  test_duo_isolation_rollover_and_diagnostics_reset();
  test_forced_defrost_and_deferred_level_cannot_resurrect();
  test_injected_fault_survives_watchdog_clear_and_pending_start_is_stopped();
}
