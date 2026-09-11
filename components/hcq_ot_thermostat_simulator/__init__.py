import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import esp32, esp32_rmt
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID
from esphome.core import CORE, coroutine_with_priority

CONF_IN_PIN = "in_pin"
CONF_OUT_PIN = "out_pin"
CONF_ENABLED = "enabled"
CONF_POLL_INTERVAL_MS = "poll_interval_ms"

thermostat_ns = cg.esphome_ns.namespace("hcq_ot_thermostat_simulator")
HCQOTThermostatSimulator = thermostat_ns.class_("HCQOTThermostatSimulator", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HCQOTThermostatSimulator),
        cv.Required(CONF_IN_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_OUT_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_ENABLED, True): cv.boolean,
        cv.Optional(CONF_POLL_INTERVAL_MS, 1000): cv.int_range(min=250, max=10000),
    }
).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config):
    if CORE.target_framework != "esp-idf":
        raise cv.Invalid("hcq_ot_thermostat_simulator requires esp32.framework.type: esp-idf")
    if esp32.get_esp32_variant() in esp32_rmt.VARIANTS_NO_RMT:
        raise cv.Invalid("hcq_ot_thermostat_simulator requires ESP32 RMT support")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


@coroutine_with_priority(2.0)
async def to_code(config):
    include_builtin_idf_component("esp_driver_rmt")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_in_pin(config[CONF_IN_PIN]))
    cg.add(var.set_out_pin(config[CONF_OUT_PIN]))
    cg.add(var.set_enabled(config[CONF_ENABLED]))
    cg.add(var.set_poll_interval_ms(config[CONF_POLL_INTERVAL_MS]))
