#include <Arduino.h>
#include <esp_mac.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <Preferences.h>
#include "config.h"
#include "link_mode.h"
#include "wifi_store.h"

#define LOG(...) Serial0.printf(__VA_ARGS__)
#define LOGLN(msg) Serial0.println(msg)

static const bool THIS_IS_TANK = true;

EthernetUDP UdpEth;
WiFiUDP UdpWifi;
static uint8_t alarmState = ALARM_LEVEL_NONE;
static IPAddress alarmPeer(0, 0, 0, 0);
static uint32_t alarmPeerSeen = 0;
static char myMacHex[13];

static bool ethUdpReady = false;
static bool wifiUdpReady = false;
static bool ethHwReady = false;
static bool staJoined = false;
static bool apOnlyFallback = false;
static uint32_t dhcpStartMs = 0;
static uint32_t dhcpTryAt = 0;
static uint32_t staTryAt = 0;
static uint32_t staFailSince = 0;
static bool peerOnWifi = false;
static bool reenterMode = false;
static WlanCreds wlan{};
static char consoleLine[128];
static uint8_t consoleLen = 0;

// Home STA association can disrupt SoftAP on ESP32. After this long without a
// home association, drop STA and stay AP-only so TankAlarmLink stays reachable.
static const uint32_t STA_GIVE_UP_MS = 45000;
// While in AP-only fallback, periodically retry home Wi-Fi.
static const uint32_t STA_RETRY_HOME_MS = 120000;
// Re-assert SoftAP while still attempting STA (failed associates can kill AP).
static const uint32_t STA_REASSERT_MS = 20000;

static void startEthHardware() {
  pinMode(PIN_ETH_CS, OUTPUT);
  digitalWrite(PIN_ETH_CS, HIGH);
  pinMode(PIN_ETH_RST, OUTPUT);
  digitalWrite(PIN_ETH_RST, LOW);
  delay(50);
  digitalWrite(PIN_ETH_RST, HIGH);
  delay(200);
  SPI.begin(PIN_ETH_SCK, PIN_ETH_MISO, PIN_ETH_MOSI, PIN_ETH_CS);
  Ethernet.init(PIN_ETH_CS);
  SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
  W5100.writePHYCFGR_W5500(0x00);
  SPI.endTransaction();
  delay(50);
  SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
  W5100.writePHYCFGR_W5500(0xF8);
  SPI.endTransaction();
}

static uint8_t readPhy() {
  SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
  uint8_t v = W5100.readPHYCFGR_W5500();
  SPI.endTransaction();
  return v;
}

static IPAddress bcastOf(IPAddress ip, IPAddress sn) {
  IPAddress b;
  for (int i = 0; i < 4; i++) b[i] = (ip[i] & sn[i]) | (uint8_t)(~sn[i]);
  return b;
}

static bool sendEth(IPAddress dest, const char* buf) {
  if (!ethUdpReady) return false;
  if (!UdpEth.beginPacket(dest, GROUP_PORT)) return false;
  UdpEth.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return UdpEth.endPacket() == 1;
}

static bool sendWifi(IPAddress dest, const char* buf) {
  if (!wifiUdpReady) return false;
  if (!UdpWifi.beginPacket(dest, GROUP_PORT)) return false;
  UdpWifi.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return UdpWifi.endPacket() == 1;
}

