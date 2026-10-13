import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@MrPabloUK"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True
AUTO_LOAD = []

CONF_JK_RS485_NATIVE_MASTER_ID = "jk_rs485_native_master_id"
CONF_MASTER_ID = "master_id"
CONF_FLOW_CONTROL_PIN = "flow_control_pin"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_INTER_BYTE_TIMEOUT = "inter_byte_timeout"
CONF_TURNAROUND = "turnaround"
CONF_MIN_COMMAND_GAP = "min_command_gap"
CONF_OFFLINE_RETRY_INTERVAL = "offline_retry_interval"
CONF_POLL = "poll"
CONF_CELL_INFO = "cell_info"
CONF_SETTINGS = "settings"
CONF_DEVICE_INFO = "device_info"
CONF_DEBUG_DUMP_FRAMES = "debug_dump_frames"
CONF_BROADCAST_CHANGES = "broadcast_changes_to_all_bms"

jk_rs485_native_master_ns = cg.esphome_ns.namespace("jk_rs485_native_master")
JkRS485NativeMaster = jk_rs485_native_master_ns.class_("JkRS485NativeMaster", cg.Component, uart.UARTDevice)

POLL_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_CELL_INFO, default="1s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SETTINGS, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_DEVICE_INFO, default="1h"): cv.positive_time_period_milliseconds,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JkRS485NativeMaster),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_RESPONSE_TIMEOUT, default="200ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_INTER_BYTE_TIMEOUT, default="30ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TURNAROUND, default="25ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MIN_COMMAND_GAP, default="150ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_OFFLINE_RETRY_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_POLL, default={}): POLL_SCHEMA,
            cv.Optional(CONF_DEBUG_DUMP_FRAMES, default=False): cv.boolean,
            cv.Optional(CONF_BROADCAST_CHANGES, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

JK_RS485_NATIVE_MASTER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MASTER_ID): cv.use_id(JkRS485NativeMaster),
    }
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))
    cg.add(var.set_response_timeout(config[CONF_RESPONSE_TIMEOUT].total_milliseconds))
    cg.add(var.set_inter_byte_timeout(config[CONF_INTER_BYTE_TIMEOUT].total_milliseconds))
    cg.add(var.set_turnaround(config[CONF_TURNAROUND].total_milliseconds))
    cg.add(var.set_min_command_gap(config[CONF_MIN_COMMAND_GAP].total_milliseconds))
    cg.add(var.set_offline_retry_interval(config[CONF_OFFLINE_RETRY_INTERVAL].total_milliseconds))
    poll = config[CONF_POLL]
    cg.add(var.set_cell_interval(poll[CONF_CELL_INFO].total_milliseconds))
    cg.add(var.set_settings_interval(poll[CONF_SETTINGS].total_milliseconds))
    cg.add(var.set_device_info_interval(poll[CONF_DEVICE_INFO].total_milliseconds))
    cg.add(var.set_debug_dump_frames(config[CONF_DEBUG_DUMP_FRAMES]))
    cg.add(var.set_broadcast_changes(config[CONF_BROADCAST_CHANGES]))
