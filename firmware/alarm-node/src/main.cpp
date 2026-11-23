#include <Arduino.h>
#include <esp_mac.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Update.h>
#include <BLEDevice.h>
#include <Wire.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include "config.h"
#include "app_config.h"
#include "ble_name.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);
WebServer server(HTTP_PORT);
EthernetUDP UdpEth;
WiFiUDP UdpWifi;
EthernetServer ethHttp(HTTP_PORT);

static Cfg cfg;
static Cfg remote;
static bool ethReady = false;
static bool ethHw = false;
static bool wifiUdp = false;
static bool staUp = false;
static uint32_t dhcpStart = 0;
static uint32_t dhcpTry = 0;
static uint32_t staTry = 0;
static uint32_t rebootAt = 0;
static bool otaOk = false;
static bool muted = false;
static uint8_t relayOut = 0;
static uint8_t buzzMode = 0;
static uint8_t relayMode = 0;
static uint8_t relayHigh = 1;

static void setMuted(bool on) {
  muted = on;
  if (muted) {
    buzzMode = 0;
    relayMode = 0;
  }
}
static bool everConnected = false;
static char ipBuf[16];
static char modeIpBuf[16];

struct TankStatus {
  float pct = NAN;
  float cm = NAN;
  bool level = false;
  bool fresh = true;
  uint32_t lastRx = 0;
};
static TankStatus tank;

static void ipToBuf(const IPAddress& ip, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void chosenIps() {
  ipToBuf(ALARM_AP_IP, ipBuf, sizeof(ipBuf));
  modeIpBuf[0] = 0;
  if (cfg.mode == MODE_WIFI && WiFi.status() == WL_CONNECTED && WiFi.localIP()[0]) {
    ipToBuf(WiFi.localIP(), modeIpBuf, sizeof(modeIpBuf));
  } else if (cfg.mode == MODE_LAN && ethReady && Ethernet.localIP()[0]) {
    ipToBuf(Ethernet.localIP(), modeIpBuf, sizeof(modeIpBuf));
  } else {
    strncpy(modeIpBuf, ipBuf, sizeof(modeIpBuf) - 1);
    modeIpBuf[sizeof(modeIpBuf) - 1] = 0;
  }
}

static const char* bleIp() {
  chosenIps();
  return modeIpBuf[0] ? modeIpBuf : ipBuf;
}

static void drawIpScreen() {
  chosenIps();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.setFontDirection(0);
  u8g2.drawStr(0, 14, modeName(cfg.mode));
  u8g2.drawStr(0, 32, bleIp());
  u8g2.drawStr(0, 52, "port 5005");
  u8g2.sendBuffer();
}

static void drawLive(float pct, float cm, bool borderOn) {
  int p = 0;
  bool havePct = !(isnan(pct) || pct < 0);
  if (havePct) {
    p = (int)(pct + 0.5f);
    if (p > 100) p = 100;
  }
  char top[8];
  char bot[12];
  if (!havePct) snprintf(top, sizeof(top), "--%%");
  else snprintf(top, sizeof(top), "%d%%", p);
  if (isnan(cm) || cm < 0) snprintf(bot, sizeof(bot), "--");
  else snprintf(bot, sizeof(bot), "%d cm", (int)(cm + 0.5f));

  const int textH = 34;
  const int tx = 1;
  const int ty = textH + 2;
  const int neckW = 10;
  const int neckH = 12;
  const int bodyW = 116;
  const int bodyH = 64 - ty - 1;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.setFontDirection(1);
  int span = u8g2.getStrWidth(top);
  int y = (textH - span) / 2;
  if (y < 0) y = 0;
  u8g2.drawStr(34, y, top);
  u8g2.setFont(u8g2_font_6x12_tf);
  int digitY = y + span + 2;
  if (digitY > textH - 8) digitY = textH - 8;
  u8g2.drawStr(34, digitY, tank.level ? "1" : "0");
  u8g2.setFont(u8g2_font_5x7_tf);
  span = u8g2.getStrWidth(bot);
  y = (textH - span) / 2;
  if (y < 0) y = 0;
  u8g2.drawStr(108, y, bot);
  u8g2.setFontDirection(0);
  if (borderOn) {
    u8g2.drawFrame(tx, ty, bodyW, bodyH);
    const int neckY = ty + (bodyH - neckH) / 2;
    u8g2.drawFrame(tx + bodyW, neckY, neckW, neckH);
    u8g2.setDrawColor(0);
    u8g2.drawVLine(tx + bodyW, neckY + 1, neckH - 2);
    u8g2.setDrawColor(1);
  }
  const int SEGMENTS = 10;
  const int gap = 1;
  const int innerX = tx + 3;
  const int innerY = ty + 3;
  const int innerW = bodyW - 6;
  const int innerH = bodyH - 6;
  const int segW = (innerW - (SEGMENTS - 1) * gap) / SEGMENTS;
  const int barsOn = (!havePct || p <= 0) ? 0 : ((p + 9) / 10);
  const int on = (barsOn > SEGMENTS) ? SEGMENTS : barsOn;
  for (int i = 0; i < SEGMENTS; i++) {
    if (i < on) u8g2.drawBox(innerX + i * (segW + gap), innerY, segW, innerH);
  }
  u8g2.sendBuffer();
}

static void drawDisconnected() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.setFontDirection(0);
  u8g2.drawStr(4, 36, "DISCONNECTED");
  u8g2.sendBuffer();
}

