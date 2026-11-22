#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Update.h>
#include <ETH.h>
#include <BLEDevice.h>
#include <Preferences.h>
#include "config.h"
#include "app_config.h"
#include "ble_name.h"

#define LOG(...) Serial0.printf(__VA_ARGS__)
#define LOGLN(msg) Serial0.println(msg)

WebServer server(HTTP_PORT);
WiFiUDP UdpWifi;

static Cfg cfg;
static bool ethStarted = false;
static bool ethStatic = false;
static bool wifiUdp = false;
static bool staUp = false;
static bool otaOk = false;
static IPAddress ethIp(0, 0, 0, 0);
static uint32_t dhcpStart = 0;
static uint32_t staTry = 0;
static IPAddress alarmPeer(0, 0, 0, 0);
static uint32_t alarmSeen = 0;
static uint32_t rebootAt = 0;
static char pageErr[80];

static const int DIST_RING = 11;
static float distRing[DIST_RING];
static uint8_t distN = 0;
static uint8_t distI = 0;
static float smoothCm = NAN;
static float spreadCm = 0;
static bool readingStable = false;
static uint32_t tightSince = 0;
static uint32_t lastAccept = 0;
static float outAnchor = NAN;
static uint8_t outRun = 0;
static bool levelWet = false;

static char ipBuf[16];
static char modeIpBuf[16];

static void ipToBuf(const IPAddress& ip, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void noteLanIp(const IPAddress& now) {
  if (now == ethIp) return;
  ethIp = now;
  if (now[0]) LOG("LAN ip %s\n", now.toString().c_str());
  else LOGLN("LAN ip lost, hotspot 192.168.4.1");
}

static void chosenIps() {
  ipToBuf(TANK_AP_IP, ipBuf, sizeof(ipBuf));
  if (cfg.mode == MODE_WIFI && WiFi.status() == WL_CONNECTED && WiFi.localIP()[0]) {
    ipToBuf(WiFi.localIP(), modeIpBuf, sizeof(modeIpBuf));
  } else if (cfg.mode == MODE_LAN && ethIp[0]) {
    ipToBuf(ethIp, modeIpBuf, sizeof(modeIpBuf));
  } else {
    strncpy(modeIpBuf, ipBuf, sizeof(modeIpBuf) - 1);
    modeIpBuf[sizeof(modeIpBuf) - 1] = 0;
  }
}

static const char* bleIp() {
  chosenIps();
  return modeIpBuf[0] ? modeIpBuf : ipBuf;
}

static bool staBegun = false;

static void startRadio() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setHostname("tank");
  WiFi.softAPConfig(TANK_AP_IP, TANK_AP_IP, AP_MASK);
  ipToBuf(TANK_AP_IP, ipBuf, sizeof(ipBuf));
  if (!WiFi.softAP(TANK_SSID, LINK_PASS, 6)) LOGLN("AP failed");
  if (!wifiUdp) {
    UdpWifi.begin(GROUP_PORT);
    wifiUdp = true;
  }
  staTry = millis();
  LOG("hotspot %s %s setup only\n", TANK_SSID, ipBuf);
}

static void keepHotspot() {
  static uint32_t checked = 0;
  const uint32_t now = millis();
  if (now - checked < 3000) return;
  checked = now;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  const bool apUp = (WiFi.getMode() & WIFI_MODE_AP) && WiFi.softAPIP() == TANK_AP_IP;
  if (apUp) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.softAPConfig(TANK_AP_IP, TANK_AP_IP, AP_MASK);
  if (WiFi.softAP(TANK_SSID, LINK_PASS, 6)) LOGLN("hotspot restored 192.168.4.1");
  else LOGLN("AP failed");
}

static IPAddress staIp(0, 0, 0, 0);

