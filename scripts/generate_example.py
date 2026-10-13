"""Generate examples/esp32-jk-native-master.yaml from component entity keys."""
from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
BMS = ROOT / "components" / "jk_rs485_native_bms"
OUT = ROOT / "examples" / "esp32-jk-native-master.yaml"

HOT_SENSORS = {
    "battery_voltage",
    "battery_current",
    "battery_power",
    "battery_power_charging",
    "battery_power_discharging",
    "battery_capacity_state_of_charge",
    "status_charging",
    "status_discharging",
    "status_precharging",
    "status_balancing",
    "status_heating",
}


def conf_map(text: str) -> dict[str, str]:
    out = {}
    for match in re.finditer(r'(CONF_[A-Z0-9_]+)\s*=\s*"([^"]+)"', text):
        out[match.group(1)] = match.group(2)
    return out


def list_from(text: str, name: str, mapping: dict[str, str]) -> list[str]:
    block = re.search(rf"{name}\s*=\s*\[(.*?)\]", text, re.S)
    if not block:
        return []
    keys = re.findall(r"(CONF_[A-Z0-9_]+)", block.group(1))
    return [mapping[k] for k in keys if k in mapping]


def dict_keys(text: str, name: str, mapping: dict[str, str]) -> list[str]:
    block = re.search(rf"{name}\s*=\s*\{{(.*?)\n\}}", text, re.S)
    if not block:
        return []
    keys = re.findall(r"(CONF_[A-Z0-9_]+)\s*:", block.group(1))
    return [mapping[k] for k in keys if k in mapping]


def entity_block(key: str, pack: str, kind: str) -> str:
    name = f"{pack} {key.replace('_', ' ')}"
    lines = [f"    {key}:", f'      name: "{name}"']
    if kind == "cell_v":
        lines += [
            "      filters:",
            "        - delta: 0.002",
            "        - heartbeat: 10s",
        ]
    elif kind == "throttle":
        lines += [
            "      filters:",
            "        - throttle: 30s",
        ]
    return "\n".join(lines)


def pack_entities(pack: str, bms_id: str) -> str:
    sensor_py = (BMS / "sensor.py").read_text(encoding="utf-8")
    smap = conf_map(sensor_py)
    cells = list_from(sensor_py, "CELL_VOLTAGES", smap)
    resists = list_from(sensor_py, "CELL_RESISTANCES", smap)
    temps = list_from(sensor_py, "TEMPERATURES", smap)
    sensors = list_from(sensor_py, "SENSORS", smap)

    bin_py = (BMS / "binary_sensor.py").read_text(encoding="utf-8")
    bmap = conf_map(bin_py)
    binaries = list_from(bin_py, "BINARY_SENSORS", bmap)

    text_py = (BMS / "text_sensor.py").read_text(encoding="utf-8")
    tmap = conf_map(text_py)
    texts = list_from(text_py, "TEXT_SENSORS", tmap)

    sw_py = (BMS / "switch" / "__init__.py").read_text(encoding="utf-8")
    swmap = conf_map(sw_py)
    # also from const.py
    const_py = (BMS / "const.py").read_text(encoding="utf-8")
    swmap.update(conf_map(const_py))
    switches = dict_keys(sw_py, "SWITCHES", swmap)

    num_py = (BMS / "number" / "__init__.py").read_text(encoding="utf-8")
    nmap = conf_map(num_py)
    numbers = dict_keys(num_py, "NUMBERS", nmap) if "NUMBERS" in num_py else dict_keys(num_py, "SWITCHES", nmap)
    # number file uses unnamed dict assigned after comments — grab CONF_ keys before JK_RS485_NUMBER
    if not numbers:
        block = re.search(r"(CONF_CELL_SMART_SLEEP_VOLTAGE:.*?)JkRS485NativeBmsNumber", num_py, re.S)
        if block:
            keys = re.findall(r"(CONF_[A-Z0-9_]+)\s*:", block.group(1))
            numbers = [nmap[k] for k in keys if k in nmap]

    def platform_block(keys: list[str], kind_fn) -> str:
        lines = [
            "  - platform: jk_rs485_native_bms",
            f"    jk_rs485_native_bms_id: {bms_id}",
        ]
        for key in keys:
            lines.append(entity_block(key, pack, kind_fn(key)))
        return "\n".join(lines)

    return {
        "sensor": platform_block(
            cells + resists + temps + sensors,
            lambda key: "cell_v"
            if key in cells
            else ("hot" if key in HOT_SENSORS else "throttle"),
        ),
        "binary_sensor": platform_block(
            binaries,
            lambda key: "hot" if key in HOT_SENSORS or key.startswith("status_") else "throttle",
        ),
        "text_sensor": platform_block(texts, lambda _key: "hot"),
        "switch": platform_block(switches, lambda _key: "hot"),
        "number": platform_block(numbers, lambda _key: "hot"),
    }


def header() -> str:
    return """substitutions:
  name: jk-native-master
  tx_pin: GPIO17
  rx_pin: GPIO16
  flow_control_pin: GPIO4

esphome:
  name: ${name}
  friendly_name: JK RS485 Native Master

esp32:
  board: esp32dev
  framework:
    type: esp-idf

logger:
  level: INFO

api:
ota:
  - platform: esphome

wifi:
  ssid: "jk-native-ci"
  password: "jk-native-ci"

external_components:
  - source:
      type: local
      path: ../components
    components: [jk_rs485_native_master, jk_rs485_native_bms]

uart:
  id: uart_jk
  tx_pin: ${tx_pin}
  rx_pin: ${rx_pin}
  baud_rate: 115200
  rx_buffer_size: 512

jk_rs485_native_master:
  id: jk_bus
  uart_id: uart_jk
  flow_control_pin: ${flow_control_pin}
  response_timeout: 200ms
  inter_byte_timeout: 30ms
  offline_retry_interval: 5s
  min_command_gap: 150ms
  turnaround: 25ms
  debug_dump_frames: false
  poll:
    cell_info: 1s
    settings: 30s
    device_info: 1h

jk_rs485_native_bms:
  - id: bms1
    address: 0x01
    master_id: jk_bus
    protocol_version: JK02_32S
    cell_count: 16
  - id: bms2
    address: 0x02
    master_id: jk_bus
    protocol_version: JK02_32S
    cell_count: 16

"""


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    one = pack_entities("Pack1", "bms1")
    two = pack_entities("Pack2", "bms2")
    sections = []
    for key in ("sensor", "binary_sensor", "text_sensor", "switch", "number"):
        sections.append(f"{key}:\n{one[key]}\n{two[key]}")
    OUT.write_text(header() + "\n\n".join(sections) + "\n", encoding="utf-8")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