static bool readPat(const char* json, const char* key, BeepPat& p) {
  char needle[12];
  snprintf(needle, sizeof(needle), "\"%s\":[", key);
  const char* s = strstr(json, needle);
  if (!s) return false;
  int a, b, c, d, e;
  if (sscanf(s + strlen(needle), "%d,%d,%d,%d,%d", &a, &b, &c, &d, &e) != 5) return false;
  p.count = (uint8_t)constrain(a, 1, 5);
  p.onMs = (uint16_t)constrain(b, 50, 10000);
  p.innerMs = (uint16_t)constrain(c, 0, 10000);
  p.groupMs = (uint16_t)constrain(d, 0, 10000);
  p.times = (uint8_t)constrain(e, 0, 20);
  return true;
}

static int jsonUInt(const char* json, const char* key, int fallback) {
  char needle[16];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* s = strstr(json, needle);
  if (!s) return fallback;
  return atoi(s + strlen(needle));
}

static bool parseStatus(const char* json) {
  const char* pctp = strstr(json, "\"pct\":");
  const char* cmp = strstr(json, "\"cm\":");
  if (!pctp) return false;
  float pct = atof(pctp + 6);
  float cm = cmp ? atof(cmp + 5) : NAN;
  tank.pct = pct < 0 ? NAN : pct;
  tank.cm = cm < 0 ? NAN : cm;
  const char* okp = strstr(json, "\"ok\":");
  tank.fresh = okp ? atoi(okp + 5) != 0 : true;
  const char* lp = strstr(json, "\"lvl\":");
  tank.level = lp && atoi(lp + 6) != 0;
  if (tank.level) {
    tank.pct = 100.0f;
    tank.fresh = true;
  }
  tank.lastRx = millis();
  remote.lowPct = (uint8_t)constrain(jsonUInt(json, "lo", remote.lowPct), 1, 100);
  remote.midPct = (uint8_t)constrain(jsonUInt(json, "md", remote.midPct), 1, 100);
  remote.highPct = (uint8_t)constrain(jsonUInt(json, "hi", remote.highPct), 1, 100);
  remote.critPct = (uint8_t)constrain(jsonUInt(json, "cr", remote.critPct), 1, 100);
  remote.resetPct = (uint8_t)constrain(jsonUInt(json, "rb", remote.resetPct), 1, 100);
  remote.levelSolid = (uint8_t)jsonUInt(json, "ls", remote.levelSolid) ? 1 : 0;
  readPat(json, "lp", remote.low);
  readPat(json, "mp", remote.mid);
  readPat(json, "hp", remote.high);
  readPat(json, "cp", remote.crit);
  return true;
}

static int heldZone = 0;

static int updateZone() {
  if (tank.level) {
    heldZone = 5;
    return heldZone;
  }
  if (heldZone == 5) heldZone = 0;
  if (!tank.fresh || isnan(tank.pct)) {
    heldZone = 0;
    return heldZone;
  }
  const float pct = tank.pct;
  int raw = 0;
  if (pct >= remote.critPct) raw = 4;
  else if (pct >= remote.highPct) raw = 3;
  else if (pct >= remote.midPct) raw = 2;
  else if (pct <= remote.lowPct + 1.0f && pct >= remote.lowPct - 1.0f) raw = 1;
  if (raw >= heldZone) heldZone = raw;
  else if (heldZone == 4 && pct < remote.critPct - 2) heldZone = raw;
  else if (heldZone == 3 && pct < remote.highPct - 2) heldZone = raw;
  else if (heldZone == 2 && pct < remote.midPct - 2) heldZone = raw;
  else if (heldZone == 1 && (pct > remote.lowPct + 3 || pct < remote.lowPct - 3)) heldZone = 0;
  return heldZone;
}

static const BeepPat* zonePat(int zone) {
  if (zone == 1) return &remote.low;
  if (zone == 2) return &remote.mid;
  if (zone == 3) return &remote.high;
  if (zone == 4) return &remote.crit;
  return nullptr;
}

