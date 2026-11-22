# ESP32 Tank Fill Alarm — Complete Wiring Sheet

**Print this page.** Keep one copy at the tank (roof) and one at the alarm (ground).

| | |
|--|--|
| **Project** | ESP32 Tank Fill Alarm |
| **Power** | USB on both ESP32 boards (phone charger / adapter OK) |
| **Data** | Ethernet CAT6 → W5500 RJ45 only (not into ESP USB) |
| **Link** | Tank → Alarm over LAN, UDP port **5005**, pair by MAC |

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

## 1. Parts list

| # | Part | Where | Notes |
|---|------|-------|-------|
| 1 | ESP32-S3 | Tank (roof) | Controller |
| 2 | W5500 Ethernet module | Tank | RJ45 → switch |
| 3 | JSN-SR04T ultrasonic | Tank | Waterproof probe on lid |
| 4 | 2.2 kΩ + 3.3 kΩ resistors | Tank | Echo voltage divider |
| 5 | USB 5V supply | Tank | Charger / power bank |
| 6 | ESP32 DevKit V1 | Alarm (ground) | Controller |
| 7 | W5500 Ethernet module | Alarm | RJ45 → switch |
| 8 | OLED 1.3" I2C SH1106 128×64 | Alarm | Display, 3.3V |
| 9 | 3-pin active buzzer | Alarm | I/O LOW = sound |
| 10 | Relay module | Alarm | HIGH on GPIO27 during high alarm |
| 11 | TTP223 touch | Alarm | I/O to GPIO33, 2 second mute |
| 12 | USB 5V supply | Alarm | Charger / adapter |
| 13 | Pipe level sensor | Tank | Yellow to GPIO4, HIGH = wet |
| 14 | LAN switch + CAT6 | Where a LAN mode is used | Same network for both boards |

---

## 2. TANK NODE — ESP32-S3 (roof)

### 2.1 Power

| From | To | Note |
|------|-----|------|
| USB cable | ESP32-S3 USB port | Main power |
| ESP32-S3 `5V` | JSN-SR04T `VCC` | Sensor needs **5V** |
| ESP32-S3 `5V` or `3.3V` | W5500 `VCC` | **Check module marking** |
| ESP32-S3 `GND` | W5500 `GND` + JSN `GND` | Common ground |

### 2.2 W5500 SPI

| W5500 pin | Wire to ESP32-S3 | Must |
|-----------|------------------|------|
| VCC | 5V or 3.3V (per module) | ✓ |
| GND | GND | ✓ |
| MOSI | **GPIO11** | ✓ |
| MISO | **GPIO13** | ✓ |
| SCK | **GPIO12** | ✓ |
| CS | **GPIO10** | ✓ |
| RST | **GPIO14** | ✓ |
| INT | GPIO9 | Optional (unused in FW) |
| RJ45 | LAN switch | ✓ |

### 2.3 JSN-SR04T ultrasonic

Module pads often show **two names on one pin**. Use **Trig / Echo** mode (not UART).

| Silkscreen on module | Use as | Wire to ESP32-S3 |
|----------------------|--------|------------------|
| **Trig / RX** | Trig | **GPIO6** |
| **Echo / TX** | Echo | **GPIO7** via divider below |
| VCC | Power | **5V** |
| GND | Ground | **GND** |

**Echo level shifter (required — ESP32 is not 5V tolerant):**

```
JSN Echo/TX ──── 2.2 kΩ ────┬──── GPIO7
                            │
                          3.3 kΩ
                            │
                           GND
```

| From | Through | To |
|------|---------|-----|
| Echo/TX | 2.2 kΩ series | GPIO7 |
| GPIO7 | 3.3 kΩ | GND |

Probe: mount on **tank top lid**, facing the water. Empty is about 140 cm. Full is about 25 cm. Calibrate on the tank page.

### 2.4 Level sensor

| Wire | ESP32-S3 | Note |
|------|----------|------|
| brown | 5V | supply |
| blue | GND | common ground |
| yellow | **GPIO4** | HIGH = water, LOW = dry |
| black | open | do not connect |

Wet reads `1` and the shown percent becomes 100. Dry leaves the percent to the ultrasonic sensor.

### 2.5 Tank GPIO quick list

| GPIO | Function |
|------|----------|
| 4 | Level sensor |
| 6 | Ultrasonic Trig |
| 7 | Ultrasonic Echo (divided) |
| 10 | W5500 CS |
| 11 | W5500 MOSI |
| 12 | W5500 SCK |
| 13 | W5500 MISO |
| 14 | W5500 RST |
| 9 | W5500 INT (optional) |

---

## 3. ALARM NODE — ESP32 DevKit V1 (ground)

### 3.1 Power

| From | To | Note |
|------|-----|------|
| USB cable | DevKit USB port | Main power |
| DevKit `5V` or `3.3V` | W5500 `VCC` | Check module marking |
| DevKit `3.3V` | OLED `VCC` | **OLED = 3.3V only — never 5V** |
| DevKit `GND` | All module GNDs | Common ground |

### 3.2 W5500 SPI

