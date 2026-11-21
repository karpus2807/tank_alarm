# ESP32 Tank Fill Alarm

Accurate rooftop plastic-tank fill alarm (~1.5 m tanks, ~70 m above ground) using ultrasonic level sensing and Ethernet.

## Hardware roles

| Node | Board | Peripherals |
|------|--------|-------------|
| **Tank** | ESP32-S3 | JSN-SR04T + W5500 |
| **Alarm** | ESP32 DevKit V1 | W5500 + 0.91" OLED + buzzer/relay |

Power: **USB** on both boards. LAN cable → W5500 only.

- **Printable wiring sheet:** [docs/WIRING-PRINT.md](docs/WIRING-PRINT.md)
- Details: [docs/connections.md](docs/connections.md), pins: [docs/pin-table.md](docs/pin-table.md).

## Firmware

PlatformIO projects:

```bash
# Tank node (ESP32-S3)
cd firmware/tank-node
pio run -t upload
pio device monitor

# Alarm node (DevKit V1)
cd firmware/alarm-node
pio run -t upload
pio device monitor
```

Edit IPs / tank height / thresholds in each `src/config.h` before flash.

## Protocol

Tank → Alarm UDP port **5005**:

```json
{"id":1,"cm":23.4,"pct":78.0,"alarm":0,"ok":1}
```

`alarm`: `0` OK, `1` WARN (≥90%), `2` FULL (≥95%) with hysteresis clear below 88%.

## First calibration

1. Mount JSN-SR04T on tank lid facing water.
2. Empty-ish: note distance → `TANK_EMPTY_CM`.
3. Full: note distance → `TANK_FULL_CM`.
4. Reflash tank node; confirm `%` on OLED.
