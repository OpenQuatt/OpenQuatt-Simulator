import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import modbus, uart
from esphome.const import CONF_FLOW_CONTROL_PIN, CONF_ID
from esphome.cpp_helpers import gpio_pin_expression

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["modbus"]

CONF_HUB_ID = "hub_id"
CONF_ODU_1_ADDRESS = "odu_1_address"
CONF_ODU_2_ADDRESS = "odu_2_address"

quatt_odu_ns = cg.esphome_ns.namespace("quatt_odu_simulator")
QuattOduSimulator = quatt_odu_ns.class_("QuattOduSimulator", cg.PollingComponent)
QuattOduModbusHub = quatt_odu_ns.class_("QuattOduModbusHub", modbus.Modbus)


def _server_address(value):
    return cv.int_range(min=1, max=247)(value)


def _different_addresses(config):
    if config[CONF_ODU_1_ADDRESS] == config[CONF_ODU_2_ADDRESS]:
        raise cv.Invalid("ODU 1 and ODU 2 must use different Modbus addresses")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(QuattOduSimulator),
            cv.GenerateID(CONF_HUB_ID): cv.declare_id(QuattOduModbusHub),
            cv.Optional(CONF_ODU_1_ADDRESS, default=1): _server_address,
            cv.Optional(CONF_ODU_2_ADDRESS, default=2): _server_address,
            cv.Required(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.polling_component_schema("250ms"))
    .extend(uart.UART_DEVICE_SCHEMA),
    _different_addresses,
)


async def to_code(config):
    simulator = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(simulator, config)

    hub = cg.new_Pvariable(config[CONF_HUB_ID])
    await cg.register_component(hub, {})
    await uart.register_uart_device(hub, config)
    flow_control_pin = await gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
    cg.add(hub.set_flow_control_pin(flow_control_pin))
    cg.add(hub.set_parent(simulator))
    cg.add(simulator.set_hub(hub))
    cg.add(simulator.set_address(0, config[CONF_ODU_1_ADDRESS]))
    cg.add(simulator.set_address(1, config[CONF_ODU_2_ADDRESS]))