static uint32_t groupLen(const BeepPat& p) {
  uint8_t n = p.count ? p.count : 1;
  return (uint32_t)n * p.onMs + (n > 1 ? (uint32_t)(n - 1) * p.innerMs : 0);
}

static bool insideBeeps(const BeepPat& p, uint32_t t) {
  uint8_t n = p.count ? p.count : 1;
  for (uint8_t i = 0; i < n; i++) {
    if (t < p.onMs) return true;
    t -= p.onMs;
    if (i + 1 == n) return false;
    if (t < p.innerMs) return false;
    t -= p.innerMs;
  }
  return false;
}

static bool patternSound(const BeepPat& p, uint32_t elapsed, bool* busy) {
  const uint32_t glen = groupLen(p);
  if (glen == 0) {
    if (busy) *busy = false;
    return false;
  }
  if (p.times == 0) {
    const uint32_t cycle = glen + p.groupMs;
    if (busy) *busy = true;
    return insideBeeps(p, elapsed % (cycle ? cycle : glen));
  }
  const uint32_t total = (uint32_t)p.times * glen + (p.times > 1 ? (uint32_t)(p.times - 1) * p.groupMs : 0);
  if (elapsed >= total) {
    if (busy) *busy = false;
    return false;
  }
  if (busy) *busy = true;
  uint32_t t = elapsed;
  for (uint8_t g = 0; g < p.times; g++) {
    if (t < glen) return insideBeeps(p, t);
    t -= glen;
    if (g + 1 >= p.times) return false;
    if (t < p.groupMs) return false;
    t -= p.groupMs;
  }
  return false;
}

static int zoneMark = -1;
static uint32_t zoneStart = 0;

static bool alarmSound(uint32_t now, bool* busy) {
  const int zone = updateZone();
  if (zone != zoneMark) {
    zoneMark = zone;
    zoneStart = now;
  }
  if (busy) *busy = false;
  if (zone == 5) {
    if (busy) *busy = true;
    return true;
  }
  const BeepPat* p = zonePat(zone);
  if (!p) return false;
  return patternSound(*p, now - zoneStart, busy);
}

static void startRadio() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setHostname("alarm");
  const bool sta = cfg.mode == MODE_WIFI && cfg.ssid[0];
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(ALARM_AP_IP, ALARM_AP_IP, AP_MASK);
  WiFi.softAP(ALARM_SSID, LINK_PASS, 6);
  if (!wifiUdp) {
    UdpWifi.begin(GROUP_PORT);
    wifiUdp = true;
  }
  if (sta) {
    WiFi.begin(cfg.ssid, cfg.pass);
    staTry = millis();
    Serial.printf("wifi -> %s\n", cfg.ssid);
  }
  ipToBuf(ALARM_AP_IP, ipBuf, sizeof(ipBuf));
  Serial.printf("hotspot %s %s\n", ALARM_SSID, ipBuf);
}

static void startEthHw() {
  pinMode(PIN_ETH_CS, OUTPUT);
  digitalWrite(PIN_ETH_CS, HIGH);
  pinMode(PIN_ETH_RST, OUTPUT);
  digitalWrite(PIN_ETH_RST, LOW);
  delay(40);
  digitalWrite(PIN_ETH_RST, HIGH);
  delay(150);
  SPI.begin(PIN_ETH_SCK, PIN_ETH_MISO, PIN_ETH_MOSI, PIN_ETH_CS);
  Ethernet.init(PIN_ETH_CS);
}

static void serviceSta() {
  if (cfg.mode != MODE_WIFI || !cfg.ssid[0]) return;
  if (WiFi.status() == WL_CONNECTED) {
    if (!staUp) {
      staUp = true;
      Serial.printf("wifi ip %s\n", WiFi.localIP().toString().c_str());
    }
    return;
  }
  staUp = false;
  if (WiFi.status() == WL_IDLE_STATUS) return;
  if (millis() - staTry < 20000) return;
  staTry = millis();
  WiFi.reconnect();
}

static void serviceEth() {
  if (cfg.mode != MODE_LAN) return;
  if (ethReady) {
    Ethernet.maintain();
    return;
  }
  const uint32_t now = millis();
  if (!ethHw) {
    if (now < 2000) return;
    startEthHw();
    ethHw = true;
    dhcpStart = now;
    dhcpTry = now;
    Serial.println("LAN DHCP");
  }
  if (now < dhcpTry) return;
  if (now - dhcpStart < 30000) {
    if (Ethernet.begin(const_cast<uint8_t*>(ALARM_MAC), 400, 200)) {
      UdpEth.begin(GROUP_PORT);
      ethHttp.begin();
      ethReady = true;
      Serial.printf("LAN %s\n", Ethernet.localIP().toString().c_str());
    } else dhcpTry = millis() + 8000;
    return;
  }
  Ethernet.begin(const_cast<uint8_t*>(ALARM_MAC), FALLBACK_ALARM_IP, FALLBACK_DNS, FALLBACK_GATEWAY, FALLBACK_SUBNET);
  UdpEth.begin(GROUP_PORT);
  ethHttp.begin();
  ethReady = true;
  Serial.printf("LAN static %s\n", Ethernet.localIP().toString().c_str());
}

