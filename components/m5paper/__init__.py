import esphome.codegen as cg
from esphome import pins
import esphome.config_validation as cv
from esphome import automation
from esphome.components import sensor
from esphome.const import (
    CONF_ID, 
    DEVICE_CLASS_VOLTAGE,
    CONF_BATTERY_VOLTAGE,
    UNIT_VOLT,
    STATE_CLASS_MEASUREMENT,
)

m5paper_ns = cg.esphome_ns.namespace('m5paper')

M5PaperComponent = m5paper_ns.class_('M5PaperComponent', cg.PollingComponent)
PowerAction = m5paper_ns.class_("PowerAction", automation.Action)

CONF_MAIN_POWER_PIN = "main_power_pin"
CONF_BATTERY_POWER_PIN = "battery_power_pin"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(M5PaperComponent),
    cv.Required(CONF_MAIN_POWER_PIN): pins.gpio_output_pin_schema,
    cv.Required(CONF_BATTERY_POWER_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    )
}).extend(cv.polling_component_schema('60s'))

@automation.register_action(
    "m5paper.shutdown_main_power",
    PowerAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(M5PaperComponent),
        }
    ),
)
async def m5paper_shutdown_main_power_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_MAIN_POWER_PIN in config:
        power = await cg.gpio_pin_expression(config[CONF_MAIN_POWER_PIN])
        cg.add(var.set_main_power_pin(power))
    if CONF_BATTERY_POWER_PIN in config:
        power = await cg.gpio_pin_expression(config[CONF_BATTERY_POWER_PIN])
        cg.add(var.set_battery_power_pin(power))
    if CONF_BATTERY_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage(sens))