static void serviceSta() {
  if (cfg.mode != MODE_WIFI || !cfg.ssid[0]) return;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0]) {
    const IPAddress now = WiFi.localIP();
    const bool changed = now != staIp;
    if (!staUp || changed) LOG("home ip %s\n", now.toString().c_str());
    staIp = now;
    staUp = true;
    return;
  }
  if (staUp || staIp[0]) {
    staUp = false;
    staIp = IPAddress((uint32_t)0);
  }
  if (WiFi.status() == WL_IDLE_STATUS) return;
  const uint32_t wait = staBegun ? 45000 : 5000;
  if (millis() - staTry < wait) return;
  staTry = millis();
  staBegun = true;
  WiFi.setAutoReconnect(false);
  WiFi.begin(cfg.ssid, cfg.pass);
  LOG("wifi -> %s\n", cfg.ssid);
}

static void serviceEth() {
  if (cfg.mode != MODE_LAN) return;
  if (!ethStarted) {
    if (millis() < 1500) return;
    ETH.setPollPeriod(10);
    if (!ETH.begin(ETH_PHY_W5500, 1, PIN_ETH_CS, -1, PIN_ETH_RST, SPI2_HOST, PIN_ETH_SCK, PIN_ETH_MISO, PIN_ETH_MOSI, 20)) {
      LOGLN("LAN hardware failed");
      return;
    }
    ethStarted = true;
    dhcpStart = millis();
    LOGLN("LAN DHCP");
  }
  if (!ETH.linkUp()) {
    if (ethStatic) {
      ETH.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
      ethStatic = false;
    }
    noteLanIp(IPAddress((uint32_t)0));
    dhcpStart = millis();
    return;
  }
  IPAddress now = ETH.localIP();
  if (!now[0] && !ethStatic && millis() - dhcpStart >= 30000) {
    if (ETH.config(FALLBACK_TANK_IP, FALLBACK_GATEWAY, FALLBACK_SUBNET, FALLBACK_DNS)) {
      ethStatic = true;
      now = ETH.localIP();
      LOGLN("LAN static 192.168.50.50");
    }
  }
  if (!now[0]) {
    noteLanIp(IPAddress((uint32_t)0));
    return;
  }
  noteLanIp(now);
}