static String statusJson() {
  chosenIps();
  const bool linked = tank.lastRx && millis() - tank.lastRx < 25000;
  String s = "{\"role\":\"alarm\",\"mode\":\"";
  s += modeName(cfg.mode);
  s += "\",\"ap\":\"";
  s += ipBuf;
  s += "\",\"ip\":\"";
  s += modeIpBuf;
  s += "\",\"link\":";
  s += linked ? "1" : "0";
  s += ",\"pct\":";
  s += isnan(tank.pct) ? "-1" : String(tank.pct, 1);
  s += ",\"cm\":";
  s += isnan(tank.cm) ? "-1" : String(tank.cm, 1);
  s += ",\"lvl\":";
  s += tank.level ? "1" : "0";
  s += ",\"muted\":";
  s += muted ? "1" : "0";
  s += ",\"buzz\":";
  s += buzzMode;
  s += ",\"relay\":";
  s += relayMode;
  s += ",\"rlyh\":";
  s += relayHigh ? "1" : "0";
  s += ",\"out\":";
  s += relayOut ? "1" : "0";
  s += "}";
  return s;
}

static void armReboot() {
  rebootAt = millis() + 400;
  if (rebootAt == 0) rebootAt = 1;
}

static EthernetClient ethClient;
static char httpBuf[2048];
static uint16_t httpLen = 0;

static void ethWrite(const String& body, const char* type, int code) {
  ethClient.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
                   code, code == 200 ? "OK" : "Bad Request", type, (unsigned)body.length());
  ethClient.print(body);
  ethClient.stop();
  httpLen = 0;
}

static String alarmPage(const char* err);

static void handleSave() {
  Field fields[40];
  memset(fields, 0, sizeof(fields));
  int n = 0;
  for (int i = 0; i < server.args() && n < 40; i++) {
    strncpy(fields[n].key, server.argName(i).c_str(), sizeof(fields[n].key) - 1);
    strncpy(fields[n].val, server.arg(i).c_str(), sizeof(fields[n].val) - 1);
    n++;
  }
  char err[80];
  if (!applyFields(cfg, false, fields, n, err, sizeof(err))) {
    server.send(400, "text/html", alarmPage(err));
    return;
  }
  saveCfg(cfg);
  server.send(200, "text/html", "<p>Saved. Rebooting into the selected mode.</p>");
  armReboot();
}

static String wifiListJson(bool start) {
  int n = WiFi.scanComplete();
  if (start && n != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    n = WIFI_SCAN_RUNNING;
  }
  if (n < 0) {
    if (n != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true, true);
    return "{\"scanning\":1,\"networks\":[]}";
  }
  struct Item {
    char ssid[33];
    int rssi;
  } items[16];
  int count = 0;
  for (int i = 0; i < n && count < 16; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length() || ssid.length() > 32) continue;
    bool printable = true;
    for (unsigned j = 0; j < ssid.length(); j++) {
      if ((unsigned char)ssid[j] < 32) printable = false;
    }
    if (!printable) continue;
    int found = -1;
    for (int j = 0; j < count; j++) {
      if (!strcmp(items[j].ssid, ssid.c_str())) found = j;
    }
    const int rssi = WiFi.RSSI(i);
    if (found >= 0) {
      if (rssi > items[found].rssi) items[found].rssi = rssi;
      continue;
    }
    strncpy(items[count].ssid, ssid.c_str(), 32);
    items[count].ssid[32] = 0;
    items[count].rssi = rssi;
    count++;
  }
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (items[j].rssi > items[i].rssi) {
        Item tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
      }
    }
  }
  String s = "{\"scanning\":0,\"networks\":[";
  for (int i = 0; i < count; i++) {
    if (i) s += ',';
    s += "{\"ssid\":\"";
    for (const char* p = items[i].ssid; *p; p++) {
      if (*p == '"' || *p == '\\') s += '\\';
      s += *p;
    }
    s += "\",\"rssi\":";
    s += items[i].rssi;
    s += '}';
  }
  s += "]}";
  WiFi.scanDelete();
  return s;
}

static void handleUpdateFinish() {
  if (!otaOk || Update.hasError()) {
    Update.abort();
    otaOk = false;
    server.send(500, "text/plain", Update.errorString());
    return;
  }
  server.send(200, "text/plain", "ok");
  armReboot();
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaOk = false;
    Serial.printf("update %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Serial.printf("update begin %s\n", Update.errorString());
      return;
    }
    otaOk = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaOk) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      otaOk = false;
      Update.abort();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaOk) return;
    otaOk = Update.end(true);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaOk = false;
  }
}

