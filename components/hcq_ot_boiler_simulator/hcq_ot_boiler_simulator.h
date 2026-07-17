#pragma once

#include <cmath>
#include <cstdint>

#include "esphome.h"
#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

#include "OpenTherm.h"
#include "boiler_simulator_model.h"

namespace esphome::hcq_ot_boiler_simulator {

class HCQOTBoilerSimulator : public PollingComponent
#ifdef USE_OTA_STATE_LISTENER
                             , public ota::OTAGlobalStateListener
#endif
{
 public:
  HCQOTBoilerSimulator();
  ~HCQOTBoilerSimulator();

  void set_in_pin(uint8_t pin) { in_pin_ = pin; }
  void set_out_pin(uint8_t pin) { out_pin_ = pin; }
  void set_enabled(bool value) { enabled_ = value; }
  void set_response_enabled(bool value) { response_enabled_ = value; }
  void set_dhw_present(bool value) { capabilities_.dhw_present = value; }
  void set_control_type_modulating(bool value) { capabilities_.control_type_modulating = value; }
  void set_cooling_supported(bool value) { capabilities_.cooling_supported = value; }
  void set_member_id(uint8_t value) { capabilities_.member_id = value; }
  void set_max_capacity_kw(uint8_t value) { capabilities_.max_capacity_kw = value; }
  void set_min_modulation_level(uint8_t value) { capabilities_.min_modulation_level = value; }
  void set_slave_ot_version(float value) { capabilities_.ot_version = value; }
  void set_product_type(uint8_t value) { capabilities_.product_type = value; }
  void set_product_version(uint8_t value) { capabilities_.product_version = value; }

  void set_automatic_mode(bool value) { automatic_mode_ = value; }
  void set_dhw_demand(bool value) { dhw_demand_ = value; }
  void set_manual_ch_active(bool value) { manual_ch_active_ = value; }
  void set_manual_dhw_active(bool value) { manual_dhw_active_ = value; }
  void set_manual_flame_on(bool value) { manual_flame_on_ = value; }
  void set_manual_telemetry(bool value) { manual_telemetry_ = value; }
  void set_fault_active(bool value) { fault_active_ = value; }
  void set_diagnostic_active(bool value) { diagnostic_active_ = value; }
  void set_service_request(bool value) { fault_flags_.service_request = value; }
  void set_lockout_reset(bool value) { fault_flags_.lockout_reset = value; }
  void set_low_water_pressure(bool value) { fault_flags_.low_water_pressure = value; }
  void set_flame_fault(bool value) { fault_flags_.flame_fault = value; }
  void set_air_pressure_fault(bool value) { fault_flags_.air_pressure_fault = value; }
  void set_water_over_temp(bool value) { fault_flags_.water_over_temp = value; }
  void set_oem_fault_code(uint8_t value) { fault_flags_.oem_fault_code = value; }
  void set_oem_diagnostic_code(uint16_t value) { fault_flags_.oem_diagnostic_code = value; }

  void set_ambient_temperature(float value) { model_inputs_.ambient_c = value; }
  void set_dhw_target(float value) { model_inputs_.dhw_target_c = value; }
  void set_return_delta(float value) { model_inputs_.return_delta_c = value; }
  void set_minimum_t_set(float value) { model_inputs_.minimum_t_set_c = value; }
  void set_ignition_delay(float value) { model_inputs_.ignition_delay_s = value; }
  void set_heat_rate(float value) { model_inputs_.heat_rate_c_per_s = value; }
  void set_cool_rate(float value) { model_inputs_.cool_rate_c_per_s = value; }
  void set_pressure(float value) { pressure_bar_ = value; }
  void set_max_t_set(float value) { max_t_set_c_ = value; }
  void set_dhw_temperature(float value) { dhw_temperature_c_ = value; }
  void set_manual_boiler_temperature(float value) { manual_boiler_temperature_c_ = value; }
  void set_manual_return_temperature(float value) { manual_return_temperature_c_ = value; }
  void set_manual_modulation(float value) { manual_modulation_pct_ = value; }

  void set_boiler_temperature_valid(bool value) { boiler_temperature_valid_ = value; }
  void set_return_temperature_valid(bool value) { return_temperature_valid_ = value; }
  void set_pressure_valid(bool value) { pressure_valid_ = value; }
  void set_modulation_valid(bool value) { modulation_valid_ = value; }
  void set_dhw_temperature_valid(bool value) { dhw_temperature_valid_ = value; }

  bool get_response_enabled() const { return response_enabled_; }
  bool get_master_status_valid() const;
  bool get_master_ch_enable() const { return master_state_.ch_enable; }
  bool get_master_dhw_enable() const { return master_state_.dhw_enable; }
  float get_master_t_set() const { return master_state_.t_set_c; }
  uint32_t get_request_count() const { return request_count_; }
  int get_last_request_id() const { return last_request_id_; }
  uint32_t get_last_request_age_ms() const;
  bool get_ch_active() const { return model_.state().ch_active; }
  bool get_dhw_active() const { return model_.state().dhw_active; }
  bool get_flame_on() const { return model_.state().flame_on; }
  bool get_fault() const { return model_.state().fault; }
  bool get_diagnostic() const { return model_.state().diagnostic; }
  float get_boiler_temperature() const;
  float get_return_temperature() const;
  float get_relative_modulation() const;
  const char *get_mode_name() const;
  void reset_model();

  void setup() override;
  void loop() override;
  void update() override;
  void on_shutdown() override;
  void dump_config() override;
#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                           ota::OTAComponent *component) override;
#endif

 protected:
  struct MasterState {
    bool ch_enable = false;
    bool dhw_enable = false;
    float t_set_c = 0.0f;
    float max_relative_modulation = NAN;
  };

  struct Capabilities {
    bool dhw_present = true;
    bool control_type_modulating = true;
    bool cooling_supported = false;
    uint8_t member_id = 1;
    uint8_t max_capacity_kw = 20;
    uint8_t min_modulation_level = 20;
    float ot_version = 2.2f;
    uint8_t product_type = 1;
    uint8_t product_version = 1;
  };

  struct FaultFlags {
    bool service_request = false;
    bool lockout_reset = false;
    bool low_water_pressure = false;
    bool flame_fault = false;
    bool air_pressure_fault = false;
    bool water_over_temp = false;
    uint8_t oem_fault_code = 0;
    uint16_t oem_diagnostic_code = 0;
  };

  static void process_request_callback_(unsigned long request,
                                        OpenThermResponseStatus status,
                                        void *context);
  void process_request_(unsigned long request, OpenThermResponseStatus status);
  void parse_request_(OpenThermMessageType type, OpenThermMessageID id, uint16_t data);
  unsigned long build_response_(OpenThermMessageType type, OpenThermMessageID id,
                                uint16_t data);
  uint16_t build_status_(uint16_t master_status) const;
  uint16_t build_config_() const;
  uint16_t build_fault_flags_() const;
  void refresh_model_(float delta_s);
  void schedule_start_();
  void try_start_();
  void start_();
  void stop_();

  uint8_t in_pin_ = 0;
  uint8_t out_pin_ = 0;
  bool enabled_ = true;
  bool response_enabled_ = true;
  bool started_ = false;
  bool start_pending_ = false;
  bool ota_active_ = false;
  unsigned long start_not_before_ms_ = 0;
  unsigned long bus_idle_since_ms_ = 0;
  unsigned long last_master_status_ms_ = 0;
  unsigned long last_request_ms_ = 0;
  unsigned long last_model_update_ms_ = 0;
  uint32_t request_count_ = 0;
  int last_request_id_ = -1;
  OpenTherm *opentherm_ = nullptr;

  MasterState master_state_{};
  Capabilities capabilities_{};
  FaultFlags fault_flags_{};
  hcq::ot_sim::Inputs model_inputs_{};
  hcq::ot_sim::BoilerSimulatorModel model_{};

  bool automatic_mode_ = true;
  bool dhw_demand_ = false;
  bool manual_ch_active_ = false;
  bool manual_dhw_active_ = false;
  bool manual_flame_on_ = false;
  bool manual_telemetry_ = false;
  bool fault_active_ = false;
  bool diagnostic_active_ = false;
  bool boiler_temperature_valid_ = true;
  bool return_temperature_valid_ = true;
  bool pressure_valid_ = true;
  bool modulation_valid_ = true;
  bool dhw_temperature_valid_ = true;
  float pressure_bar_ = 1.5f;
  float max_t_set_c_ = 80.0f;
  float dhw_temperature_c_ = 40.0f;
  float manual_boiler_temperature_c_ = 40.0f;
  float manual_return_temperature_c_ = 30.0f;
  float manual_modulation_pct_ = 50.0f;
};

}  // namespace esphome::hcq_ot_boiler_simulator