static float distanceToPercent(float cm) {
  if (isnan(cm) || !(cfg.emptyCm > cfg.fullCm)) return NAN;
  float pct = (cfg.emptyCm - cm) / (cfg.emptyCm - cfg.fullCm) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static float reportedPercent() {
  if (levelWet) return 100.0f;
  return distanceToPercent(smoothCm);
}

static bool reportedOk() {
  if (levelWet) return true;
  return readingStable && !isnan(smoothCm);
}

static String statusJson() {
  chosenIps();
  const float cm = smoothCm;
  const float pct = reportedPercent();
  String s = "{\"role\":\"tank\",\"mode\":\"";
  s += modeName(cfg.mode);
  s += "\",\"ap\":\"";
  s += ipBuf;
  s += "\",\"ip\":\"";
  s += modeIpBuf;
  s += "\",\"cm\":";
  s += isnan(cm) ? "-1" : String(cm, 1);
  s += ",\"pct\":";
  s += isnan(pct) ? "-1" : String(pct, 1);
  s += ",\"stable\":";
  s += readingStable ? "1" : "0";
  s += ",\"spread\":";
  s += String(spreadCm, 1);
  s += ",\"share\":0";
  s += ",\"lvl\":";
  s += levelWet ? "1" : "0";
  s += ",\"link\":";
  s += (alarmSeen && millis() - alarmSeen < 20000) ? "1" : "0";
  s += "}";
  return s;
}

static void armReboot() {
  rebootAt = millis() + 700;
  if (rebootAt == 0) rebootAt = 1;
}

static void sortFloats(float* a, int n) {
  for (int i = 1; i < n; i++) {
    float v = a[i];
    int j = i;
    while (j > 0 && a[j - 1] > v) {
      a[j] = a[j - 1];
      j--;
    }
    a[j] = v;
  }
}

static float quantile(const float* sorted, int n, float q) {
  if (n <= 1) return sorted[0];
  float pos = q * (float)(n - 1);
  int i = (int)pos;
  float frac = pos - (float)i;
  if (i >= n - 1) return sorted[n - 1];
  return sorted[i] * (1.0f - frac) + sorted[i + 1] * frac;
}

static void copyRing(float* dst) {
  for (uint8_t i = 0; i < distN; i++) {
    int idx = (int)distI - (int)distN + (int)i;
    if (idx < 0) idx += DIST_RING;
    dst[i] = distRing[idx];
  }
}

static bool ringStats(float* median, float* spread) {
  if (distN < 5) return false;
  float tmp[DIST_RING];
  copyRing(tmp);
  sortFloats(tmp, distN);
  *median = quantile(tmp, distN, 0.5f);
  *spread = quantile(tmp, distN, 0.75f) - quantile(tmp, distN, 0.25f);
  return true;
}

static bool captureLevel(float* measured, char* err, size_t errN);

static bool calibrateFrom(float emptyCm, float fullCm, float nowPct, char* err, size_t errN) {
  if (!(emptyCm > fullCm + 2.0f) || emptyCm > 600 || fullCm < 2) {
    snprintf(err, errN, "Empty distance must be above the full distance");
    return false;
  }
  if (nowPct < 0 || nowPct > 100) {
    snprintf(err, errN, "Current water level must be 0 to 100");
    return false;
  }
  float measured = 0;
  if (!captureLevel(&measured, err, errN)) return false;
  const float expected = emptyCm - (nowPct / 100.0f) * (emptyCm - fullCm);
  const float offset = measured - expected;
  if (fabsf(offset) > 25.0f) {
    snprintf(err, errN, "Reading %.1f cm is too far from %d%%", measured, (int)(nowPct + 0.5f));
    return false;
  }
  const float newEmpty = emptyCm + offset;
  const float newFull = fullCm + offset;
  if (!(newEmpty > newFull + 2.0f) || newEmpty > 600 || newFull < 2) {
    snprintf(err, errN, "Calibration is outside the sensor range");
    return false;
  }
  cfg.emptyCm = newEmpty;
  cfg.fullCm = newFull;
  saveCfg(cfg);
  distN = 0;
  distI = 0;
  smoothCm = measured;
  spreadCm = 0;
  readingStable = true;
  tightSince = millis();
  lastAccept = tightSince;
  outRun = 0;
  outAnchor = NAN;
  snprintf(err, errN, "Calibrated at %.1f cm. Empty %.1f, full %.1f", measured, newEmpty, newFull);
  return true;
}

static bool runCalibrate(Field* fields, int n, char* msg, size_t msgN) {
  const char* e = fieldGet(fields, n, "cempty");
  const char* f = fieldGet(fields, n, "cfull");
  const char* p = fieldGet(fields, n, "cnow");
  if (!e || !e[0] || !f || !f[0] || !p || !p[0]) {
    snprintf(msg, msgN, "Enter empty, full, and the current water level");
    return false;
  }
  return calibrateFrom(atof(e), atof(f), atof(p), msg, msgN);
}

static void handleSave() {
  Field fields[40];
  memset(fields, 0, sizeof(fields));
  int n = 0;
  int args = server.args();
  for (int i = 0; i < args && n < 40; i++) {
    strncpy(fields[n].key, server.argName(i).c_str(), sizeof(fields[n].key) - 1);
    strncpy(fields[n].val, server.arg(i).c_str(), sizeof(fields[n].val) - 1);
    n++;
  }
  char err[80];
  if (!applyFields(cfg, true, fields, n, err, sizeof(err))) {
    strncpy(pageErr, err, sizeof(pageErr) - 1);
    pageErr[sizeof(pageErr) - 1] = 0;
    chosenIps();
    server.send(400, "text/html", portalPage(true, cfg, ipBuf, modeIpBuf, pageErr));
    return;
  }
  saveCfg(cfg);
  server.send(200, "text/html", "<p>Saved. Rebooting into the selected mode.</p>");
  armReboot();
}

static void handleCalibrate() {
  Field fields[8];
  memset(fields, 0, sizeof(fields));
  int n = 0;
  for (int i = 0; i < server.args() && n < 8; i++) {
    strncpy(fields[n].key, server.argName(i).c_str(), sizeof(fields[n].key) - 1);
    strncpy(fields[n].val, server.arg(i).c_str(), sizeof(fields[n].val) - 1);
    n++;
  }
  char msg[80];
  const bool ok = runCalibrate(fields, n, msg, sizeof(msg));
  chosenIps();
  server.send(ok ? 200 : 400, "text/html", portalPage(true, cfg, ipBuf, modeIpBuf, msg));
}

static void handleReset() {
  clearCfg();
  server.send(200, "text/html", "<p>Reset. Rebooting to the hotspot.</p>");
  armReboot();
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
    LOG("update %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      LOG("update begin %s\n", Update.errorString());
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

static void startHttp() {
  server.on("/", HTTP_GET, []() {
    pageErr[0] = 0;
    chosenIps();
    server.send(200, "text/html", portalPage(true, cfg, ipBuf, modeIpBuf, ""));
  });
  server.on("/api/status", HTTP_GET, []() {
    server.send(200, "application/json", statusJson());
  });
  server.on("/api/wifi", HTTP_GET, []() {
    server.send(200, "application/json", wifiListJson(server.hasArg("start")));
  });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);
  server.enableCORS(true);
  server.begin();
}

static void takeAlarm(const char* buf, IPAddress from) {
  if (!(jsonFromAlarm(buf) && strstr(buf, "\"role\":\"alarm\""))) return;
  alarmPeer = from;
  alarmSeen = millis();
}

static void pollUdp() {
  if (!wifiUdp) return;
  int ps = UdpWifi.parsePacket();
  while (ps > 0) {
    char buf[160];
    int n = UdpWifi.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      takeAlarm(buf, UdpWifi.remoteIP());
    }
    ps = UdpWifi.parsePacket();
  }
}

static bool sendUdp(IPAddress dest, const char* buf) {
  if (!wifiUdp || !UdpWifi.beginPacket(dest, GROUP_PORT)) return false;
  UdpWifi.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return UdpWifi.endPacket() == 1;
}

static IPAddress bcastOf(IPAddress ip, IPAddress sn) {
  IPAddress b;
  for (int i = 0; i < 4; i++) b[i] = (ip[i] & sn[i]) | (uint8_t)(~sn[i]);
  return b;
}

static void sendStatus(float cm, float pct, bool ok) {
  if (cfg.mode == MODE_NONE) return;
  char lan[13], wifi[13];
  macToHex(TANK_MAC, lan);
  macToHex(TANK_WIFI_MAC, wifi);
  char buf[TANK_PACKET_MAX];
  int used = snprintf(buf, sizeof(buf),
                      "{\"mac\":\"%s\",\"lan\":\"%s\",\"wifi\":\"%s\",\"role\":\"tank\",\"id\":%u,\"cm\":%.1f,\"pct\":%.1f,\"ok\":%u,\"lvl\":%u,"
                      "\"lo\":%u,\"md\":%u,\"hi\":%u,\"cr\":%u,\"rb\":%u,\"ls\":%u,",
                      lan, lan, wifi, TANK_ID, isnan(cm) ? -1.0f : cm, isnan(pct) ? -1.0f : pct, ok ? 1u : 0u,
                      levelWet ? 1u : 0u, cfg.lowPct, cfg.midPct, cfg.highPct, cfg.critPct, cfg.resetPct, cfg.levelSolid);
  const BeepPat* pats[] = {&cfg.low, &cfg.mid, &cfg.high, &cfg.crit};
  const char* keys[] = {"lp", "mp", "hp", "cp"};
  for (int i = 0; i < 4 && used > 0 && used < (int)sizeof(buf) - 40; i++) {
    used += snprintf(buf + used, sizeof(buf) - used, "\"%s\":[%u,%u,%u,%u,%u]%s", keys[i], pats[i]->count,
                     pats[i]->onMs, pats[i]->innerMs, pats[i]->groupMs, pats[i]->times, i == 3 ? "}" : ",");
  }
  if (used > 0 && used < (int)sizeof(buf) && buf[used - 1] != '}') {
    if (used < (int)sizeof(buf) - 1) {
      buf[used++] = '}';
      buf[used] = 0;
    }
  }
  if (cfg.mode == MODE_AP && wifiUdp) {
    sendUdp(bcastOf(TANK_AP_IP, AP_MASK), buf);
    sendUdp(IPAddress(255, 255, 255, 255), buf);
    if (alarmSeen && alarmPeer[0]) sendUdp(alarmPeer, buf);
  } else if (cfg.mode == MODE_WIFI && wifiUdp && staUp) {
    sendUdp(bcastOf(WiFi.localIP(), WiFi.subnetMask()), buf);
    sendUdp(IPAddress(255, 255, 255, 255), buf);
    if (alarmSeen && alarmPeer[0]) sendUdp(alarmPeer, buf);
  } else if (cfg.mode == MODE_LAN && wifiUdp && ethIp[0]) {
    sendUdp(ETH.broadcastIP(), buf);
    sendUdp(IPAddress(255, 255, 255, 255), buf);
    if (alarmSeen && alarmPeer[0]) sendUdp(alarmPeer, buf);
  }
}

// JSN-SR04T blind zone is about 25 cm. Closer echoes are the probe ringing, not water.
static bool echoOk(float d) {
  if (!(d >= 25.0f && d <= 450.0f)) return false;
  if (!(cfg.emptyCm > cfg.fullCm + 2.0f)) return true;
  float hi = cfg.emptyCm + 40.0f;
  if (hi > 450.0f) hi = 450.0f;
  float lo = cfg.fullCm - 5.0f;
  if (lo < 25.0f) lo = 25.0f;
  return d >= lo && d <= hi;
}

static float pingCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(20);
  digitalWrite(PIN_TRIG, LOW);
  const unsigned long us = pulseIn(PIN_ECHO, HIGH, 25000UL);
  if (us < 1400 || us > 24000) return NAN;
  return us / 58.2f;
}

