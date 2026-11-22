# Complete pin & device table

## Devices overview

| # | Device | Node | Role |
|---|--------|------|------|
| 1 | ESP32-S3 | Tank (roof) | Controller (USB power) |
| 2 | W5500 #1 | Tank | Ethernet |
| 3 | JSN-SR04T | Tank | Water level |
| 4 | USB supply | Tank | Phone charger / power bank / adapter |
| 5 | ESP32 DevKit V1 | Alarm (ground) | Controller (USB power) |
| 6 | W5500 #2 | Alarm | Ethernet |
| 7 | OLED 1.3" I2C (SH1106 128×64) | Alarm | Display |
| 8 | 3-pin active buzzer | Alarm | Sound when I/O is LOW |
| 9 | Relay module | Alarm | ON while the high alarm is unmuted |
| 10 | TTP223 touch | Alarm | 2 second hold toggles mute |
| 11 | Pipe level sensor | Tank | Wet = HIGH on GPIO4 |
| 12 | USB supply | Alarm | Phone charger / adapter |

---

## A) Tank node — ESP32-S3

| Device | Device pin | ESP32-S3 | Notes |
|--------|------------|----------|-------|
| USB | — | USB port | board power |
| LAN switch | RJ45 | W5500 RJ45 | data only |
| W5500 | VCC | `5V` or `3.3V` | check module marking |
| W5500 | GND | `GND` | |
| W5500 | MOSI | **GPIO11** | |
| W5500 | MISO | **GPIO13** | |
| W5500 | SCK | **GPIO12** | |
| W5500 | CS | **GPIO10** | |
| W5500 | RST | **GPIO14** | |
| W5500 | INT | GPIO9 | optional, unused in code |
| JSN-SR04T | VCC | **5V** | must be 5V |
| JSN-SR04T | GND | `GND` | |
| JSN-SR04T | Trig / RX | **GPIO6** | use as Trig |
| JSN-SR04T | Echo / TX | **GPIO7** | via 2.2k + 3.3k divider |
| Level sensor | brown | **5V** | supply |
| Level sensor | blue | `GND` | common ground |
| Level sensor | yellow | **GPIO4** | HIGH = water, LOW = dry. `INPUT_PULLDOWN` |
| Level sensor | black | open | do not connect |

### Echo divider

| From | Through | To |
|------|---------|-----|
| Echo/TX | 2.2kΩ series | GPIO7 |
| GPIO7 | 3.3kΩ to GND | GND |

---

## B) Alarm node — ESP32 DevKit V1

| Device | Device pin | ESP32 DevKit | Notes |
|--------|------------|--------------|-------|
| USB | — | USB port | board power |
| LAN switch | RJ45 | W5500 RJ45 | data only |
| W5500 | VCC | `5V` or `3.3V` | check module marking |
| W5500 | GND | `GND` | |
| W5500 | MOSI | **GPIO23** | |
| W5500 | MISO | **GPIO19** | |
| W5500 | SCK | **GPIO18** | |
| W5500 | CS | **GPIO4** | not GPIO5/15 (boot strap) |
| W5500 | RST | **GPIO17** | |
| W5500 | INT | GPIO16 | optional, unused in code |
| OLED 1.3" SH1106 | VCC | **3.3V** | not 5V |
| OLED 1.3" SH1106 | GND | `GND` | |
| OLED 1.3" SH1106 | SDA | **GPIO21** | |
| OLED 1.3" SH1106 | SCL | **GPIO22** | |
| Buzzer | VCC | `5V` or `3.3V` | check the module |
| Buzzer | GND | `GND` | |
| Buzzer | I/O | **GPIO26** | LOW = sound, HIGH = quiet |
| Relay IN | IN | **GPIO27** | HIGH while an unmuted 80/90/95/level alarm is active |
| Relay | VCC / GND | `5V` / `GND` | |
| TTP223 | VCC | **3.3V** | |
| TTP223 | GND | `GND` | |
| TTP223 | I/O | **GPIO33** | touch reads HIGH. GPIO32 is also accepted |

---

## C) Network

Pages and the HTTP API are `http://<ip>:5005`. UDP status uses the same port number. The hotspot stays up in every mode.

| Device | Hotspot | Hotspot IP | LAN fallback if DHCP fails |
|--------|---------|------------|----------------------------|
| Tank | `tank` | `192.168.4.1` | `192.168.50.50` |
| Alarm | `alarm` | `192.168.5.1` | `192.168.50.51` |

Programmed LAN MACs are `02:54:41:4E:4B:01` (tank) and `02:41:4C:52:4D:01` (alarm). DHCP on a `10.x` or `192.168.x` network is accepted. LAN mode is the only mode that asks the cable for an address. Wi-Fi mode and hotspot mode leave the W5500 idle.

---

## D) GPIO summary

### ESP32-S3 (tank)
| GPIO | Function |
|------|----------|
| 4 | Level sensor, HIGH = wet |
| 6 | Ultrasonic Trig |
| 7 | Ultrasonic Echo (divided) |
| 10 | W5500 CS |
| 11 | W5500 MOSI |
| 12 | W5500 SCK |
| 13 | W5500 MISO |
| 14 | W5500 RST |
| 9 | W5500 INT (optional) |

### ESP32 DevKit V1 (alarm)
| GPIO | Function |
|------|----------|
| 4 | W5500 CS |
| 16 | W5500 INT (optional) |
| 17 | W5500 RST |
| 18 | W5500 SCK |
| 19 | W5500 MISO |
| 21 | OLED SDA |
| 22 | OLED SCL |
| 23 | W5500 MOSI |
| 26 | Buzzer I/O, active low |
| 27 | Relay |
| 32 | Extra touch input, HIGH = pressed |
| 33 | TTP223 touch |
