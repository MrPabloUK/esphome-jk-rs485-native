# esphome-jk-rs485-native

ESPHome **external components** that make the ESP the **only RS485 master** on a JK native dump bus.

| Platform | Role |
|---|---|
| `jk_rs485_native_master` | UART hub: dump/ACK parser, one-in-flight scheduler, DE/RE, write queue |
| `jk_rs485_native_bms` | Pack decode + sensors / numbers / switches / text / binary entities |

**“Native”** means JK’s proprietary dump protocol: trigger `16 20` / `16 1E` / `16 1C`, reply `55 AA EB 90` (308 bytes). This is **not** ESPHome `modbus_controller` / FC03 holding-register Modbus.

## Native dump vs Modbus

- **Use these components** when the pack speaks the JK dump on RS485-2 (typical JK-PB and other lines that use the same dump).
- **Do not use these components** if the BMS only exposes standard Modbus holding registers. Use syssi-style `modbus` + `modbus_controller` instead.
- **Do not load both** this repo and [txubelaxu/esphome-jk-bms](https://github.com/txubelaxu/esphome-jk-bms) on the same ESP. The YAML keys collide in intent (`jk_rs485_bms` vs `jk_rs485_native_bms`) and two masters on one bus will fight.

## Naming vs txubelaxu

| This repo | txubelaxu |
|---|---|
| `jk_rs485_native_master` | `jk_rs485_sniffer` |
| `jk_rs485_native_bms` | `jk_rs485_bms` |
| `master_id` | `jk_rs485_sniffer_id` |
| `address` | `rs485_address` |

YAML is **not** drop-in. Entity names and decode are ported; platform ids are new.

## Hardware / DIP

- UART **115200**, `esp-idf`, ESPHome **2026.9+**.
- TTL-to-RS485 with **DE/RE** on `flow_control_pin`.
- Set every pack DIP to **`0x01`–`0x0F`**. Do **not** leave a JK at `0x00` (that would be a competing master). Max 15 dump slaves.
- One ESP master per bus. Do not share this UART with a Solis inverter.

## Loop stack (two packs, `cell_info: 1s`)

Hot path is a round-robin cell dump (`0x02`). Settings (`0x01`) every 30 s, device info (`0x03`) at boot then hourly. Writes preempt the ring; after ACK a settings verify poll runs for that address only. Offline: first 3 misses stay on the ring, then retry every `offline_retry_interval` (default 5 s).

## Logger

Keep `logger: level: INFO` in production. Hex dumps are gated behind `debug_dump_frames: true` for bench fixture capture.

## Example

See [`examples/esp32-jk-native-master.yaml`](examples/esp32-jk-native-master.yaml) (two packs, full entity surface). Minimal skeleton:

```yaml
external_components:
  - source: github://MrPabloUK/esphome-jk-rs485-native@main
    components: [jk_rs485_native_master, jk_rs485_native_bms]

logger:
  level: INFO

uart:
  id: uart_jk
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 115200
  rx_buffer_size: 512

jk_rs485_native_master:
  id: jk_bus
  uart_id: uart_jk
  flow_control_pin: GPIO4
  response_timeout: 200ms
  inter_byte_timeout: 30ms
  offline_retry_interval: 5s
  min_command_gap: 150ms
  turnaround: 25ms
  poll:
    cell_info: 1s
    settings: 30s
    device_info: 1h

jk_rs485_native_bms:
  - id: bms1
    address: 0x01
    master_id: jk_bus
    cell_count: 16
  - id: bms2
    address: 0x02
    master_id: jk_bus
    cell_count: 16
```

`cell_count` is a YAML loop-bound hint. The settings dump still publishes the BMS-reported count.

## License

Apache-2.0. Decode/entity code ported from txubelaxu/syssi; see `NOTICE`.