// Seven pings, 65 ms apart. Median, not the average. A mixed burst is dropped.
static bool burstMedian(float* median, float* span) {
  float raw[7];
  int n = 0;
  for (int i = 0; i < 7; i++) {
    const float d = pingCm();
    if (echoOk(d)) raw[n++] = d;
    if (i + 1 < 7) delay(65);
  }
  if (n < 4) return false;
  sortFloats(raw, n);
  const float width = quantile(raw, n, 0.75f) - quantile(raw, n, 0.25f);
  if (width > 8.0f) return false;
  *median = quantile(raw, n, 0.5f);
  *span = width;
  return true;
}

// Three separate bursts must land within 1.5 cm. Used only by Calibrate.
static bool captureLevel(float* measured, char* err, size_t errN) {
  float shot[3];
  float span = 0;
  for (int i = 0; i < 3; i++) {
    if (!burstMedian(&shot[i], &span) || span > 2.5f) {
      snprintf(err, errN, "Sensor is jumping. Keep the water still and try again");
      return false;
    }
    if (i + 1 < 3) delay(80);
  }
  sortFloats(shot, 3);
  if (shot[2] - shot[0] > 1.5f) {
    snprintf(err, errN, "Reading moved during calibration. Try again");
    return false;
  }
  *measured = shot[1];
  return true;
}

