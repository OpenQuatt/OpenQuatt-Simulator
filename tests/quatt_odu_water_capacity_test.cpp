#include <cassert>
#include <cmath>
#include <cstdio>
#include "../components/quatt_odu_simulator/quatt_odu_simulator_model.h"
using namespace esphome::quatt_odu_simulator;
constexpr float kCapacity = 4180.0f * 800.0f / 3600.0f * 4.0f;
QuattOduSimulatorModel steady(float flow, float frequency = 0.0f, WorkingMode mode = WorkingMode::HEATING) {
  QuattOduSimulatorModel m;
  m.configure(Profile::V1_5);
  m.mutable_settings().flow_offset_lph = flow;
  m.mutable_settings().flow_gain_lph_per_ipwm = 0.0f;
  m.mutable_settings().freeze_measured_frequency = true;
  m.mutable_state().force_flow_without_relay = true;
  m.mutable_state().flow_lph = flow;
  m.mutable_state().measured_frequency_hz = frequency;
  m.mutable_state().active_mode = m.mutable_state().requested_mode = mode;
  return m;
}
int main() {
  // Independent analytic cooling solution; subdivision must not change it.
  for (float step : {0.25f, 1.0f, 2.0f}) {
    auto m = steady(800.0f);
    m.mutable_state().water_out_temperature_c = 50.0f;
    for (int i = 0; i < static_cast<int>(20.0f / step); ++i)
      m.update(step);
    assert(std::fabs(m.state().water_out_temperature_c - (30.0f + 20.0f * std::exp(-5.0f))) < 0.0001f);
  }
  // Closed water retains heat after the compressor stops.
  auto trapped = steady(0.0f);
  trapped.mutable_state().water_out_temperature_c = 50.0f;
  for (int i = 0; i < 20; ++i)
    trapped.update(1.0f);
  assert(trapped.state().water_out_temperature_c == 50.0f);
  // No discontinuity at the former 1 L/h cutoff; zero-flow input is energy bounded.
  float previous = 0.0f;
  for (float flow : {0.0f, 0.99f, 1.0f, 1.01f}) {
    auto m = steady(flow, 50.0f);
    m.update(0.25f);
    const float rise = m.state().water_out_temperature_c - 30.0f;
    assert(rise > 0.0f && rise <= m.state().thermal_power_w * 0.25f / kCapacity + 0.00001f);
    if (flow > 0.0f)
      assert(std::fabs(rise - previous) < 0.001f);
    previous = rise;
  }
  // Heating, cooling and defrost retain the stationary calorimetric balance.
  for (int scenario = 0; scenario < 3; ++scenario) {
    auto m = steady(800.0f, 50.0f, scenario == 1 ? WorkingMode::COOLING : WorkingMode::HEATING);
    if (scenario == 2)
      m.set_defrost(true);
    for (int i = 0; i < 800; ++i)
      m.update(0.25f);
    const float conductance = 4180.0f * 800.0f / 3600.0f;
    const float expected =
        (scenario == 1 ? -1.0f : 1.0f) * m.state().thermal_power_w - (scenario == 2 ? 4.0f * conductance : 0.0f);
    assert(std::fabs(conductance * (m.state().water_out_temperature_c - 30.0f) - expected) < 0.1f);
  }
  // Pump-stop sequence cannot add more heat than the simulated compressor delivered.
  QuattOduSimulatorModel m;
  m.configure(Profile::V1_5);
  m.mutable_settings().compressor_start_delay_s = m.mutable_settings().minimum_runtime_s = 0.0f;
  m.write_register(1999, 8, 1);
  m.write_register(3999, 2, 2);
  m.write_register(2010, 4096, 3);
  m.write_register(2015, 150, 4);
  for (int i = 0; i < 120; ++i)
    m.update(0.25f);
  const float start = m.state().water_out_temperature_c;
  float energy_j = 0.0f;
  m.write_register(2015, 1000, 5);
  for (int i = 0; i < 80; ++i) {
    m.update(0.25f);
    energy_j += m.state().thermal_power_w * 0.25f;
    assert(std::isfinite(m.state().water_out_temperature_c));
    assert(m.state().water_out_temperature_c <= start + energy_j / kCapacity + 0.001f);
  }
  auto invalid = steady(0.0f, 50.0f);
  invalid.mutable_settings().water_response_tau_s = 0.0f;
  invalid.update(1.0f);
  assert(invalid.state().water_out_temperature_c == 30.0f);
  // A tiny finite capacity reaches equilibrium without overflowing intermediates.
  auto tiny = steady(800.0f, 50.0f);
  tiny.mutable_settings().water_response_tau_s = 1e-40f;
  tiny.update(0.25f);
  const float equilibrium = 30.0f + tiny.state().thermal_power_w / (4180.0f * 800.0f / 3600.0f);
  assert(std::isfinite(tiny.state().water_out_temperature_c));
  assert(std::fabs(tiny.state().water_out_temperature_c - equilibrium) < 0.0001f);
  std::puts("Finite water capacity tests passed");
}