static bool startSoftAp(bool withSta) {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(withSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(TANK_AP_IP, TANK_AP_IP, TANK_AP_MASK);
  if (!WiFi.softAP(TANK_AP_SSID, TANK_AP_PASS, 6)) {
    LOGLN("AP failed");
    return false;
  }
  return true;
}

static void enterApOnlyFallback(const char* reason) {
  apOnlyFallback = true;
  staJoined = false;
  staFailSince = 0;
  WiFi.disconnect(false);
  if (!startSoftAp(false)) return;
  staTryAt = millis();
  LOG("home wifi failed (%s); AP-only fallback %s\n", reason, TANK_AP_SSID);
}

static void ensureAp() {
  if (wifiUdpReady) return;
  const bool tryHome = wlan.ok && !apOnlyFallback;
  if (!startSoftAp(tryHome)) return;
  UdpWifi.begin(GROUP_PORT);
  wifiUdpReady = true;
  uint8_t apmac[6];
  WiFi.softAPmacAddress(apmac);
  macToHex(apmac, myMacHex);
  LOG("AP up %s mac=%s\n", TANK_AP_SSID, myMacHex);
  if (tryHome) {
    WiFi.begin(wlan.ssid, wlan.pass);
    staTryAt = millis();
    staFailSince = millis();
    LOG("also joining home %s\n", wlan.ssid);
  }
}

static void onConsoleLine(char* line) {
  trimInPlace(line);
  if (!line[0]) return;
  if (strcasecmp(line, "HELP") == 0 || strcmp(line, "?") == 0) {
    printLinkHelp(Serial0);
    return;
  }
  if (strcasecmp(line, "MODE") == 0) {
    LOG("eth=%d wifi=%d peer=%s home=%s\n", ethUdpReady ? 1 : 0, wifiUdpReady ? 1 : 0,
        alarmPeerSeen ? alarmPeer.toString().c_str() : "none",
        !wlan.ok ? "none" : (staJoined ? "up" : (apOnlyFallback ? "ap-fallback" : "trying")));
    if (ethUdpReady) LOG("lan ip=%s\n", Ethernet.localIP().toString().c_str());
    if (wifiUdpReady) LOG("ap ip=%s\n", WiFi.softAPIP().toString().c_str());
    return;
  }
  int rc = handleWlanLine(line, Serial0);
  if (rc == 1 || rc == 2) {
    wlan = loadWlan();
    reenterMode = true;
  }
}

static void pollConsole() {
  while (Serial0.available()) {
    int c = Serial0.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      consoleLine[consoleLen] = 0;
      if (consoleLen) onConsoleLine(consoleLine);
      consoleLen = 0;
    } else if (consoleLen < sizeof(consoleLine) - 1) {
      consoleLine[consoleLen++] = (char)c;
    }
  }
}

static void serviceEth() {
  if (ethUdpReady) {
    Ethernet.maintain();
    return;
  }
  if (alarmPeerSeen != 0 && (millis() - alarmPeerSeen) < MODE_DWELL_MS) return;
  const uint32_t now = millis();
  if (!ethHwReady) {
    if (now < 4000) return;
    startEthHardware();
    ethHwReady = true;
    dhcpStartMs = now;
    dhcpTryAt = now;
    LOGLN("LAN DHCP, AP stays up");
  }
  if (now < dhcpTryAt) return;
  if (now - dhcpStartMs < MODE_DWELL_MS) {
    int ok = Ethernet.begin(const_cast<uint8_t*>(TANK_MAC), 400, 200);
    if (ok) {
      UdpEth.begin(GROUP_PORT);
      ethUdpReady = true;
      LOG("LAN dhcp ip=%s\n", Ethernet.localIP().toString().c_str());
    } else {
      dhcpTryAt = millis() + 8000;
    }
    return;
  }
  Ethernet.begin(const_cast<uint8_t*>(TANK_MAC), FALLBACK_TANK_IP, FALLBACK_DNS,
                 FALLBACK_GATEWAY, FALLBACK_SUBNET);
  UdpEth.begin(GROUP_PORT);
  ethUdpReady = true;
  LOG("LAN static ip=%s\n", Ethernet.localIP().toString().c_str());
}