static void loadRelayDrive() {
  Preferences prefs;
  if (!prefs.begin("cfg", true)) return;
  relayHigh = prefs.getUChar("rlyh", 1) ? 1 : 0;
  prefs.end();
}

static void saveRelayDrive() {
  Preferences prefs;
  if (!prefs.begin("cfg", false)) return;
  prefs.putUChar("rlyh", relayHigh);
  prefs.end();
}

static String alarmPage(const char* err) {
  chosenIps();
  return portalPage(false, cfg, ipBuf, modeIpBuf, err ? err : "", buzzMode, relayMode, relayHigh);
}

static void applyMute(const char* value) {
  if (!value || !value[0]) return;
  if (!strcmp(value, "1") || !strcmp(value, "mute")) setMuted(true);
  else if (!strcmp(value, "0") || !strcmp(value, "unmute")) setMuted(false);
  else if (!strcmp(value, "toggle")) setMuted(!muted);
}

static void applyTest(const char* act) {
  if (!act) return;
  if (!strcmp(act, "buzzon")) buzzMode = 1;
  else if (!strcmp(act, "buzzoff")) buzzMode = 2;
  else if (!strcmp(act, "relayon")) relayMode = 1;
  else if (!strcmp(act, "relayoff")) relayMode = 2;
  else if (!strcmp(act, "rlyhigh")) {
    relayHigh = 1;
    saveRelayDrive();
  } else if (!strcmp(act, "rlylow")) {
    relayHigh = 0;
    saveRelayDrive();
  } else if (!strcmp(act, "auto")) {
    buzzMode = 0;
    relayMode = 0;
  }
}

static void startHttp() {
  server.on("/", HTTP_GET, []() {
    chosenIps();
    server.send(200, "text/html", alarmPage(""));
  });
  server.on("/test", HTTP_POST, []() {
    applyTest(server.hasArg("act") ? server.arg("act").c_str() : "");
    server.send(200, "text/html", alarmPage(""));
  });
  server.on("/mute", HTTP_POST, []() {
    applyMute(server.hasArg("muted") ? server.arg("muted").c_str() : nullptr);
    server.send(200, "application/json", statusJson());
  });
  server.on("/api/status", HTTP_GET, []() { server.send(200, "application/json", statusJson()); });
  server.on("/api/wifi", HTTP_GET, []() {
    server.send(200, "application/json", wifiListJson(server.hasArg("start")));
  });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);
  server.on("/reset", HTTP_POST, []() {
    clearCfg();
    server.send(200, "text/html", "<p>Reset. Rebooting to the hotspot.</p>");
    armReboot();
  });
  server.enableCORS(true);
  server.begin();
}

static bool ethOta = false;
static bool ethOtaFile = false;
static bool ethOtaOk = false;
static int ethOtaLeft = 0;
static char ethMark[80];
static int ethMarkN = 0;
static uint8_t ethHold[256];
static int ethHoldN = 0;

static int findBytes(const uint8_t* b, int n, const char* pat) {
  int m = strlen(pat);
  if (m <= 0 || n < m) return -1;
  for (int i = 0; i + m <= n; i++) {
    if (!memcmp(b + i, pat, m)) return i;
  }
  return -1;
}

static void ethOtaWrite(const uint8_t* p, int n) {
  if (n <= 0 || !ethOtaOk) return;
  if (Update.write(const_cast<uint8_t*>(p), n) != (size_t)n) ethOtaOk = false;
}

static void ethOtaFinish() {
  int cut = findBytes(ethHold, ethHoldN, "\r\n--");
  if (cut < 0) cut = ethHoldN;
  ethOtaWrite(ethHold, cut);
  const bool ok = ethOtaOk && Update.end(true);
  ethOta = false;
  ethOtaFile = false;
  ethHoldN = 0;
  if (!ok) {
    Update.abort();
    ethWrite(Update.errorString(), "text/plain", 500);
    return;
  }
  ethWrite("ok", "text/plain", 200);
  armReboot();
}

static void ethOtaPushFile(const uint8_t* data, int n) {
  uint8_t mix[512];
  if (ethHoldN + n > (int)sizeof(mix)) {
    ethOtaOk = false;
    return;
  }
  memcpy(mix, ethHold, ethHoldN);
  memcpy(mix + ethHoldN, data, n);
  int total = ethHoldN + n;
  int keep = ethMarkN;
  if (keep < 8) keep = 8;
  if (keep > total) keep = total;
  ethOtaWrite(mix, total - keep);
  memcpy(ethHold, mix + total - keep, keep);
  ethHoldN = keep;
}

