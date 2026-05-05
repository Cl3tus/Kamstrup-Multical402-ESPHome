import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_KILOWATT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

multical402_ns = cg.esphome_ns.namespace("multical402")
Multical402 = multical402_ns.class_("Multical402", cg.PollingComponent, uart.UARTDevice)

# Custom units not available in ESPHome constants
UNIT_GIGAJOULE      = "GJ"
UNIT_LITER_PER_HOUR = "l/h"
UNIT_CUBIC_METER    = "m³"

# YAML config keys
CONF_HEAT_ENERGY     = "heat_energy"
CONF_SUPPLY_TEMP     = "supply_temperature"
CONF_RETURN_TEMP     = "return_temperature"
CONF_TEMP_DIFFERENCE = "temperature_difference"
CONF_POWER           = "power"
CONF_FLOW_RATE       = "flow_rate"
CONF_VOLUME          = "volume"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Multical402),
            cv.Optional(CONF_HEAT_ENERGY): sensor.sensor_schema(
                unit_of_measurement=UNIT_GIGAJOULE,
                icon="mdi:lightning-bolt",
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_SUPPLY_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon="mdi:thermometer-chevron-up",
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_RETURN_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon="mdi:thermometer-chevron-down",
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMP_DIFFERENCE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon="mdi:thermometer-lines",
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOWATT,
                icon="mdi:flash",
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_FLOW_RATE): sensor.sensor_schema(
                unit_of_measurement=UNIT_LITER_PER_HOUR,
                icon="mdi:waves-arrow-right",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_VOLUME): sensor.sensor_schema(
                unit_of_measurement=UNIT_CUBIC_METER,
                icon="mdi:gauge",
                accuracy_decimals=3,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_HEAT_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_HEAT_ENERGY])
        cg.add(var.set_energy_sensor(sens))

    if CONF_SUPPLY_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_SUPPLY_TEMP])
        cg.add(var.set_tempin_sensor(sens))

    if CONF_RETURN_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_RETURN_TEMP])
        cg.add(var.set_tempout_sensor(sens))

    if CONF_TEMP_DIFFERENCE in config:
        sens = await sensor.new_sensor(config[CONF_TEMP_DIFFERENCE])
        cg.add(var.set_tempdiff_sensor(sens))

    if CONF_POWER in config:
        sens = await sensor.new_sensor(config[CONF_POWER])
        cg.add(var.set_power_sensor(sens))

    if CONF_FLOW_RATE in config:
        sens = await sensor.new_sensor(config[CONF_FLOW_RATE])
        cg.add(var.set_flow_sensor(sens))

    if CONF_VOLUME in config:
        sens = await sensor.new_sensor(config[CONF_VOLUME])
        cg.add(var.set_volume_sensor(sens))