static void pushDistance(uint32_t now) {
  float sample = 0;
  float span = 0;
  if (!burstMedian(&sample, &span)) {
    if (lastAccept && now - lastAccept > 8000) {
      readingStable = false;
      tightSince = 0;
    }
    return;
  }
  float ringMed = 0;
  float ringSpread = 0;
  bool snapped = false;
  if (distN >= 5 && ringStats(&ringMed, &ringSpread) && fabsf(sample - ringMed) > 8.0f) {
    if (!isnan(outAnchor) && fabsf(sample - outAnchor) <= 3.0f) outRun++;
    else outRun = 1;
    outAnchor = sample;
    if (outRun < 3) return;
    distN = 0;
    distI = 0;
    readingStable = false;
    tightSince = 0;
    snapped = true;
  }
  distRing[distI] = sample;
  distI = (distI + 1) % DIST_RING;
  if (distN < DIST_RING) distN++;
  outRun = 0;
  outAnchor = NAN;
  lastAccept = now;
  if (snapped || isnan(smoothCm)) smoothCm = sample;
  else smoothCm += 0.28f * (sample - smoothCm);
  if (!ringStats(&ringMed, &ringSpread)) return;
  spreadCm = ringSpread;
  if (distN >= 5 && spreadCm <= 3.0f) {
    if (!tightSince) tightSince = now;
    if (now - tightSince >= 1500) readingStable = true;
  } else {
    tightSince = 0;
    readingStable = false;
  }
}

