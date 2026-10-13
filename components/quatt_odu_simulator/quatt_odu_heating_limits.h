#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace esphome::quatt_odu_simulator {

enum HeatingLimitReason : uint8_t {
  HEATING_LIMIT_NONE = 0U,
  HEATING_LIMIT_OUTSIDE = 1U,
  HEATING_LIMIT_HIGH_LEVEL_RELEASE = 2U,
  HEATING_LIMIT_QUIET = 4U,
  HEATING_LIMIT_INVALID_TEMPERATURE = 8U,
};

inline const char* heating_limit_reason_text(uint8_t reasons) {
  switch (reasons) {
    case 0:
      return "none";
    case 1:
      return "outside temperature";
    case 2:
      return "high-level release";
    case 3:
      return "outside temperature, high-level release";
    case 4:
      return "quiet mode";
    case 5:
      return "outside temperature, quiet mode";
    case 6:
      return "high-level release, quiet mode";
    case 7:
      return "outside temperature, high-level release, quiet mode";
    default:
      return "invalid temperature";
  }
}

struct HeatingLimitResult {
  uint8_t physical_level{0U};
  uint8_t reasons{HEATING_LIMIT_NONE};
};

// A bounded V2 new normal-heating preset. Limits select physical levels;
// frequencies always come from the model's current heating table. This is
// neither an inverter model nor a complete set of compressor protections.
class HeatingLimits {
 public:
  void reset() {
    this->outside_band_ = NO_BAND;
    this->high_levels_released_ = false;
  }

  bool high_levels_released() const { return this->high_levels_released_; }
  uint8_t outside_level_limit() const {
    return this->outside_band_ < OUTSIDE_LEVELS.size() ? OUTSIDE_LEVELS[this->outside_band_] : 0U;
  }

  HeatingLimitResult apply(uint8_t requested_level, float outside_c, float water_in_c, bool quiet) {
    if (!std::isfinite(outside_c) || !std::isfinite(water_in_c)) {
      // Explicit lab policy for invalid inputs, not a sensor-fault emulation.
      this->reset();
      return {0U, HEATING_LIMIT_INVALID_TEMPERATURE};
    }

    if (outside_c < -13.0f || outside_c > 0.0f || water_in_c > 35.0f)
      this->high_levels_released_ = false;
    else if (outside_c >= -12.0f && outside_c <= -1.0f && water_in_c <= 34.0f)
      this->high_levels_released_ = true;

    this->update_outside_band_(outside_c);
    HeatingLimitResult result{requested_level, HEATING_LIMIT_NONE};
    const auto limit = [&](uint8_t level, uint8_t reason) {
      if (requested_level > level) result.reasons |= reason;
      result.physical_level = std::min(result.physical_level, level);
    };
    limit(OUTSIDE_LEVELS[this->outside_band_], HEATING_LIMIT_OUTSIDE);
    if (!this->high_levels_released_) limit(18U, HEATING_LIMIT_HIGH_LEVEL_RELEASE);
    if (quiet) limit(14U, HEATING_LIMIT_QUIET);
    return result;
  }

 private:
  void update_outside_band_(float outside_c) {
    // Keep the previous band in its hysteresis window. Cold shutdown has
    // a distinct restart threshold; the lowest running band excludes -25 C.
    if (this->outside_band_ == 0U && outside_c <= -20.0f) return;
    if (this->outside_band_ == 1U && outside_c > -25.0f && outside_c <= -3.0f) return;
    if (this->outside_band_ >= 2U && this->outside_band_ <= 8U &&
        outside_c >= OUTSIDE_BOUNDARIES[this->outside_band_ - 1U] - 1.0f &&
        outside_c <= OUTSIDE_BOUNDARIES[this->outside_band_] + 1.0f)
      return;
    if (this->outside_band_ == 9U && outside_c >= 45.0f) return;

    this->outside_band_ = 0U;
    while (this->outside_band_ < OUTSIDE_BOUNDARIES.size() && outside_c > OUTSIDE_BOUNDARIES[this->outside_band_])
      this->outside_band_++;
  }

  static constexpr uint8_t NO_BAND = 0xFFU;
  static constexpr std::array<float, 9> OUTSIDE_BOUNDARIES{
      {-25.0f, -4.0f, 1.0f, 6.0f, 12.0f, 20.0f, 24.0f, 28.0f, 46.0f}};
  static constexpr std::array<uint8_t, 10> OUTSIDE_LEVELS{{0U, 20U, 18U, 16U, 14U, 12U, 10U, 8U, 6U, 0U}};
  uint8_t outside_band_{NO_BAND};
  bool high_levels_released_{false};
};

}  // namespace esphome::quatt_odu_simulator
