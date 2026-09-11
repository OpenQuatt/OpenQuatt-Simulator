#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

#include "../hcq_ot_boiler_simulator/OpenTherm.h"

namespace esphome::hcq_ot_thermostat_simulator {

class HCQOTThermostatSimulator final : public Component
#ifdef USE_OTA_STATE_LISTENER
    ,
                                       public ota::OTAGlobalStateListener
#endif
{
public:
  void set_in_pin(uint8_t value) { in_pin_ = value; }
  void set_out_pin(uint8_t value) { out_pin_ = value; }
  void set_enabled(bool value) { enabled_ = value; }
  void set_poll_interval_ms(uint32_t value) { poll_interval_ms_ = value; }
  void set_ch_enable(bool value) { ch_enable_ = value; }
  void set_dhw_enable(bool value) { dhw_enable_ = value; }
  void set_t_set(float value) { t_set_c_ = value; }
  void set_t_room_set(float value) { t_room_set_c_ = value; }
  void set_t_room(float value) { t_room_c_ = value; }
  void reset_diagnostics();

  uint32_t request_count() const { return request_count_; }
  uint32_t response_count() const { return response_count_; }
  uint32_t timeout_count() const { return timeout_count_; }
  uint32_t invalid_response_count() const { return invalid_response_count_; }
  int last_response_id() const { return last_response_id_; }
  uint32_t last_response_age_ms() const;
  bool controller_ch_active() const { return controller_ch_active_; }
  bool controller_dhw_active() const { return controller_dhw_active_; }
  bool controller_fault() const { return controller_fault_; }
  bool controller_flame() const { return controller_flame_; }
  uint32_t rx_queue_overflow_count() const;
  uint32_t tx_error_count() const;

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                           ota::OTAComponent *component) override;
#endif

protected:
  enum class Request : uint8_t {
    STATUS,
    T_SET,
    T_ROOM_SET,
    T_ROOM,
  };

  static void response_callback_(unsigned long frame,
                                 OpenThermResponseStatus status, void *context);
  void process_response_(unsigned long frame, OpenThermResponseStatus status);
  void send_next_request_();
  void start_();
  void stop_();

  uint8_t in_pin_{0};
  uint8_t out_pin_{0};
  bool enabled_{true};
  bool ota_active_{false};
  bool started_{false};
  bool ch_enable_{false};
  bool dhw_enable_{false};
  float t_set_c_{20.0f};
  float t_room_set_c_{21.0f};
  float t_room_c_{20.0f};
  uint32_t poll_interval_ms_{1000};
  uint32_t last_request_ms_{0};
  uint32_t last_response_ms_{0};
  Request next_request_{Request::STATUS};
  uint32_t request_count_{0};
  uint32_t response_count_{0};
  uint32_t timeout_count_{0};
  uint32_t invalid_response_count_{0};
  int last_response_id_{-1};
  bool controller_ch_active_{false};
  bool controller_dhw_active_{false};
  bool controller_fault_{false};
  bool controller_flame_{false};
  OpenTherm *opentherm_{nullptr};
};

} // namespace esphome::hcq_ot_thermostat_simulator