static void ethOtaFeed(const uint8_t* data, int n) {
  if (!ethOta || n < 0) return;
  ethOtaLeft -= n;
  int off = 0;
  if (!ethOtaFile) {
    while (off < n && ethHoldN < (int)sizeof(ethHold)) ethHold[ethHoldN++] = data[off++];
    int at = findBytes(ethHold, ethHoldN, "\r\n\r\n");
    if (at >= 0) {
      ethOtaFile = true;
      int start = at + 4;
      int rest = ethHoldN - start;
      ethHoldN = 0;
      if (rest > 0) ethOtaPushFile(ethHold + start, rest);
    } else if (ethHoldN >= (int)sizeof(ethHold)) {
      ethOtaOk = false;
    }
    if (off < n && ethOtaFile) ethOtaPushFile(data + off, n - off);
  } else {
    ethOtaPushFile(data, n);
  }
  if (ethOtaLeft <= 0) ethOtaFinish();
}

static void serviceEthOta() {
  if (!ethClient || !ethClient.connected()) {
    if (ethOtaLeft > 0) {
      Update.abort();
      ethOta = false;
      ethClient.stop();
      httpLen = 0;
    }
    return;
  }
  uint8_t buf[256];
  int got = 0;
  while (ethClient.available() && got < (int)sizeof(buf)) buf[got++] = (uint8_t)ethClient.read();
  if (got > 0) ethOtaFeed(buf, got);
}

static bool ethStartOta(const char* headers, const uint8_t* already, int alreadyN, int contentLength) {
  const char* bpos = strstr(headers, "boundary=");
  ethMarkN = 0;
  ethMark[0] = 0;
  if (bpos) {
    bpos += 9;
    if (*bpos == '"') bpos++;
    int i = 0;
    while (*bpos && *bpos != '"' && *bpos != ';' && *bpos != '\r' && i < 60) ethMark[i++] = *bpos++;
    ethMark[i] = 0;
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "\r\n--%s", ethMark);
    strncpy(ethMark, tmp, sizeof(ethMark) - 1);
    ethMark[sizeof(ethMark) - 1] = 0;
    ethMarkN = strlen(ethMark);
  }
  ethOtaOk = Update.begin(UPDATE_SIZE_UNKNOWN);
  ethOta = true;
  ethOtaFile = bpos == nullptr;
  ethOtaLeft = contentLength;
  ethHoldN = 0;
  if (!ethOtaOk) {
    ethOta = false;
    ethWrite(Update.errorString(), "text/plain", 500);
    return false;
  }
  if (alreadyN > 0) ethOtaFeed(already, alreadyN);
  else if (ethOtaLeft <= 0) ethOtaFinish();
  return true;
}

static void serviceEthHttp() {
  if (!ethReady) return;
  if (ethOta) {
    serviceEthOta();
    return;
  }
  if (!ethClient || !ethClient.connected()) {
    EthernetClient next = ethHttp.available();
    if (!next) return;
    ethClient = next;
    httpLen = 0;
  }
  while (ethClient.available() && httpLen < sizeof(httpBuf) - 1) httpBuf[httpLen++] = (char)ethClient.read();
  httpBuf[httpLen] = 0;
  char* headEnd = strstr(httpBuf, "\r\n\r\n");
  if (!headEnd) {
    if (httpLen >= sizeof(httpBuf) - 1) ethWrite("too big", "text/plain", 400);
    return;
  }
  int bodyAt = (int)(headEnd - httpBuf) + 4;
  const char* lenP = strstr(httpBuf, "Content-Length:");
  int need = 0;
  if (lenP && lenP < headEnd) need = atoi(lenP + 15);
  char method[8] = {0};
  char path[48] = {0};
  sscanf(httpBuf, "%7s %47s", method, path);
  char* query = strchr(path, '?');
  if (query) *query = 0;
  if (!strcmp(method, "POST") && !strcmp(path, "/update")) {
    int have = httpLen - bodyAt;
    if (have < 0) have = 0;
    const uint8_t* body = reinterpret_cast<const uint8_t*>(httpBuf + bodyAt);
    httpLen = 0;
    ethStartOta(httpBuf, body, have, need);
    return;
  }
  if (need < 0 || bodyAt + need >= (int)sizeof(httpBuf)) {
    ethWrite("too big", "text/plain", 400);
    return;
  }
  if ((int)httpLen < bodyAt + need) return;
  httpBuf[bodyAt + need] = 0;
  const bool startScan = strstr(httpBuf, "start=1") != nullptr;
  if (!strcmp(method, "GET") && !strcmp(path, "/")) {
    chosenIps();
    ethWrite(alarmPage(""), "text/html", 200);
  } else if (!strcmp(method, "GET") && !strcmp(path, "/api/status")) {
    ethWrite(statusJson(), "application/json", 200);
  } else if (!strcmp(method, "GET") && !strcmp(path, "/api/wifi")) {
    ethWrite(wifiListJson(startScan), "application/json", 200);
  } else if (!strcmp(method, "POST") && !strcmp(path, "/mute")) {
    Field fields[4];
    int n = parseForm(httpBuf + bodyAt, fields, 4);
    applyMute(fieldGet(fields, n, "muted"));
    ethWrite(statusJson(), "application/json", 200);
  } else if (!strcmp(method, "POST") && !strcmp(path, "/test")) {
    Field fields[8];
    int n = parseForm(httpBuf + bodyAt, fields, 8);
    applyTest(fieldGet(fields, n, "act"));
    ethWrite(alarmPage(""), "text/html", 200);
  } else if (!strcmp(method, "POST") && !strcmp(path, "/reset")) {
    clearCfg();
    ethWrite("Reset. Rebooting.", "text/plain", 200);
    armReboot();
  } else if (!strcmp(method, "POST") && !strcmp(path, "/save")) {
    Field fields[40];
    int n = parseForm(httpBuf + bodyAt, fields, 40);
    char err[80];
    if (!applyFields(cfg, false, fields, n, err, sizeof(err))) {
      chosenIps();
      ethWrite(alarmPage(err), "text/html", 400);
    } else {
      saveCfg(cfg);
      ethWrite("Saved. Rebooting.", "text/plain", 200);
      armReboot();
    }
  } else {
    ethWrite("not found", "text/plain", 400);
  }
  httpLen = 0;
}

