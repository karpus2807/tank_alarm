#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// Home Wi-Fi for modes 3 and 4. Saved in flash (NVS namespace "wlan").
// Survives reboot and mode changes. Password is not printed back.

struct WlanCreds {
  char ssid[33];
  char pass[65];
  bool ok;
};

inline void trimInPlace(char* s) {
  if (!s) return;
  char* start = s;
  while (*start == ' ' || *start == '\t') start++;
  if (start != s) memmove(s, start, strlen(start) + 1);
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

inline WlanCreds loadWlan() {
  WlanCreds c{};
  Preferences prefs;
  if (!prefs.begin("wlan", true)) return c;
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) return c;
  strncpy(c.ssid, ssid.c_str(), sizeof(c.ssid) - 1);
  strncpy(c.pass, pass.c_str(), sizeof(c.pass) - 1);
  c.ok = true;
  return c;
}

inline bool saveWlan(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0] || strlen(ssid) > 32) return false;
  if (!pass || strlen(pass) > 64) return false;
  Preferences prefs;
  if (!prefs.begin("wlan", false)) return false;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  return true;
}

inline void clearWlan() {
  Preferences prefs;
  if (!prefs.begin("wlan", false)) return;
  prefs.clear();
  prefs.end();
}

// 1 saved, 2 cleared, 0 not a WIFI line, -1 WIFI line but bad syntax.
inline int handleWlanLine(char* line, Stream& out) {
  trimInPlace(line);
  if (strncasecmp(line, "WIFI", 4) != 0) return 0;
  char* rest = line + 4;
  if (*rest == '?' && rest[1] == 0) {
    WlanCreds c = loadWlan();
    if (!c.ok) out.println("WLAN: (not set)");
    else out.printf("WLAN ssid=%s\n", c.ssid);
    return 0;
  }
  if (*rest == 0) {
    out.println("Use: WIFI <ssid> <password>");
    return -1;
  }
  if (*rest != ' ') return 0;
  while (*rest == ' ') rest++;
  if (strcasecmp(rest, "CLEAR") == 0) {
    clearWlan();
    out.println("WLAN cleared");
    return 2;
  }
  if (strcasecmp(rest, "?") == 0) {
    WlanCreds c = loadWlan();
    if (!c.ok) out.println("WLAN: (not set)");
    else out.printf("WLAN ssid=%s\n", c.ssid);
    return 0;
  }
  char ssid[33];
  char* pass;
  if (*rest == '"') {
    char* endq = strchr(rest + 1, '"');
    if (!endq || (endq[1] != ' ' && endq[1] != 0)) {
      out.println("Use: WIFI <ssid> <password>   or   WIFI \"my ssid\" <password>");
      return -1;
    }
    *endq = 0;
    strncpy(ssid, rest + 1, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = 0;
    pass = endq[1] ? endq + 2 : endq + 1;
  } else {
    char* sp = strchr(rest, ' ');
    if (!sp) {
      out.println("Use: WIFI <ssid> <password>");
      return -1;
    }
    *sp = 0;
    strncpy(ssid, rest, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = 0;
    pass = sp + 1;
  }
  trimInPlace(ssid);
  trimInPlace(pass);
  if (!saveWlan(ssid, pass)) {
    out.println("WLAN save failed (ssid 1-32, password 0-64)");
    return -1;
  }
  out.printf("WLAN saved ssid=%s\n", ssid);
  return 1;
}
