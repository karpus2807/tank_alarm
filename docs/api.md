# Tank and alarm HTTP API

Both boards serve this API on TCP port **5005**. Use `http://`, not `https`. Port 80 is closed.

The address is the one in `GET /api/status` as `ip`. The hotspot stays up as well:

| Device | Hotspot | Hotspot address |
|---|---|---|
| Tank | `tank` | `http://192.168.4.1:5005` |
| Alarm | `alarm` | `http://192.168.5.1:5005` |

The hotspot password is `LINK_PASS` in `firmware/shared/app_config.h`.

When the tank is in LAN mode and the cable has an address, the same API answers on that address, for example `http://10.42.0.244:5005`. Bluetooth advertises the same `ip`.

JSON routes send `Access-Control-Allow-Origin: *`. A bad form POST returns the HTML page with the error text, not a JSON body.

## Tank

### `GET /api/status`

```json
{
  "role": "tank",
  "mode": "lan",
  "ap": "192.168.4.1",
  "ip": "10.42.0.244",
  "cm": 66.0,
  "pct": 64.4,
  "stable": 1,
  "spread": 0.4,
  "share": 0,
  "lvl": 0,
  "link": 0
}
```

| Field | Meaning |
|---|---|
| `mode` | `setup`, `wifi`, `lan`, or `ap` |
| `ap` | Hotspot address, always `192.168.4.1` |
| `ip` | Address to open. Hotspot address when Wi-Fi or LAN has no address yet |
| `cm` | Smoothed distance from the sensor down to the water. `-1` if there is no reading |
| `pct` | Fill percent from empty and full distance. `-1` if `cm` is missing |
| `stable` | `1` when recent ultrasonic readings agree. The percent beeps run only while this is `1` |
| `spread` | Spread of recent readings, centimetres |
| `share` | Always `0`. The hotspot is for setup and does not share internet |
| `lvl` | Level sensor. `1` means full: `pct` is 100 and the alarm sounds continuously until touch mute. `0` leaves `pct` to the ultrasonic reading |
| `link` | `1` when an alarm packet arrived in the last 20 seconds |

### `GET /`

HTML setup page.

### `GET /api/wifi`

Lists Wi-Fi networks. `GET /api/wifi?start=1` starts a scan. Poll `GET /api/wifi` until `scanning` is `0`. A scan can drop a phone off the hotspot for a moment.

```json
{"scanning":0,"networks":[{"ssid":"example","rssi":-46}]}
```

### `POST /save`

`application/x-www-form-urlencoded`. Success reboots into the saved mode.

| Field | Values |
|---|---|
| `mode` | `wifi`, `lan`, or `ap` |
| `ssid` | Home Wi-Fi name. Required for `wifi` |
| `pass` | Password, 8 to 64 characters. Blank keeps the old password when the name did not change |
| `empty` | Sensor distance when the tank is empty, centimetres |
| `full` | Sensor distance when the tank is full, centimetres. Must be at least 2 cm below `empty` |
| `ls` | Kept for old pages. The level-sensor beep is always on when `lvl` is `1` |
| `rb` | Auto-unmute percent. Must be below the mid level |
| `low`, `mid`, `high`, `crit` | Four rising percents |
| `ln` `lo` `li` `lg` `lt` | Low pattern: beep count, on ms, gap ms, group gap ms, times. `lt` `0` means until mute |
| `mn` `mo` `mi` `mg` `mt` | Mid pattern, same order |
| `hn` `ho` `hi` `hg` `ht` | High pattern |
| `cn` `co` `ci` `cg` `ct` | Full pattern |

A missing `mode` returns 400 and does not save.

### `POST /calibrate`

Sensor is on top of the tank, so a higher water level is a shorter distance.

| Field | Meaning |
|---|---|
| `cempty` | Empty distance, centimetres. About 140 for this tank |
| `cfull` | Full distance, centimetres. Often 25, and not inside the 25 cm blind zone |
| `cnow` | How full the tank looks right now, 0 to 100 |

The board measures for a few seconds. It saves only when the readings stay within 1.5 cm, and only when that distance is within 25 cm of the distance implied by `cnow`. Success returns the page and does not reboot. A jumping sensor returns 400.

### `POST /reset`

Clears saved settings and reboots to the hotspot. There is no body.

### `POST /update`

Multipart upload. The file field name is `firmware`. The body is the tank application `.bin` from `firmware/tank-node/.pio/build/tank-s3/firmware.bin`, not a full flash image with the bootloader. `200` and the text `ok` mean the write finished. The board reboots. `500` means the write failed.

## Alarm

### `GET /api/status`

```json
{
  "role": "alarm",
  "mode": "wifi",
  "ap": "192.168.5.1",
  "ip": "192.168.5.1",
  "link": 1,
  "pct": 64.4,
  "cm": 66.0,
  "lvl": 0,
  "muted": 0
}
```

`link` is `1` when a tank packet arrived in the last 25 seconds. `muted` is the touch mute state. `pct` below `0` means the tank has no level yet.

### `GET /`

HTML setup page. Alarm modes are `lan` and `wifi` only.

### `GET /api/wifi`

Same shape as the tank. Used to pick a network to join.

### `POST /save`

| Field | Values |
|---|---|
| `mode` | `lan` or `wifi` |
| `tankap` | `1` to join the tank hotspot. Fills the tank name and password |
| `ssid` | Wi-Fi name when `tankap` is not set |
| `pass` | Password. Blank keeps the old password when the name did not change |

Success reboots. Alarm cannot be saved as `ap`.

### `POST /update`

Same upload as the tank. The file is the alarm application image `alarm-firmware.bin`, not a full flash dump. `200` and `ok` mean it finished and the board reboots.

### `POST /reset`

Clears saved settings and reboots to the alarm hotspot.

## UDP

Tank and alarm also exchange JSON on UDP port 5005. That is the level link, not this HTTP API. A phone or browser should use the HTTP routes above.
