#include <cassert>
#include <cstdint>

#include "../components/hcq_ot_boiler_simulator/opentherm_response_scheduler.h"

using hcq::ot_sim::OpenThermResponseScheduler;

int main() {
  OpenThermResponseScheduler scheduler;
  assert(scheduler.delay_ms() == 30);

  scheduler.set_delay_ms(0);
  assert(scheduler.delay_ms() == OpenThermResponseScheduler::MIN_DELAY_MS);
  scheduler.set_delay_ms(1000);
  assert(scheduler.delay_ms() == OpenThermResponseScheduler::MAX_DELAY_MS);
  scheduler.set_delay_ms(30);

  scheduler.schedule(0x40000000U, 1000000ULL);
  assert(scheduler.pending());
  assert(scheduler.pending_frame() == 0x40000000U);
  assert(scheduler.due_us() == 1030000ULL);
  assert(!scheduler.due(1029999ULL));
  assert(scheduler.due(1030000ULL));
  scheduler.mark_queued(1030400ULL);
  assert(!scheduler.pending());
  assert(scheduler.scheduled_count() == 1);
  assert(scheduler.queued_count() == 1);
  assert(scheduler.has_turnaround());
  assert(scheduler.last_turnaround_us() == 30400U);
  assert(scheduler.min_turnaround_us() == 30400U);
  assert(scheduler.max_turnaround_us() == 30400U);

  scheduler.set_delay_ms(20);
  scheduler.schedule(0x40010000U, 2000000ULL);
  scheduler.mark_queued(2021000ULL);
  assert(scheduler.last_turnaround_us() == 21000U);
  assert(scheduler.min_turnaround_us() == 21000U);
  assert(scheduler.max_turnaround_us() == 30400U);

  scheduler.schedule(0x40020000U, 3000000ULL);
  scheduler.schedule(0x40030000U, 3001000ULL);
  assert(scheduler.overlap_count() == 1);
  assert(scheduler.pending_frame() == 0x40030000U);
  scheduler.cancel_pending(true);
  assert(!scheduler.pending());
  assert(scheduler.suppressed_count() == 1);

  scheduler.mark_suppressed();
  assert(scheduler.suppressed_count() == 2);
  scheduler.schedule(0x40040000U, 4000000ULL);
  scheduler.mark_queue_failed();
  assert(scheduler.queue_failure_count() == 1);
  assert(!scheduler.pending());

  scheduler.schedule(0x40050000U, 5000000ULL);
  scheduler.reset_diagnostics();
  assert(scheduler.delay_ms() == 20);
  assert(scheduler.pending());
  assert(scheduler.pending_frame() == 0x40050000U);
  assert(scheduler.due_us() == 5020000ULL);
  assert(scheduler.scheduled_count() == 0);
  assert(scheduler.queued_count() == 0);
  assert(scheduler.queue_failure_count() == 0);
  assert(scheduler.suppressed_count() == 0);
  assert(scheduler.overlap_count() == 0);
  assert(!scheduler.has_turnaround());
  scheduler.mark_queued(5020000ULL);
  assert(!scheduler.pending());
  assert(scheduler.queued_count() == 1);
  assert(scheduler.last_turnaround_us() == 20000U);
  return 0;
}
