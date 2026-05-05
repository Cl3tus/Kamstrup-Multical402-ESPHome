# Kamstrup Multical402 — ESPHome Custom Component

ESPHome custom component for reading Kamstrup Multical 402/403 heat meters via the KMP optical IR protocol. Uses a Hichi HB0015 USB IR read head connected to an ESP32-S3 via USB-OTG.

Reads 7 registers every 30 seconds (configurable) and exposes them as Home Assistant sensors.

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32-S3-N16R8 (16MB Flash, 8MB PSRAM) |
| Board | UICPAL ESP32-S3 DevKitC-1 variant |
| IR Read Head | Hichi HB0015 USB optical read head |
| USB-Serial chip | Silicon Labs CP2102 (VID: 10C4, PID: EA60) |
| Heat meter | Kamstrup Multical 402 / 403 |
| Connection | USB-OTG (ESP32-S3 acting as USB host) |

### Wiring

The Hichi HB0015 connects directly to the ESP32-S3 USB-OTG port (the port labeled USB on the board, not the UART/debug port). No additional wiring required — power and data run over USB.

The IR head is magnetically attached to the optical port on the front of the Kamstrup meter.

---

## Registers

| Register | Description | Unit |
|---|---|---|
| 0x003C | Heat Energy | GJ |
| 0x0056 | Supply Temperature | °C |
| 0x0057 | Return Temperature | °C |
| 0x0059 | Temperature Difference | °C |
| 0x0050 | Power | kW |
| 0x004A | Flow Rate | l/h |
| 0x0044 | Volume | m³ |

---

## Protocol

The KMP (Kamstrup Meter Protocol) is a proprietary half-duplex IR serial protocol:

- **Baud rate:** 1200 baud, 8N2
- **Physical layer:** IR optical, half-duplex
- **TX echo:** The meter reflects every byte sent back on the RX line — the component handles this automatically by reading and discarding the echo frame before parsing the response
- **Byte stuffing:** Special bytes (0x06, 0x0D, 0x1B, 0x40, 0x80) are escaped as `0x1B, byte ^ 0xFF`
- **CRC:** CRC-16/CCITT (polynomial 0x1021)
- **Frame format:** `[start] [dest] [cmd] [data...] [crc_hi] [crc_lo] [stop]`
  - Start TX: `0x80`, Start RX: `0x40`, Stop: `0x0D`

### Timing

The component uses ESPHome's non-blocking `set_timeout()` to read the response 1.5 seconds after sending the request. This is necessary because the ESP32-S3 USB host stack runs in a separate FreeRTOS task — actively polling for data blocks the USB task and prevents bytes from being delivered. The 1.5 second gap allows the USB task to fully receive all response bytes before the component reads from the buffer.

---

## File Structure

```
components/
└── multical402/
    ├── __init__.py       # Empty — required by ESPHome
    ├── sensor.py         # ESPHome component definition and YAML schema
    ├── multical402.h     # Main component class (PollingComponent)
    └── kmp.h             # KMP protocol implementation
```

---

## Setup

### 1. Copy the component

Copy the `components/` folder into your ESPHome config directory:

```
/config/esphome/
├── components/
│   └── multical402/
│       ├── __init__.py
│       ├── sensor.py
│       ├── multical402.h
│       └── kmp.h
└── your-device.yaml
```

### 2. Add to your YAML

```yaml
external_components:
  - source:
      type: local
      path: components

usb_host:
  - id: usb_host_bus

usb_uart:
  - type: cp210x
    usb_host_id: usb_host_bus
    channels:
      - id: uart_kamstrup
        baud_rate: 1200
        stop_bits: 2
        buffer_size: 4096

sensor:
  - platform: multical402
    uart_id: uart_kamstrup
    update_interval: 30s
    heat_energy:
      name: "Heat Energy"
      force_update: true
    supply_temperature:
      name: "Supply Temperature"
      force_update: true
    return_temperature:
      name: "Return Temperature"
      force_update: true
    temperature_difference:
      name: "Temperature Difference"
      force_update: true
    power:
      name: "Power"
      force_update: true
    flow_rate:
      name: "Flow Rate"
      force_update: true
    volume:
      name: "Volume"
      force_update: true
```

### 3. Clean build files

After copying the component files, clean the ESPHome build cache before compiling:

```bash
rm -rf /data/build/<your-device-name>
```

Or use **Clean Build Files** in the ESPHome dashboard. This ensures ESPHome picks up the new `sensor.py` instead of a cached version.

### 4. Flash

Compile and flash via the ESPHome dashboard or CLI:

```bash
esphome run your-device.yaml
```

---

## YAML sensor options

All sensor properties (`unit_of_measurement`, `device_class`, `state_class`, `icon`, `accuracy_decimals`) are pre-configured in `sensor.py`. You only need to provide `name` and optionally `force_update` in your YAML.

| YAML key | Sensor | Unit | Device Class |
|---|---|---|---|
| `heat_energy` | Heat Energy | GJ | energy |
| `supply_temperature` | Supply Temperature | °C | temperature |
| `return_temperature` | Return Temperature | °C | temperature |
| `temperature_difference` | Temperature Difference | °C | temperature |
| `power` | Power | kW | power |
| `flow_rate` | Flow Rate | l/h | — |
| `volume` | Volume | m³ | — |

The `update_interval` defaults to `30s` but can be set to any value in the YAML (e.g. `60s`, `5min`).

---

## Battery consumption

The Kamstrup Multical meter runs on an internal battery. Every IR read wakes the meter's optical interface and consumes battery. With `update_interval: 30s` the meter is polled 2880 times per day. Consider using a longer interval (e.g. `60s` or `5min`) if battery life is a concern.

Kamstrup states the battery should last 10+ years under normal conditions but frequent IR polling can reduce this. See the [PyKMP battery consumption page](https://gertvdijk.github.io/PyKMP/battery-consumption/) for more details.

---

## Acknowledgements

- [PyKMP](https://github.com/gertvdijk/PyKMP) by Gert van Dijk — KMP protocol documentation and reference implementation
- [ha-kamstrup_403](https://github.com/golles/ha-kamstrup_403) by golles — Python HA integration, useful reference for frame parsing
- [Hichi HB0015](https://www.hichi-wire.com) — USB IR optical read head hardware
- ESPHome USB UART component — for CP210x USB-serial support on ESP32-S3

---

## License

MIT
