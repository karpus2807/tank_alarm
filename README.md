# ESP32 Tank Fill Alarm

Rooftop tank gauge and alarm. The tank board measures the water. The alarm board shows it and sounds the buzzer. Setup is a web page on each board.

## Hardware

| Node | Board | What is connected |
|------|--------|-------------------|
| Tank | ESP32-S3 | JSN-SR04T on the lid, pipe level sensor, W5500 |
| Alarm | ESP32 DevKit V1 | 1.3" SH1106 OLED, active-low buzzer, relay, touch mute, W5500 |

Power both boards from USB. The LAN cable goes into the W5500, not the ESP USB port.

Wiring: [docs/WIRING-PRINT.md](docs/WIRING-PRINT.md), [docs/connections.md](docs/connections.md), [docs/pin-table.md](docs/pin-table.md).  
HTTP API: [docs/api.md](docs/api.md).

## How they connect

Each board keeps its own hotspot up:

| Device | Hotspot | Page |
|--------|---------|------|
| Tank | `tank` | `http://192.168.4.1:5005` |
| Alarm | `alarm` | `http://192.168.5.1:5005` |

Use `http` and port **5005**. Port 80 is closed. The hotspot password is `LINK_PASS` in `firmware/shared/app_config.h`.

Save a mode on the page and the board reboots into it.

| Tank mode | Data path |
|-----------|-----------|
| Home Wi-Fi | Tank joins the saved network and sends status there |
| LAN | W5500 DHCP, then `192.168.50.50` if DHCP does not answer in 30 seconds |
| Hotspot | Alarm joins `tank` |

The alarm page is LAN or Wi-Fi only. A wrong Wi-Fi password does not close the hotspot. When Wi-Fi or LAN gets a gateway, the tank hotspot shares that internet.

Level packets are UDP JSON on port **5005**. The alarm beeps from the tank percent. It stays quiet while the tank marks the reading unsteady.

## Firmware

```bash
cd firmware/tank-node
pio run -t upload

cd firmware/alarm-node
pio run -t upload
```

Tank upload port is `/dev/ttyACM0`. Alarm upload port is `/dev/ttyUSB0`.

The tank page can also flash `firmware/tank-node/.pio/build/tank-s3/firmware.bin` with **Firmware update**. That file is the application image, not a full flash dump.

## Level

The ultrasonic sensor faces the water from the lid. A higher level is a shorter distance. Percent is `(empty − distance) / (empty − full) × 100`. For this tank, empty is about **140 cm** and full is about **25 cm**. Set them on the tank page, then calibrate with the current fill percent. The level sensor on GPIO4 is a separate wet/dry digit and does not force the percent to 100.

Default beeps, edited on the tank page: 10% two short beeps five times, 80% two short beeps once, 90% three short beeps once, 95% a long beep until mute, and a solid beep while the level sensor is wet. Hold the alarm touch sensor for 2 seconds to mute or unmute.