static void serviceSta() {
  if (!wlan.ok || !wifiUdpReady) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!staJoined) {
      staJoined = true;
      apOnlyFallback = false;
      staFailSince = 0;
      LOG("home wifi ip=%s\n", WiFi.localIP().toString().c_str());
    }
    return;
  }

  if (staJoined) staJoined = false;

  // AP-only recovery: SoftAP stays up; periodically retry home STA.
  if (apOnlyFallback) {
    if (millis() - staTryAt < STA_RETRY_HOME_MS) return;
    apOnlyFallback = false;
    staFailSince = millis();
    staTryAt = millis();
    if (!startSoftAp(true)) {
      enterApOnlyFallback("reassert");
      return;
    }
    WiFi.begin(wlan.ssid, wlan.pass);
    LOG("retry home %s\n", wlan.ssid);
    return;
  }

  if (staFailSince == 0) staFailSince = millis();

  // Prolonged home failure → drop STA so the hotspot stays usable.
  if (millis() - staFailSince >= STA_GIVE_UP_MS) {
    enterApOnlyFallback("timeout");
    return;
  }

  if (millis() - staTryAt < STA_REASSERT_MS) return;
  staTryAt = millis();
  // Re-assert SoftAP before reconnect; failed STA associates often take AP down.
  WiFi.softAPConfig(TANK_AP_IP, TANK_AP_IP, TANK_AP_MASK);
  WiFi.softAP(TANK_AP_SSID, TANK_AP_PASS, 6);
  if (WiFi.status() != WL_IDLE_STATUS) WiFi.reconnect();
}

static void serviceNet() {
  pollConsole();
  if (reenterMode) {
    reenterMode = false;
    wlan = loadWlan();
    staJoined = false;
    apOnlyFallback = false;
    staFailSince = 0;
    if (wifiUdpReady && wlan.ok) {
      if (!startSoftAp(true)) return;
      WiFi.begin(wlan.ssid, wlan.pass);
      staTryAt = millis();
      staFailSince = millis();
      LOG("WLAN updated; joining home %s\n", wlan.ssid);
    } else if (wifiUdpReady) {
      startSoftAp(false);
    }
  }
  ensureAp();
  serviceSta();
  serviceEth();
}

static void takeAlarmPacket(const char* buf, IPAddress from, bool viaWifi) {
  if (!(jsonFromAlarm(buf) && strstr(buf, "\"role\":\"alarm\""))) return;
  const bool changed = alarmPeerSeen == 0 || from != alarmPeer || peerOnWifi != viaWifi;
  alarmPeer = from;
  peerOnWifi = viaWifi;
  alarmPeerSeen = millis();
  if (changed) {
    LOG("PEER alarm %s via %s\n", alarmPeer.toString().c_str(), viaWifi ? "wifi" : "lan");
  }
}

static void pollSocket(bool wifi) {
  if (wifi && !wifiUdpReady) return;
  if (!wifi && !ethUdpReady) return;
  int ps = wifi ? UdpWifi.parsePacket() : UdpEth.parsePacket();
  while (ps > 0) {
    char buf[TANK_PACKET_MAX];
    int n = wifi ? UdpWifi.read(buf, sizeof(buf) - 1) : UdpEth.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      IPAddress from = wifi ? UdpWifi.remoteIP() : UdpEth.remoteIP();
      takeAlarmPacket(buf, from, wifi);
    }
    ps = wifi ? UdpWifi.parsePacket() : UdpEth.parsePacket();
  }
}

static float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(20);
  digitalWrite(PIN_TRIG, LOW);
  const unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (us == 0) return NAN;
  return (us * 0.0343f) / 2.0f;
}

static float averageDistanceCm() {
  float sum = 0;
  uint8_t ok = 0;
  for (uint8_t i = 0; i < SAMPLE_AVG; i++) {
    const float d = readDistanceCm();
    if (!isnan(d) && d > 2.0f && d < 400.0f) {
      sum += d;
      ok++;
    }
    delay(40);
  }
  return ok ? (sum / ok) : NAN;
}

