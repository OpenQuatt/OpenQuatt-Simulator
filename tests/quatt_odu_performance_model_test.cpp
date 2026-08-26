#include <cassert>
#include <cmath>

#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"

using namespace esphome::quatt_odu_simulator;

static bool close(float actual, float expected, float tolerance = 0.1f) {
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

  model.configure(Profile::V2_NEW);
  auto v2 = model.performance_at(20.0f, -10.0f, 35.0f);
  assert(close(v2.thermal_power_w, 3238.45f));
  assert(close(v2.cop, 3.97f));

  auto high = model.performance_at(110.0f, -10.0f, 35.0f);
  auto at_90 = model.performance_at(90.0f, -10.0f, 35.0f);
  assert(high.high_frequency_synthetic);
  assert(close(high.thermal_power_w, at_90.thermal_power_w));
  assert(close(high.cop, at_90.cop));
  assert(high.thermal_power_w > 0.0f && high.cop > 0.1f);

  auto midpoint = model.performance_at(83.5f, 0.5f, 40.0f);
  assert(midpoint.thermal_power_w > 0.0f);
  assert(midpoint.cop > 0.1f && midpoint.cop < 15.0f);

  model.mutable_settings().experimental_high_frequency_extrapolation = true;
  auto extrapolated = model.performance_at(110.0f, -10.0f, 35.0f);
  assert(extrapolated.high_frequency_synthetic);
  assert(!close(extrapolated.thermal_power_w, at_90.thermal_power_w));
  assert(extrapolated.thermal_power_w > 0.0f && extrapolated.cop > 0.1f);
  return 0;
}
