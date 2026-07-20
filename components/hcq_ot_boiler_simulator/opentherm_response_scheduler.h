#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace hcq::ot_sim {

class OpenThermResponseScheduler {
 public:
  static constexpr uint32_t MIN_DELAY_MS = 20;
  static constexpr uint32_t MAX_DELAY_MS = 700;
  static constexpr uint32_t DEFAULT_DELAY_MS = 30;

  void set_delay_ms(uint32_t delay_ms) {
    delay_ms_ = std::clamp(delay_ms, MIN_DELAY_MS, MAX_DELAY_MS);
  }

  uint32_t delay_ms() const { return delay_ms_; }

  void schedule(uint32_t frame, uint64_t request_end_us) {
    if (pending_) {
      overlap_count_++;
    }
    pending_ = true;
    pending_frame_ = frame;
    request_end_us_ = request_end_us;
    due_us_ = request_end_us + static_cast<uint64_t>(delay_ms_) * 1000ULL;
    scheduled_count_++;
  }

  bool pending() const { return pending_; }
  bool due(uint64_t now_us) const { return pending_ && now_us >= due_us_; }
  uint32_t pending_frame() const { return pending_frame_; }
  uint64_t due_us() const { return due_us_; }

  void mark_queued(uint64_t queued_us) {
    if (!pending_) {
      return;
    }
    const uint64_t elapsed_us = queued_us >= request_end_us_ ? queued_us - request_end_us_ : 0;
    const uint32_t turnaround_us = static_cast<uint32_t>(
        std::min<uint64_t>(elapsed_us, std::numeric_limits<uint32_t>::max()));
    last_turnaround_us_ = turnaround_us;
    if (!has_turnaround_) {
      min_turnaround_us_ = turnaround_us;
      max_turnaround_us_ = turnaround_us;
      has_turnaround_ = true;
    } else {
      min_turnaround_us_ = std::min(min_turnaround_us_, turnaround_us);
      max_turnaround_us_ = std::max(max_turnaround_us_, turnaround_us);
    }
    queued_count_++;
    clear_pending();
  }

  void mark_queue_failed() {
    queue_failure_count_++;
    clear_pending();
  }

  void mark_suppressed() { suppressed_count_++; }

  void cancel_pending(bool count_as_suppressed) {
    if (pending_ && count_as_suppressed) {
      suppressed_count_++;
    }
    clear_pending();
  }

  void clear_pending() {
    pending_ = false;
    pending_frame_ = 0;
    request_end_us_ = 0;
    due_us_ = 0;
  }

  void reset_diagnostics() {
    scheduled_count_ = 0;
    queued_count_ = 0;
    queue_failure_count_ = 0;
    suppressed_count_ = 0;
    overlap_count_ = 0;
    last_turnaround_us_ = 0;
    min_turnaround_us_ = 0;
    max_turnaround_us_ = 0;
    has_turnaround_ = false;
  }

  uint32_t scheduled_count() const { return scheduled_count_; }
  uint32_t queued_count() const { return queued_count_; }
  uint32_t queue_failure_count() const { return queue_failure_count_; }
  uint32_t suppressed_count() const { return suppressed_count_; }
  uint32_t overlap_count() const { return overlap_count_; }
  bool has_turnaround() const { return has_turnaround_; }
  uint32_t last_turnaround_us() const { return last_turnaround_us_; }
  uint32_t min_turnaround_us() const { return min_turnaround_us_; }
  uint32_t max_turnaround_us() const { return max_turnaround_us_; }

 private:
  uint32_t delay_ms_ = DEFAULT_DELAY_MS;
  bool pending_ = false;
  uint32_t pending_frame_ = 0;
  uint64_t request_end_us_ = 0;
  uint64_t due_us_ = 0;
  uint32_t scheduled_count_ = 0;
  uint32_t queued_count_ = 0;
  uint32_t queue_failure_count_ = 0;
  uint32_t suppressed_count_ = 0;
  uint32_t overlap_count_ = 0;
  bool has_turnaround_ = false;
  uint32_t last_turnaround_us_ = 0;
  uint32_t min_turnaround_us_ = 0;
  uint32_t max_turnaround_us_ = 0;
};

}  // namespace hcq::ot_sim