static float distanceToPercent(float cm) {
  if (isnan(cm)) return NAN;
  float pct = (TANK_EMPTY_CM - cm) / (TANK_EMPTY_CM - TANK_FULL_CM) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static uint8_t updateAlarm(float pct) {
  if (isnan(pct)) return alarmState;
  if (alarmState == ALARM_LEVEL_NONE) {
    if (pct >= FULL_PERCENT) alarmState = ALARM_LEVEL_FULL;
    else if (pct >= WARN_PERCENT) alarmState = ALARM_LEVEL_WARN;
  } else if (alarmState == ALARM_LEVEL_WARN) {
    if (pct >= FULL_PERCENT) alarmState = ALARM_LEVEL_FULL;
    else if (pct < CLEAR_PERCENT) alarmState = ALARM_LEVEL_NONE;
  } else {
    if (pct < CLEAR_PERCENT) alarmState = ALARM_LEVEL_NONE;
    else if (pct < FULL_PERCENT) alarmState = ALARM_LEVEL_WARN;
  }
  return alarmState;
}

static void pollPeer() {
  pollSocket(true);
  pollSocket(false);
}

static void blast(bool wifi, const char* buf) {
  IPAddress ip = wifi ? WiFi.softAPIP() : Ethernet.localIP();
  IPAddress sn = wifi ? TANK_AP_MASK : Ethernet.subnetMask();
  if (wifi && staJoined) {
    sendWifi(bcastOf(WiFi.localIP(), WiFi.subnetMask()), buf);
  }
  if (ip[0] != 0) {
    if (wifi) sendWifi(bcastOf(ip, sn), buf);
    else sendEth(bcastOf(ip, sn), buf);
  }
  if (wifi) sendWifi(IPAddress(255, 255, 255, 255), buf);
  else sendEth(IPAddress(255, 255, 255, 255), buf);
  if (alarmPeer[0] != 0 && peerOnWifi == wifi) {
    if (wifi) sendWifi(alarmPeer, buf);
    else sendEth(alarmPeer, buf);
  }
}

static void sendStatus(float cm, float pct, uint8_t alarm, bool ok, bool level) {
  if (!ethUdpReady && !wifiUdpReady) return;
  char lanHex[13];
  char wifiHex[13];
  macToHex(TANK_MAC, lanHex);
  macToHex(TANK_WIFI_MAC, wifiHex);
  char buf[TANK_PACKET_MAX];
  snprintf(buf, sizeof(buf),
           "{\"mac\":\"%s\",\"lan\":\"%s\",\"wifi\":\"%s\",\"role\":\"tank\",\"id\":%u,\"cm\":%.1f,\"pct\":%.1f,\"alarm\":%u,\"ok\":%u,\"mode\":%u,\"lvl\":%u}",
           myMacHex, lanHex, wifiHex, TANK_ID, isnan(cm) ? -1.0f : cm, isnan(pct) ? -1.0f : pct,
           alarm, ok ? 1u : 0u, wifiUdpReady ? (unsigned)MODE_TANK_AP : (unsigned)MODE_LAN_DHCP,
           level ? 1u : 0u);
  if (wifiUdpReady) blast(true, buf);
  if (ethUdpReady) blast(false, buf);
}

void setup() {
  Serial0.begin(115200);
  delay(300);
  macToHex(TANK_MAC, myMacHex);
  uint8_t staMac[6];
  esp_read_mac(staMac, ESP_MAC_WIFI_STA);
  char staHex[13];
  char lanHex[13];
  macToHex(staMac, staHex);
  macToHex(TANK_MAC, lanHex);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_LEVEL, INPUT_PULLDOWN);
  wlan = loadWlan();

  LOGLN("=== tank link modes ===");
  LOG("LAN  MAC %s\n", lanHex);
  LOG("WIFI MAC %s\n", staHex);
  printLinkHelp(Serial0);
  if (wlan.ok) LOG("saved WLAN ssid=%s\n", wlan.ssid);
  else LOGLN("saved WLAN: (not set)");

  ensureAp();
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
  if (now - since >= 200 && wet != rawHeld) {
    wet = rawHeld;
    LOG("level sensor %s\n", wet ? "1" : "0");
  }
  return wet;
}

void loop() {
  static uint32_t lastSample = 0;
  static uint32_t lastSend = 0;
  static float lastCm = NAN;
  static float lastPct = NAN;
  static bool wasFull = false;
  static bool haveSample = false;
  const uint32_t now = millis();

  serviceNet();
  pollPeer();

  const bool levelWet = levelIsWet(now);

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    lastCm = averageDistanceCm();
    lastPct = distanceToPercent(lastCm);
    updateAlarm(lastPct);
    haveSample = true;
  }

  if (wasFull != levelWet) lastSend = 0;
  wasFull = levelWet;

  if (haveSample && (lastSend == 0 || now - lastSend >= SEND_MS)) {
    lastSend = now;
    sendStatus(lastCm, lastPct, alarmState, !isnan(lastCm), levelWet);
  }
}