static bool levelIsWet(uint32_t now) {
  static uint32_t since = 0;
  static bool wet = false;
  static bool rawHeld = false;
  const bool raw = digitalRead(PIN_LEVEL) == HIGH;
  if (raw != rawHeld) {
    rawHeld = raw;
    since = now;
  }
  if (now - since >= 1500 && wet != rawHeld) wet = rawHeld;
  return wet;
}

static void pollConsole() {
  static char line[32];
  static uint8_t len = 0;
  while (Serial0.available()) {
    int c = Serial0.read();
    if (c == '\n') {
      line[len] = 0;
      if (!strcasecmp(line, "RESET")) {
        clearCfg();
        ESP.restart();
      }
      len = 0;
    } else if (c != '\r' && len < sizeof(line) - 1) {
      line[len++] = (char)c;
    }
  }
}

void setup() {
  Serial0.begin(115200);
  delay(200);
  pageErr[0] = 0;
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_LEVEL, INPUT_PULLDOWN);
  loadCfg(cfg);
  const bool oldDefault = fabsf(cfg.emptyCm - 150.0f) < 0.2f && fabsf(cfg.fullCm - 15.0f) < 0.2f;
  const bool wrongInch = cfg.emptyCm >= 300.0f && cfg.emptyCm <= 400.0f;
  if (oldDefault || wrongInch) {
    cfg.emptyCm = 140.0f;
    cfg.fullCm = 25.0f;
    saveCfg(cfg);
    LOGLN("tank height set to 140 cm");
  }
  startRadio();
  startHttp();
  chosenIps();
  LOG("open http://%s:%u\n", bleIp(), HTTP_PORT);
}

void loop() {
  static uint32_t lastSample = 0;
  static uint32_t lastSend = 0;
  const uint32_t now = millis();
  if (pollBootReset(cfg) == 1) LOGLN("BOOT 5s: settings cleared, release to restart");
  pollConsole();
  keepHotspot();
  if (cfg.mode == MODE_WIFI) serviceSta();
  else if (cfg.mode == MODE_LAN) serviceEth();
  server.handleClient();
  pollUdp();
  levelWet = levelIsWet(now);
  if (now - lastSample >= 900) {
    lastSample = now;
    pushDistance(now);
  }
  if (cfg.mode != MODE_NONE && (lastSend == 0 || now - lastSend >= SEND_MS)) {
    lastSend = now;
    sendStatus(smoothCm, reportedPercent(), reportedOk());
  }
  blePublishIp(bleIp());
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) ESP.restart();
}
