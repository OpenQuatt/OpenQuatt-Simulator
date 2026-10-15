#pragma once

#include <algorithm>
#include <cstdint>

namespace esphome::quatt_odu_simulator {

// Nominal normal-mode behavior from one statically analyzed ODU firmware.
// The clock is supplied by the caller; no ESPHome or wall-clock dependency.
class CommunicationWatchdog {
 public:
  static constexpr uint32_t TICK_MS = 10U;
  static constexpr uint16_t LIMIT = 12000U;
  static constexpr uint16_t FAULT_MASK = 0x0001U;

  void reset(uint32_t now_ms) {
    last_tick_ms_ = now_ms;
    counter_ = 0U;
    timed_out_ = false;
  }

  void advance(uint32_t now_ms) {
    const uint32_t ticks = static_cast<uint32_t>(now_ms - last_tick_ms_) / TICK_MS;
    if (ticks == 0U) return;
    last_tick_ms_ += ticks * TICK_MS;
    const uint32_t remaining = LIMIT - counter_;
    // Compare before increment: reaching LIMIT is not a timeout until the
    // next call. A reset request leaves the current fault until that call.
    timed_out_ = ticks > remaining;
    counter_ = static_cast<uint16_t>(counter_ + std::min(ticks, remaining));
  }

  bool observe_request(uint8_t unit_address, uint8_t request_address, uint8_t function_code, bool crc_valid,
                       uint32_t now_ms) {
    advance(now_ms);  // Firmware checks the watchdog before receiving a frame.
    if (!crc_valid || unit_address != request_address ||
        (function_code != 0x03U && function_code != 0x06U && function_code != 0x10U))
      return false;
    counter_ = 0U;
    return true;
  }

  uint16_t counter() const { return counter_; }
  bool timed_out() const { return timed_out_; }

 private:
  uint32_t last_tick_ms_{0U};
  uint16_t counter_{0U};
  bool timed_out_{false};
};

}  // namespace esphome::quatt_odu_simulator
