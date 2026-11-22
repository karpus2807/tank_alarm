#pragma once
#include <stdint.h>
#include <IPAddress.h>
#include <Stream.h>

// Five link modes. Each one is tried for MODE_DWELL_MS.
// A mode "works" only when a UDP packet from the other board arrives.
// Then the board stays there. After 30s with no peer, it moves on.
// If none of the five connect, the list repeats.
//
// Primary (tried first): tank on LAN, alarm on home Wi-Fi.
enum LinkMode : uint8_t {
  MODE_LAN_DHCP = 1,
  MODE_LAN_STATIC = 2,
  MODE_HYBRID = 3,
  MODE_BOTH_WIFI = 4,
  MODE_TANK_AP = 5,
};

static const LinkMode MODE_ORDER[] = {
    MODE_HYBRID,      // 3 primary
    MODE_BOTH_WIFI,   // 4
    MODE_TANK_AP,     // 5
    MODE_LAN_DHCP,    // 1
    MODE_LAN_STATIC,  // 2
};
static const uint8_t MODE_COUNT = 5;

// Time spent in one mode before switching, and silence that drops a locked mode.
static const uint32_t MODE_DWELL_MS = 30000;
// One DHCP attempt. The mode keeps retrying until the 30s dwell ends.
static const uint32_t DHCP_SLICE_MS = 4000;
static const uint32_t DHCP_RESPONSE_MS = 1500;
// Both boards wait out this boot window so their 30s slots start together.
static const uint32_t BOOT_SYNC_MS = 5000;

// Mode 5 only. Tank creates this network; alarm joins it.
// Home Wi-Fi for modes 3 and 4 is NOT here — it is saved on the device (NVS).
static const char TANK_AP_SSID[] = "TankAlarmLink";
static const char TANK_AP_PASS[] = "TankAlarm#5";
static const IPAddress TANK_AP_IP(192, 168, 4, 1);
static const IPAddress TANK_AP_MASK(255, 255, 255, 0);

// Alarm's own SoftAP for phone/laptop WebUI (WiFi config + status).
// Different subnet from tank SoftAP so AP+STA (peer on TankAlarmLink / home) does not collide.
static const char ALARM_AP_SSID[] = "alarm";
static const char ALARM_AP_PASS[] = "Krajay@27988";
static const IPAddress ALARM_AP_IP(192, 168, 5, 1);
static const IPAddress ALARM_AP_MASK(255, 255, 255, 0);

inline const char* linkModeName(LinkMode m) {
  switch (m) {
    case MODE_LAN_DHCP: return "LAN-DHCP";
    case MODE_LAN_STATIC: return "LAN-STATIC";
    case MODE_HYBRID: return "HYBRID";
    case MODE_BOTH_WIFI: return "WLAN";
    case MODE_TANK_AP: return "TANK-AP";
    default: return "?";
  }
}

inline bool tankUsesEthernet(LinkMode m) {
  return m == MODE_LAN_DHCP || m == MODE_LAN_STATIC || m == MODE_HYBRID;
}

inline bool alarmUsesEthernet(LinkMode m) {
  return m == MODE_LAN_DHCP || m == MODE_LAN_STATIC;
}

inline bool modeUsesHomeWifi(bool isTank, LinkMode m) {
  if (isTank) return m == MODE_BOTH_WIFI;
  return m == MODE_HYBRID || m == MODE_BOTH_WIFI;
}

inline void printLinkHelp(Stream& out) {
  out.println("Modes, 30s each, then repeat. Stay on the one that talks:");
  out.println("  3 HYBRID  tank LAN + alarm home Wi-Fi   (primary, first)");
  out.println("  4 WLAN    both on home Wi-Fi");
  out.println("  5 AP      tank hotspot, alarm joins it");
  out.println("  1 DHCP    both LAN via router, 30s of tries");
  out.println("  2 STATIC  both LAN 192.168.50.50 / .51");
  out.println("Home Wi-Fi (modes 3 and 4), once per board:");
  out.println("  WIFI <ssid> <password>");
  out.println("  WIFI \"ssid with spaces\" <password>");
  out.println("  WIFI?");
  out.println("  WIFI CLEAR");
  out.println("Mode 5 is already set: TankAlarmLink / TankAlarm#5");
  out.println("Alarm config hotspot (always): alarm / Krajay@27988");
  out.println("  WebUI http://192.168.5.1/");
}
