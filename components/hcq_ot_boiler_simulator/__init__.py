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
CONF_RESPONSE_ENABLED = "response_enabled"
CONF_RESPONSE_DELAY_MS = "response_delay_ms"
CONF_DHW_PRESENT = "dhw_present"
CONF_CONTROL_TYPE_MODULATING = "control_type_modulating"
CONF_COOLING_SUPPORTED = "cooling_supported"
CONF_MEMBER_ID = "member_id"
CONF_MAX_CAPACITY_KW = "max_capacity_kw"
CONF_MIN_MODULATION_LEVEL = "min_modulation_level"
CONF_SLAVE_OT_VERSION = "slave_ot_version"
CONF_PRODUCT_TYPE = "product_type"
CONF_PRODUCT_VERSION = "product_version"

simulator_ns = cg.esphome_ns.namespace("hcq_ot_boiler_simulator")
HCQOTBoilerSimulator = simulator_ns.class_(
    "HCQOTBoilerSimulator", cg.PollingComponent
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HCQOTBoilerSimulator),
            cv.Required(CONF_IN_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_OUT_PIN): pins.internal_gpio_output_pin_number,
            cv.Optional(CONF_ENABLED, True): cv.boolean,
            cv.Optional(CONF_RESPONSE_ENABLED, True): cv.boolean,
            cv.Optional(CONF_RESPONSE_DELAY_MS, 30): cv.int_range(min=20, max=700),
            cv.Optional(CONF_DHW_PRESENT, True): cv.boolean,
            cv.Optional(CONF_CONTROL_TYPE_MODULATING, True): cv.boolean,
            cv.Optional(CONF_COOLING_SUPPORTED, False): cv.boolean,
            cv.Optional(CONF_MEMBER_ID, 1): cv.int_range(min=0, max=255),
            cv.Optional(CONF_MAX_CAPACITY_KW, 20): cv.int_range(min=0, max=255),
            cv.Optional(CONF_MIN_MODULATION_LEVEL, 20): cv.int_range(min=0, max=100),
            cv.Optional(CONF_SLAVE_OT_VERSION, 2.2): cv.float_range(min=0.0, max=127.0),
            cv.Optional(CONF_PRODUCT_TYPE, 1): cv.int_range(min=0, max=255),
            cv.Optional(CONF_PRODUCT_VERSION, 1): cv.int_range(min=0, max=255),
        }
    ).extend(cv.polling_component_schema("500ms")),
    cv.only_on_esp32,
)


def _final_validate(config):
    if CORE.target_framework != "esp-idf":
        raise cv.Invalid(
            "hcq_ot_boiler_simulator requires esp32.framework.type: esp-idf"
        )

    variant = esp32.get_esp32_variant()
    if variant in esp32_rmt.VARIANTS_NO_RMT:
        raise cv.Invalid(
            f"hcq_ot_boiler_simulator requires ESP32 RMT support; '{variant}' has no RMT"
        )
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
    cg.add(var.set_response_enabled(config[CONF_RESPONSE_ENABLED]))
    cg.add(var.set_response_delay_ms(config[CONF_RESPONSE_DELAY_MS]))
    cg.add(var.set_dhw_present(config[CONF_DHW_PRESENT]))
    cg.add(var.set_control_type_modulating(config[CONF_CONTROL_TYPE_MODULATING]))
    cg.add(var.set_cooling_supported(config[CONF_COOLING_SUPPORTED]))
    cg.add(var.set_member_id(config[CONF_MEMBER_ID]))
    cg.add(var.set_max_capacity_kw(config[CONF_MAX_CAPACITY_KW]))
    cg.add(var.set_min_modulation_level(config[CONF_MIN_MODULATION_LEVEL]))
    cg.add(var.set_slave_ot_version(config[CONF_SLAVE_OT_VERSION]))
    cg.add(var.set_product_type(config[CONF_PRODUCT_TYPE]))
    cg.add(var.set_product_version(config[CONF_PRODUCT_VERSION]))
