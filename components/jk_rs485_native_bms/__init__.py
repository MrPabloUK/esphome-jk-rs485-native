import esphome.codegen as cg
from esphome.components import jk_rs485_native_master
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ID

CONF_PROTOCOL_VERSION = "protocol_version"
import esphome.final_validate as fv

from . import validation as v

CODEOWNERS = ["@MrPabloUK", "@txubelaxu", "@syssi"]
DEPENDENCIES = ["jk_rs485_native_master"]
AUTO_LOAD = ["binary_sensor", "sensor", "switch", "text_sensor", "number"]
MULTI_CONF = True

CONF_JK_RS485_NATIVE_BMS_ID = "jk_rs485_native_bms_id"
CONF_MASTER_ID = "master_id"
CONF_CELL_COUNT = "cell_count"

jk_rs485_native_bms_ns = cg.esphome_ns.namespace("jk_rs485_native_bms")
JkRS485NativeBms = jk_rs485_native_bms_ns.class_("JkRS485NativeBms", cg.PollingComponent)

ProtocolVersion = jk_rs485_native_bms_ns.enum("ProtocolVersion")
PROTOCOL_VERSIONS = {
    "JK02_24S": ProtocolVersion.PROTOCOL_VERSION_JK02_24S,
    "JK02_32S": ProtocolVersion.PROTOCOL_VERSION_JK02_32S,
}

JK_RS485_NATIVE_BMS_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_JK_RS485_NATIVE_BMS_ID): cv.use_id(JkRS485NativeBms),
    }
)


def _address(value):
    value = cv.int_(value)
    err = v.validate_address(value)
    if err:
        raise cv.Invalid(err)
    return value


def _cell_count_for_protocol(config):
    err = v.validate_cell_count(config.get(CONF_CELL_COUNT), config.get(CONF_PROTOCOL_VERSION))
    if err:
        raise cv.Invalid(err)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JkRS485NativeBms),
            cv.Required(CONF_ADDRESS): _address,
            cv.Required(CONF_MASTER_ID): cv.use_id(jk_rs485_native_master.JkRS485NativeMaster),
            cv.Optional(CONF_PROTOCOL_VERSION, default="JK02_32S"): cv.enum(PROTOCOL_VERSIONS, upper=True),
            cv.Optional(CONF_CELL_COUNT): cv.int_range(min=1, max=32),
        }
    )
    .extend(cv.polling_component_schema("5s")),
    _cell_count_for_protocol,
)


def _final_validate(config):
    full = fv.full_config.get()
    entries = full.get("jk_rs485_native_bms")
    if entries is None:
        return config
    if not isinstance(entries, list):
        entries = [entries]
    normalized = []
    for entry in entries:
        master = entry.get(CONF_MASTER_ID)
        master_id = getattr(master, "id", master)
        normalized.append(
            {
                "address": entry.get(CONF_ADDRESS),
                "master_id": str(master_id),
                "protocol_version": str(entry.get(CONF_PROTOCOL_VERSION) or "JK02_32S"),
                "cell_count": entry.get(CONF_CELL_COUNT),
            }
        )
    errors = v.validate_entries(normalized)
    if errors:
        raise cv.Invalid("; ".join(errors))
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_protocol_version(config[CONF_PROTOCOL_VERSION]))
    hub = await cg.get_variable(config[CONF_MASTER_ID])
    cg.add(var.set_parent(hub))
    if CONF_CELL_COUNT in config:
        cg.add(var.set_configured_cell_count(config[CONF_CELL_COUNT]))
    cg.add(hub.register_device(var, config[CONF_ADDRESS]))
    if CONF_CELL_COUNT in config:
        cg.add(hub.set_configured_cell_count(config[CONF_ADDRESS], True))
