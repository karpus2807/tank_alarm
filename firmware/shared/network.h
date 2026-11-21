#pragma once
#include <IPAddress.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "protocol.h"

// LAN MAC is the address programmed into the W5500. It is not the chip Wi-Fi MAC.
// Wi-Fi STA MAC is burned into the ESP32 we read on this bench.
// Connected when either MAC of the tank is seen with either MAC of the alarm.
// Mode does not matter: LAN packet or Wi-Fi packet, same pair.
static const uint8_t TANK_MAC[6]       = {0x02, 0x54, 0x41, 0x4E, 0x4B, 0x01};
static const uint8_t ALARM_MAC[6]      = {0x02, 0x41, 0x4C, 0x52, 0x4D, 0x01};
static const uint8_t TANK_WIFI_MAC[6]  = {0x14, 0xC1, 0x9F, 0xC8, 0xE0, 0x58};
static const uint8_t ALARM_WIFI_MAC[6] = {0x00, 0x70, 0x07, 0xE1, 0xBC, 0xD4};

static const uint16_t GROUP_PORT = 5005;

// Fallback if DHCP fails
static const IPAddress FALLBACK_TANK_IP(192, 168, 50, 50);
static const IPAddress FALLBACK_ALARM_IP(192, 168, 50, 51);
static const IPAddress FALLBACK_GATEWAY(192, 168, 50, 1);
static const IPAddress FALLBACK_SUBNET(255, 255, 255, 0);
static const IPAddress FALLBACK_DNS(192, 168, 50, 1);

static const uint32_t HELLO_FAST_MS = 2000;
static const uint32_t HELLO_SLOW_MS = 15000;
static const uint32_t PEER_FRESH_MS = 20000;

inline void macToHex(const uint8_t mac[6], char out[13]) {
  snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

inline bool macEqualHex(const char* hex12, const uint8_t mac[6]) {
  if (!hex12 || strlen(hex12) < 12) return false;
  char expect[13];
  macToHex(mac, expect);
  for (int i = 0; i < 12; i++) {
    if (toupper((unsigned char)hex12[i]) != expect[i]) return false;
  }
  return true;
}

inline const char* findMacHex(const char* json) {
  const char* p = strstr(json, "\"mac\":\"");
  if (!p) return nullptr;
  return p + 7;
}

inline const char* findKeyedMac(const char* json, const char* key) {
  const char* p = strstr(json, key);
  if (!p) return nullptr;
  return p + strlen(key);
}

inline bool jsonHasMac(const char* json, const uint8_t mac[6]) {
  const char* fields[] = {"\"mac\":\"", "\"lan\":\"", "\"wifi\":\""};
  for (unsigned i = 0; i < 3; i++) {
    const char* v = findKeyedMac(json, fields[i]);
    if (v && macEqualHex(v, mac)) return true;
  }
  return false;
}

inline bool jsonFromTank(const char* json) {
  return jsonHasMac(json, TANK_MAC) || jsonHasMac(json, TANK_WIFI_MAC);
}

inline bool jsonFromAlarm(const char* json) {
  return jsonHasMac(json, ALARM_MAC) || jsonHasMac(json, ALARM_WIFI_MAC);
}

inline bool jsonModeIs(const char* json, int mode) {
  const char* p = strstr(json, "\"mode\":");
  if (!p) return false;
  return atoi(p + 7) == mode;
}
