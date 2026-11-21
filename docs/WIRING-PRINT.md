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
| 8 | OLED 0.91" I2C SSD1306 128×32 | Alarm | Display |
| 9 | Active buzzer | Alarm | Optional |
| 10 | Relay module | Alarm | Optional (old siren) |
| 11 | Mute push-button | Alarm | Optional |
| 12 | USB 5V supply | Alarm | Charger / adapter |
| 13 | LAN switch + 2× CAT6 | Shared | Same LAN / subnet |

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

Probe: mount on **tank top lid**, facing water surface.

### 2.4 Tank GPIO quick list

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

### 3.3 OLED 0.91" (SSD1306 128×32 I2C)

| OLED pin | Wire to DevKit | Must |
|----------|----------------|------|
| VCC | **3.3V** | ✓ |
| GND | GND | ✓ |
| SDA | **GPIO21** | ✓ |
| SCL | **GPIO22** | ✓ |

I2C address usually **0x3C**.

### 3.4 Buzzer / relay / mute (optional)

| Device | Pin | Wire to DevKit | Note |
|--------|-----|----------------|------|
| Active buzzer + | — | **GPIO26** | Use transistor if high current |
| Active buzzer − | — | GND | |
| Relay IN | IN | **GPIO27** | Can short old 2-pin alarm |
| Relay power | VCC / GND | 5V / GND | |
| Mute button | one leg | **GPIO32** | Other leg → GND (INPUT_PULLUP) |

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
| 26 | Buzzer |
| 27 | Relay |
| 32 | Mute button |

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

Both W5500s must be on the **same switch / same LAN**. Laptop optional (debug only).

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
- [ ] Both boards on same LAN switch
- [ ] Optional: buzzer GPIO26, relay GPIO27, mute GPIO32

---

## 7. First boot expect

| OLED / Serial | Meaning |
|---------------|---------|
| Boot logo `TANK` / `ALARM` | Alarm firmware started |
| `WAITING` / `SEARCH` | Looking for tank peer |
| `CONNECTED` (~2 s) | Tank MAC locked |
| Fuel bars | Live tank % |
| `DISCONNECTED` | Mid-run link/tank lost |

Tank serial example: `PEER alarm locked IP=… (MAC match)`  
Alarm serial example: `eth=1 link=1` → `alarm ready`

---

## 8. Calibration (after wiring)

Edit `firmware/tank-node/src/config.h` then reflash tank:

| Constant | Meaning |
|----------|---------|
| `TANK_EMPTY_CM` | Distance when tank empty (~150 for 1.5 m) |
| `TANK_FULL_CM` | Distance when tank full (probe near surface) |
| `WARN_PERCENT` | Default 90% |
| `FULL_PERCENT` | Default 95% (alarm) |

---

*Source of truth in code: `firmware/tank-node/src/config.h`, `firmware/alarm-node/src/config.h`, `firmware/shared/network.h`.*
