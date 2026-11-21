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
| 7 | OLED 0.91" I2C (SSD1306) | Alarm | Display |
| 8 | Active buzzer | Alarm | Local beep |
| 9 | Relay module (optional) | Alarm | Old siren 2-pin short |
| 10 | Mute button (optional) | Alarm | Silence toggle |
| 11 | USB supply | Alarm | Phone charger / adapter |

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
| OLED 0.91" | VCC | **3.3V** | not 5V |
| OLED 0.91" | GND | `GND` | |
| OLED 0.91" | SDA | **GPIO21** | I2C addr `0x3C` |
| OLED 0.91" | SCL | **GPIO22** | |
| Buzzer | + | **GPIO26** | transistor if high current |
| Buzzer | − | `GND` | |
| Relay IN | IN | **GPIO27** | optional old alarm |
| Relay | VCC / GND | `5V` / `GND` | |
| Mute button | one leg | **GPIO32** | other leg → GND |

---

## C) Network (`firmware/shared/network.h`)

Permanent ESP↔ESP LAN — **laptop gateway not required**.  
Dono W5500 **same switch** pe (ya direct cable). Laptop optional debug ke liye switch pe.

| Device | IP | UDP |
|--------|-----|-----|
| Tank ESP32-S3 | `192.168.50.50` | sends → `192.168.50.51:5005` |
| Alarm DevKit | `192.168.50.51` | listens **5005** |
| Subnet | `255.255.255.0` | |

---

## D) GPIO summary

### ESP32-S3 (tank)
| GPIO | Function |
|------|----------|
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
| 26 | Buzzer |
| 27 | Relay |
| 32 | Mute button |
