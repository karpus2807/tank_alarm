# Connection diagrams

Power: **USB** dono ESP32 boards pe. Ethernet sirf data — normal switch/router → W5500 RJ45.

```
                    ┌─────────────────┐
                    │  LAN switch     │
                    └────────┬────────┘
              CAT6           │           CAT6
         ┌───────────────────┴───────────────────┐
         ▼                                       ▼
      W5500 #1                                W5500 #2
         │                                       │
   ┌─────▼──────────┐   UDP :5005        ┌───────▼────────┐
   │  TANK NODE     │ ─────────────────► │  ALARM NODE    │
   │  ESP32-S3      │     JSON status    │  ESP32 DevKit  │
   │  USB power     │                    │  USB power     │
   │  JSN-SR04T     │                    │  OLED + buzzer │
   └────────────────┘                    └────────────────┘
```

---

## 1) Tank node — ESP32-S3 + W5500 + JSN-SR04T

### Power
| From | To |
|------|-----|
| USB cable | ESP32-S3 USB port |
| ESP32-S3 `5V` | W5500 `VCC` (agar module 5V rated) **ya** `3.3V` agar board 3.3V-only |
| ESP32-S3 `GND` | W5500 `GND`, JSN-SR04T `GND` |
| ESP32-S3 `5V` | JSN-SR04T `VCC` (**5V required**) |

> W5500 modules vary: kuch `5V` pin pe onboard regulator hota hai, kuch sirf 3.3V. Apne module silkscreen check karo.  
> Phone charger / power-bank USB OK; unstable PC port se brownout aa sakta hai agar W5500 + sensor saath hon.

### W5500 SPI (ESP32-S3)

| W5500 | ESP32-S3 |
|-------|----------|
| MOSI  | GPIO11 |
| MISO  | GPIO13 |
| SCK   | GPIO12 |
| CS    | GPIO10 |
| RST   | GPIO14 |
| INT   | GPIO9 (optional, code mein unused OK) |
| GND   | GND |
| VCC   | 5V or 3.3V (module ke hisaab se) |
| RJ45  | LAN switch / router |

### JSN-SR04T ultrasonic

Module pe aksar **ek hi pin pe do naam** chhapte hain:

| Silkscreen | Matlab | Hamara mode (HC-SR04 style) |
|------------|--------|------------------------------|
| **Trig / RX** | trigger *ya* UART RX | → ESP32 **Trig** (GPIO6) |
| **Echo / TX** | echo *ya* UART TX | → ESP32 **Echo** (GPIO7 + divider) |

Hum **Mode 1 (pulse Trig/Echo)** use kar rahe hain — factory default. **Trig** / **Echo** follow karo; RX/TX ignore.

| JSN-SR04T | ESP32-S3 | Note |
|-----------|----------|------|
| VCC | 5V | waterproof probe cable tank pe |
| GND | GND | |
| **Trig** (same pad as RX) | GPIO6 | ESP → sensor |
| **Echo** (same pad as TX) | GPIO7 via divider | sensor → ESP; **5V tolerant nahi** |

Echo level shift (resistive divider ≈ 3.3V):

```
JSN Echo/TX ──┬── 2.2kΩ ──► GPIO7
              │
             3.3kΩ
              │
             GND
```

**UART mode (RX/TX) abhi mat use karo.** Current firmware Trig + `pulseIn(Echo)` pe hai.

Probe mounting: tank **top lid**, water surface facing. Empty distance is about **140 cm** and full distance is about **25 cm**. Set both on the tank page, then calibrate with the current fill percent. Do not reflash to change them.

### Level sensor (separate from percent)

| Wire | ESP32-S3 |
|------|----------|
| brown | 5V |
| blue | GND |
| yellow | GPIO4, `INPUT_PULLDOWN`, HIGH = water |
| black | leave open |

A wet sensor shows `1` under the percent. It does not force the percent to 100.

---

## 2) Alarm node — ESP32 DevKit V1 + W5500 + OLED + buzzer

### Power
| From | To |
|------|-----|
| USB cable | DevKit USB port |
| DevKit `5V` / `3.3V` | W5500 VCC (module ke hisaab se), peripherals |
| `GND` | sab modules common GND |
| OLED VCC | **3.3V only** |

### W5500 SPI (DevKit V1 / ESP32-WROOM)

| W5500 | ESP32 |
|-------|-------|
| MOSI  | GPIO23 |
| MISO  | GPIO19 |
| SCK   | GPIO18 |
| CS    | GPIO4 (NOT GPIO5 / GPIO15 — strapping pins) |
| RST   | GPIO17 |
| INT   | GPIO16 (optional) |
| GND   | GND |
| VCC   | 5V or 3.3V |
| RJ45  | LAN switch / router |

### OLED 1.3" I2C (SH1106 128×64)

| OLED | ESP32 |
|------|-------|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

Text is rotated 90 degrees. The tank graphic keeps its neck on the right. The waiting screen prints the IP horizontally because a full address does not fit rotated.

### Buzzer, relay, touch

| Device | ESP32 | Note |
|--------|-------|------|
| Buzzer VCC / GND | module supply / GND | 3-pin module |
| Buzzer I/O | GPIO26 | LOW = sound, HIGH = quiet |
| Relay `IN` | GPIO27 | HIGH during an unmuted 80/90/95/level alarm |
| Relay `VCC`/`GND` | 5V / GND | |
| TTP223 VCC | 3.3V | |
| TTP223 GND | GND | |
| TTP223 I/O | GPIO33 | touch reads HIGH. GPIO32 is also accepted |

Hold the touch for 2 seconds to mute or unmute.

---

## 3) Network

Open `http://<ip>:5005`. The hotspot stays up even after a mode is saved.

| Device | Hotspot | Hotspot page | LAN fallback |
|--------|---------|--------------|--------------|
| Tank | `tank` | `http://192.168.4.1:5005` | `192.168.50.50` |
| Alarm | `alarm` | `http://192.168.5.1:5005` | `192.168.50.51` |

LAN mode uses DHCP first, including a `10.x` network, and falls back after 30 seconds. Wi-Fi mode and hotspot mode do not start the W5500. Both boards must be on the same network only when that is the saved data path: both LAN, both Wi-Fi, or alarm joined to the tank hotspot. HTTP routes are in [api.md](api.md).

---

## 4) Wire checklist pehle flash se pehle

- [ ] Dono boards USB se powered
- [ ] W5500 VCC voltage correct for that module
- [ ] Echo divider present on tank node
- [ ] Common GND across ESP + W5500 + sensors
- [ ] CAT6 switch → W5500 RJ45 (not into ESP USB)
- [ ] OLED only 3.3V VCC