static bool sendUdp(bool wifi, IPAddress dest, const char* buf) {
  if (wifi) {
    if (!wifiUdp || !UdpWifi.beginPacket(dest, GROUP_PORT)) return false;
    UdpWifi.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
    return UdpWifi.endPacket() == 1;
  }
  if (!ethReady || !UdpEth.beginPacket(dest, GROUP_PORT)) return false;
  UdpEth.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return UdpEth.endPacket() == 1;
}

static IPAddress bcastOf(IPAddress ip, IPAddress sn) {
  IPAddress b;
  for (int i = 0; i < 4; i++) b[i] = (ip[i] & sn[i]) | (uint8_t)(~sn[i]);
  return b;
}

static void announceHello() {
  if (cfg.mode == MODE_NONE) return;
  if (!wifiUdp && !ethReady) return;
  char lan[13], wifi[13], mine[13];
  macToHex(ALARM_MAC, lan);
  macToHex(ALARM_WIFI_MAC, wifi);
  uint8_t sta[6];
  esp_read_mac(sta, ESP_MAC_WIFI_STA);
  macToHex(sta, mine);
  char buf[180];
  snprintf(buf, sizeof(buf),
           "{\"mac\":\"%s\",\"lan\":\"%s\",\"wifi\":\"%s\",\"role\":\"alarm\",\"hello\":1,\"mode\":%u}",
           mine, lan, wifi, cfg.mode);
  if (cfg.mode == MODE_WIFI && wifiUdp && staUp) {
    sendUdp(true, IPAddress(255, 255, 255, 255), buf);
    sendUdp(true, bcastOf(WiFi.localIP(), WiFi.subnetMask()), buf);
    IPAddress ip = WiFi.localIP();
    if (ip[0] == 192 && ip[1] == 168 && ip[2] == 4) sendUdp(true, TANK_AP_IP, buf);
  } else if (cfg.mode == MODE_LAN && ethReady) {
    sendUdp(false, bcastOf(Ethernet.localIP(), Ethernet.subnetMask()), buf);
    sendUdp(false, IPAddress(255, 255, 255, 255), buf);
  }
}

static void takeTank(const char* buf) {
  if (jsonFromTank(buf) && strstr(buf, "\"role\":\"tank\"") && parseStatus(buf)) everConnected = true;
}

static void pollUdp(bool wifi) {
  if (wifi && cfg.mode != MODE_WIFI) return;
  if (!wifi && cfg.mode != MODE_LAN) return;
  if (wifi && !wifiUdp) return;
  if (!wifi && !ethReady) return;
  int ps = wifi ? UdpWifi.parsePacket() : UdpEth.parsePacket();
  while (ps > 0) {
    char buf[TANK_PACKET_MAX];
    int n = wifi ? UdpWifi.read(buf, sizeof(buf) - 1) : UdpEth.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      takeTank(buf);
    }
    ps = wifi ? UdpWifi.parsePacket() : UdpEth.parsePacket();
  }
}

