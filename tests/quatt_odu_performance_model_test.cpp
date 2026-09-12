#include <cassert>
#include <cmath>
#include <limits>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

static bool close(float actual, float expected, float tolerance = 0.01f) {
  return std::fabs(actual - expected) <= tolerance;
}

int main() {
  QuattOduSimulatorModel model;
  model.configure(Profile::V1);
  auto v1 = model.performance_at(30.0f, 2.0f, 35.0f);
  assert(close(v1.thermal_power_w, 1675.91f));
  assert(close(v1.cop, 3.96f));

  auto interpolated = model.performance_at(34.5f, 2.0f, 35.0f);
  assert(interpolated.thermal_power_w > 1675.91f && interpolated.thermal_power_w < 2171.97f);

  // Preserve the legacy simulator contract: V1 clamps temperature and low
  // frequency inputs to the nearest anchor instead of going thermally idle.
  auto v1_clamped = model.performance_at(20.0, 0.0, 30.0);
  auto v1_first_anchor = model.performance_at(30.0, 2.0, 35.0);
  assert(v1_clamped.valid && v1_first_anchor.valid);
  assert(close(v1_clamped.thermal_power_w, v1_first_anchor.thermal_power_w));
  assert(close(v1_clamped.cop, v1_first_anchor.cop));

  model.configure(Profile::V2_NEW);
  auto issue_point = model.performance_at(20.0, 12.6, 22.5);
  assert(issue_point.valid);
  assert(close(issue_point.thermal_power_w, 3072.6185f, 0.02f));
  assert(close(issue_point.cop, 7.064095f, 0.0001f));

  auto before_mask = model.performance_at(80.0, -15.0, 54.999999);
  assert(before_mask.valid);
  auto at_mask = model.performance_at(80.0, -15.0, 55.0);
  assert(!at_mask.valid);
  assert(at_mask.thermal_power_w == 0.0f && at_mask.cop == 0.0f);
  assert(!model.performance_at(20.0, -15.1, 22.5).valid);
  assert(!model.performance_at(20.0, 12.6, 70.1).valid);
  assert(!model.performance_at(19.9, 12.6, 22.5).valid);
  assert(!model.performance_at(std::numeric_limits<double>::quiet_NaN(), 12.6, 22.5).valid);
  assert(!model.performance_at(20.0, std::numeric_limits<double>::infinity(), 22.5).valid);
  assert(!model.performance_at(20.0, 12.6, -std::numeric_limits<double>::infinity()).valid);

  auto high = model.performance_at(110.0f, -15.0f, 18.0f);
  auto at_90 = model.performance_at(90.0f, -15.0f, 18.0f);
  assert(high.high_frequency_synthetic);
  assert(high.valid && at_90.valid);
  assert(close(high.thermal_power_w, at_90.thermal_power_w));
  assert(close(high.cop, at_90.cop));
  assert(high.thermal_power_w > 0.0f && high.cop > 0.1f);

  auto midpoint = model.performance_at(20.0f, 12.6f, 22.5f);
  assert(midpoint.valid && midpoint.thermal_power_w > 0.0f);
  assert(midpoint.cop > 0.1f && midpoint.cop < 15.0f);

  model.mutable_settings().experimental_high_frequency_extrapolation = true;
  auto extrapolated = model.performance_at(110.0f, -15.0f, 18.0f);
  assert(extrapolated.high_frequency_synthetic);
  assert(!close(extrapolated.thermal_power_w, at_90.thermal_power_w));
  assert(extrapolated.thermal_power_w > 0.0f && extrapolated.cop > 0.1f);
  return 0;
}