| W5500 pin | Wire to DevKit | Must |
|-----------|----------------|------|
| VCC | 5V or 3.3V (per module) | ✓ |
| GND | GND | ✓ |
| MOSI | **GPIO23** | ✓ |
| MISO | **GPIO19** | ✓ |
| SCK | **GPIO18** | ✓ |
| CS | **GPIO4** | ✓ **Do NOT use GPIO5 or GPIO15** (boot/strapping) |
| RST | **GPIO17** | ✓ |
| INT | GPIO16 | Optional (unused in FW) |
| RJ45 | LAN switch | ✓ |

### 3.3 OLED 1.3" (SH1106 128×64 I2C)

| OLED pin | Wire to DevKit | Must |
|----------|----------------|------|
| VCC | **3.3V** | ✓ |
| GND | GND | ✓ |
| SDA | **GPIO21** | ✓ |
| SCL | **GPIO22** | ✓ |

I2C address usually **0x3C**.

### 3.4 Buzzer / relay / touch

| Device | Pin | Wire to DevKit | Note |
|--------|-----|----------------|------|
| Buzzer VCC / GND | power | module supply / GND | 3-pin module |
| Buzzer I/O | I/O | **GPIO26** | LOW = sound |
| Relay IN | IN | **GPIO27** | HIGH during unmuted high alarm |
| Relay power | VCC / GND | 5V / GND | |
| TTP223 VCC | VCC | **3.3V** | |
| TTP223 GND | GND | GND | |
| TTP223 I/O | I/O | **GPIO33** | HIGH while touched. GPIO32 also accepted |

### 3.5 Alarm GPIO quick list

| GPIO | Function |
|------|----------|
| 4 | W5500 CS |
| 17 | W5500 RST |
| 18 | W5500 SCK |
| 19 | W5500 MISO |
| 23 | W5500 MOSI |
| 16 | W5500 INT (optional) |
| 21 | OLED SDA |
| 22 | OLED SCL |
| 26 | Buzzer I/O, active low |
| 27 | Relay |
| 32 | Extra touch input |
| 33 | TTP223 touch |

---

## 4. Network

Firmware pairs by **MAC**, not fixed IP. DHCP preferred; static fallback if DHCP fails.

| Node | Board MAC (firmware) | DHCP | Fallback IP |
|------|----------------------|------|-------------|
| Tank | `02:54:41:4E:4B:01` | Yes | `192.168.50.50` |
| Alarm | `02:41:4C:52:4D:01` | Yes | `192.168.50.51` |

| Setting | Value |
|---------|-------|
| UDP port | **5005** |
| Fallback subnet | `255.255.255.0` |
| Fallback gateway / DNS | `192.168.50.1` |

LAN mode is the only mode that uses the W5500. Wi-Fi mode and hotspot mode do not request a cable address. When both boards use LAN, they need the same network. The page is `http://<ip>:5005` on the hotspot and on the mode address. See [api.md](api.md).

---

## 5. Do / Don’t

| Do | Don’t |
|----|-------|
| Common GND on every module | Power OLED from 5V |
| Echo divider on tank GPIO7 | Wire Echo straight 5V → GPIO |
| Alarm W5500 CS on **GPIO4** | Use GPIO5 or GPIO15 for CS |
| CAT6 into W5500 RJ45 | Expect Ethernet on ESP USB |
| USB power both boards | Starve W5500 + sensor from weak PC USB port |
| Follow Trig/Echo labels | Use UART RX/TX mode with this firmware |

---

## 6. Pre-power checklist

- [ ] Tank: USB plugged into ESP32-S3
- [ ] Tank: W5500 VCC correct for that module
- [ ] Tank: JSN VCC = 5V, GND common
- [ ] Tank: Trig → GPIO6, Echo → divider → GPIO7
- [ ] Tank: CAT6 switch → W5500 RJ45
- [ ] Alarm: USB plugged into DevKit
- [ ] Alarm: W5500 CS = GPIO4 (not 5/15)
- [ ] Alarm: OLED VCC = 3.3V, SDA21 / SCL22
- [ ] Alarm: CAT6 switch → W5500 RJ45
- [ ] Tank: level sensor yellow → GPIO4, black left open
- [ ] Alarm: buzzer I/O GPIO26, relay GPIO27, touch GPIO33
- [ ] LAN mode: both boards on the same network

---

## 7. First boot expect

No mode is saved yet. Each hotspot is up and no level packets are sent.

| What you see | Meaning |
|--------------|---------|
| Tank page at `http://192.168.4.1:5005` | Tank setup |
| Alarm page at `http://192.168.5.1:5005` | Alarm setup |
| Live tank, percent, distance, and 0/1 | Alarm is receiving packets |
| `DISCONNECTED` | The link was up, then silent for about 25 seconds |

There is no connected splash. Bluetooth shows the page IP.

---

## 8. Calibration (after wiring)

On the tank page, not in `config.h`:

| Field | Meaning |
|-------|---------|
| Empty cm | Distance with the tank empty. About 140 |
| Full cm | Distance with the tank full. About 25, outside the blind zone |
| Current water % | How full it looks right now |

Save only happens when the sensor is steady and the measured distance is within 25 cm of that percent. Beep levels are also edited on that page.

---

*Source of truth in code: `firmware/tank-node/src/config.h`, `firmware/alarm-node/src/config.h`, `firmware/shared/app_config.h`, `firmware/shared/network.h`.*