static void pollConsole() {
  static char line[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\n') {
      line[len] = 0;
      if (!strcasecmp(line, "RESET")) {
        clearCfg();
        ESP.restart();
      }
      len = 0;
    } else if (c != '\r' && len < sizeof(line) - 1) line[len++] = (char)c;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  cfgDefaults(remote);
  loadCfg(cfg);
  loadRelayDrive();
  if (cfg.mode == MODE_AP) cfg.mode = MODE_NONE;
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_MUTE, INPUT_PULLDOWN);
  pinMode(32, INPUT_PULLDOWN);
  digitalWrite(PIN_BUZZER, HIGH);
  digitalWrite(PIN_RELAY, relayHigh ? LOW : HIGH);
  Wire.begin(PIN_SDA, PIN_SCL);
  u8g2.begin();
  startRadio();
  startHttp();
  drawIpScreen();
  Serial.printf("open http://%s:%u\n", bleIp(), HTTP_PORT);
  Serial.println("touch GPIO33: hold 2s = mute or alarm reset");
}

void loop() {
  static uint32_t lastHello = 0;
  static uint32_t lastUi = 0;
  static bool shownSound = false;
  static float prevPct = NAN;
  const uint32_t now = millis();
  const int bootReset = pollBootReset(cfg);
  if (bootReset == 1) {
    Serial.println("BOOT 5s: settings cleared, release to restart");
    digitalWrite(PIN_BUZZER, LOW);
    delay(160);
    digitalWrite(PIN_BUZZER, HIGH);
  }
  pollConsole();
  serviceSta();
  serviceEth();
  server.handleClient();
  serviceEthHttp();
  pollUdp(true);
  pollUdp(false);
  if (cfg.mode != MODE_NONE && now - lastHello >= (everConnected ? 15000 : 2000)) {
    lastHello = now;
    announceHello();
  }

  const int zone = updateZone();
  static bool held = false;
  static bool acted = false;
  static uint32_t downAt = 0;
  static uint32_t lastHigh = 0;
  const bool raw = digitalRead(PIN_MUTE) == HIGH || digitalRead(32) == HIGH;
  if (raw) {
    if (!held) {
      held = true;
      acted = false;
      downAt = now;
    }
    lastHigh = now;
  }
  if (held && !acted && (uint32_t)(now - downAt) >= 2000 && (uint32_t)(now - lastHigh) < 500) {
    acted = true;
    setMuted(!muted);
    Serial.println(muted ? "mute" : "alarm reset");
  }
  if (held && (uint32_t)(now - lastHigh) >= 500) held = false;
  if (!tank.level && !isnan(tank.pct) && !isnan(prevPct) && prevPct >= remote.resetPct && tank.pct < remote.resetPct && muted) {
    muted = false;
    Serial.println("auto reset");
  }
  if (!isnan(tank.pct)) prevPct = tank.pct;

  const uint32_t rxNow = millis();
  uint32_t age = 0xFFFFFFFFu;
  if (tank.lastRx) age = (rxNow >= tank.lastRx) ? rxNow - tank.lastRx : 0;
  const bool linked = age <= 25000;
  static uint32_t unlinkAt = 0;
  if (linked) unlinkAt = 0;
  else if (everConnected && unlinkAt == 0) unlinkAt = rxNow;
  const bool showTank = everConnected && (linked || (unlinkAt && rxNow - unlinkAt < 3000));

  bool busy = false;
  const bool tone = alarmSound(now, &busy);
  bool wantBuzz = linked && tone;
  bool wantRelay = linked && zone >= 4;
  if (buzzMode == 1) wantBuzz = true;
  else if (buzzMode == 2 || muted) wantBuzz = false;
  if (relayMode == 1) wantRelay = true;
  else if (relayMode == 2 || muted) wantRelay = false;
  relayOut = wantRelay ? 1 : 0;
  const bool sound = wantBuzz;
  digitalWrite(PIN_BUZZER, wantBuzz ? LOW : HIGH);
  const bool relayLevel = relayHigh ? wantRelay : !wantRelay;
  digitalWrite(PIN_RELAY, relayLevel ? HIGH : LOW);

  if (bootReset) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setFontDirection(0);
    u8g2.drawStr(0, 28, "Settings cleared");
    u8g2.drawStr(0, 48, "Release BOOT");
    u8g2.sendBuffer();
  } else if (sound != shownSound || now - lastUi >= 250) {
    shownSound = sound;
    lastUi = now;
    if (showTank) {
      const bool levelAlarm = tank.level && linked && !muted;
      const bool borderOn = levelAlarm ? ((now / 400) % 2 == 0)
                                       : (!(linked && !muted && busy) || sound);
      drawLive(tank.pct, tank.cm, borderOn);
    } else if (everConnected && !showTank) {
      drawDisconnected();
    } else {
      drawIpScreen();
    }
  }
  blePublishIp(bleIp());
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) ESP.restart();
}
