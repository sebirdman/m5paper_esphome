import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor, touchscreen
from esphome.const import (
    CONF_ID
)
from esphome import pins

DEPENDENCIES = ['i2c']

CONF_I2C_ADDR = 0x5D
CONF_INTERRUPT_PIN = "interrupt_pin"

gt911 = cg.esphome_ns.namespace('gt911')
GT911 = gt911.class_('GT911', touchscreen.Touchscreen, cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = touchscreen.TOUCHSCREEN_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(GT911),
    cv.Required(CONF_INTERRUPT_PIN): cv.All(
                pins.internal_gpio_input_pin_schema
            ),
}).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(CONF_I2C_ADDR))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    await touchscreen.register_touchscreen(var, config)

    interrupt_pin = await cg.gpio_pin_expression(config[CONF_INTERRUPT_PIN])
    cg.add(var.set_interrupt_pin(interrupt_pin))

    