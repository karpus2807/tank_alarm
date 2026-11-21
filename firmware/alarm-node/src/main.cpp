#include <Arduino.h>
#include <esp_mac.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <U8g2lib.h>
#include <utility/w5100.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>
#include "config.h"
#include "link_mode.h"
#include "wifi_store.h"

// 1.3" 128x64, text upright. Most 1.3" modules are SH1106.
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
EthernetUDP UdpEth;
WiFiUDP UdpWifi;

static const bool THIS_IS_TANK = false;

static const float ALARM_PUSH_PERCENT = 95.0f;
static const uint32_t CONNECTED_SPLASH_MS = 1800;

enum UiMode : uint8_t {
  UI_BOOT = 0,
  UI_WAITING,
  UI_CONNECTED_SPLASH,
  UI_LIVE,
  UI_DISCONNECTED,
};

struct TankStatus {
  float pct = NAN;
  float cm = NAN;
  uint8_t alarm = ALARM_LEVEL_NONE;
  bool ok = false;
  bool level = false;
  uint32_t lastRx = 0;
};

static TankStatus tank;
static bool ethUdpReady = false;
static bool wifiUdpReady = false;
static bool ethHwReady = false;
static bool wantHome = false;
static bool appliedHome = false;
static uint32_t dhcpStartMs = 0;
static uint32_t dhcpTryAt = 0;
static uint32_t wifiBeginAt = 0;
static uint32_t wifiDownSince = 0;
static bool reenterMode = false;
static WlanCreds wlan{};
static char consoleLine[128];
static uint8_t consoleLen = 0;
static bool muted = false;
static bool peerLocked = false;
static bool everConnected = false;
static char myMacHex[13];
static UiMode uiMode = UI_BOOT;
static uint32_t connectedSplashUntil = 0;
static bool wasLive = false;

static uint8_t readPhy() {
  SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
  uint8_t v = W5100.readPHYCFGR_W5500();
  SPI.endTransaction();
  return v;
}

static void drawTurned(const char* msg, const uint8_t* font, int xRight, int yTop) {
  u8g2.setFont(font);
  u8g2.setFontDirection(1);
  u8g2.drawStr(xRight, yTop, msg);
  u8g2.setFontDirection(0);
}

static void drawBootLogo() {
  u8g2.clearBuffer();
  const int cx = 30;
  const int cy = 30;
  u8g2.drawTriangle(cx, cy - 16, cx - 12, cy + 2, cx + 12, cy + 2);
  u8g2.drawDisc(cx, cy + 8, 12);
  u8g2.setDrawColor(0);
  u8g2.drawDisc(cx - 4, cy + 5, 2);
  u8g2.setDrawColor(1);
  drawTurned("TANK", u8g2_font_7x13B_tf, 78, 16);
  drawTurned("ALARM", u8g2_font_6x12_tf, 108, 14);
  u8g2.sendBuffer();
}

static void drawStatusScreen(const char* word, bool blinkBorder) {
  const int W = u8g2.getDisplayWidth();
  const int H = u8g2.getDisplayHeight();
  u8g2.clearBuffer();
  if (!blinkBorder || ((millis() / 400) & 1)) {
    u8g2.drawFrame(1, 1, W - 2, H - 2);
  }
  const uint8_t* font = strlen(word) > 8 ? u8g2_font_5x7_tf : u8g2_font_7x13B_tf;
  u8g2.setFont(font);
  u8g2.setFontDirection(1);
  const int span = u8g2.getStrWidth(word);
  int y = (H - span) / 2;
  if (y < 1) y = 1;
  const int glyphW = u8g2.getAscent() - u8g2.getDescent();
  u8g2.drawStr((W + glyphW) / 2, y, word);
  u8g2.setFontDirection(0);
  u8g2.sendBuffer();
}

static const char* oledWord() {
  if (wantHome && !wlan.ok) return "NOWIFI";
  if (wantHome) return "WIFI";
  if (WiFi.status() == WL_CONNECTED) return "AP";
  if (!ethUdpReady) return "DHCP";
  return "LAN";
}

static void drawConnected() {
  drawStatusScreen("CONNECTED", false);
}

static void drawDisconnected() {
  drawStatusScreen("DISCONNECTED", true);
}

/** Option A, turned 90° right: text columns on top, tank along the bottom, neck on the right. */
static void drawLive(float pct, float cm, bool alarming) {
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

  if (alarming) {
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

static void renderUi(bool live, bool alarming) {
  if (live) {
    uiMode = UI_LIVE;
    drawLive(tank.pct, tank.cm, alarming);
    return;
  }

  if (everConnected) {
    uiMode = UI_DISCONNECTED;
    drawDisconnected();
    return;
  }

  uiMode = UI_WAITING;
  drawStatusScreen(oledWord(), true);
}

static bool sendEth(IPAddress dest, const char* buf) {
  if (!ethUdpReady) return false;
  if (!UdpEth.beginPacket(dest, GROUP_PORT)) return false;
  UdpEth.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return UdpEth.endPacket() == 1;
}

static bool sendWifiPkt(IPAddress dest, const char* buf) {
  if (!wifiUdpReady) return false;
  if (!UdpWifi.beginPacket(dest, GROUP_PORT)) return false;
  UdpWifi.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return UdpWifi.endPacket() == 1;
}

static IPAddress bcastOf(IPAddress ip, IPAddress sn) {
  IPAddress b;
  for (int i = 0; i < 4; i++) b[i] = (ip[i] & sn[i]) | (uint8_t)(~sn[i]);
  return b;
}

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

static bool peerFresh() {
  return tank.lastRx != 0 && (millis() - tank.lastRx) < MODE_DWELL_MS;
}

static void joinWifi(bool home) {
  wantHome = home && wlan.ok;
  appliedHome = wantHome;
  wifiUdpReady = false;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (wantHome) WiFi.begin(wlan.ssid, wlan.pass);
  else WiFi.begin(TANK_AP_SSID, TANK_AP_PASS);
  wifiBeginAt = millis();
  uint8_t sta[6];
  WiFi.macAddress(sta);
  macToHex(sta, myMacHex);
  Serial.printf("wifi -> %s\n", wantHome ? wlan.ssid : TANK_AP_SSID);
}

static void serviceEth() {
  if (ethUdpReady) {
    Ethernet.maintain();
    return;
  }
  // A live Wi-Fi peer must not be stalled by a blocking DHCP retry.
  if (WiFi.status() == WL_CONNECTED && peerFresh()) return;
  const uint32_t now = millis();
  if (!ethHwReady) {
    if (now < 4000) return;
    startEthHardware();
    ethHwReady = true;
    dhcpStartMs = now;
    dhcpTryAt = now;
    Serial.println("LAN DHCP, Wi-Fi stays up");
  }
  if (now < dhcpTryAt) return;
  if (now - dhcpStartMs < MODE_DWELL_MS) {
    int ok = Ethernet.begin(const_cast<uint8_t*>(ALARM_MAC), 400, 200);
    if (ok) {
      UdpEth.begin(GROUP_PORT);
      ethUdpReady = true;
      Serial.printf("LAN dhcp ip=%s\n", Ethernet.localIP().toString().c_str());
    } else {
      dhcpTryAt = millis() + 8000;
    }
    return;
  }
  Ethernet.begin(const_cast<uint8_t*>(ALARM_MAC), FALLBACK_ALARM_IP, FALLBACK_DNS,
                 FALLBACK_GATEWAY, FALLBACK_SUBNET);
  UdpEth.begin(GROUP_PORT);
  ethUdpReady = true;
  Serial.printf("LAN static ip=%s\n", Ethernet.localIP().toString().c_str());
}

static void serviceWifi() {
  if (wifiBeginAt == 0) joinWifi(false);

  if (WiFi.status() == WL_CONNECTED) {
    wifiDownSince = 0;
    if (!wifiUdpReady) {
      UdpWifi.begin(GROUP_PORT);
      wifiUdpReady = true;
      Serial.printf("wifi ip=%s\n", WiFi.localIP().toString().c_str());
    }
    return;
  }

  if (tank.lastRx != 0 && millis() - tank.lastRx < 20000) return;
  if (wifiDownSince == 0) wifiDownSince = millis();
  if (millis() - wifiDownSince < 15000) return;
  wifiDownSince = millis();
  WiFi.reconnect();
}

static void pollConsole();

static void serviceNet() {
  pollConsole();
  if (reenterMode) {
    reenterMode = false;
    wlan = loadWlan();
    wifiBeginAt = 0;
  }
  serviceWifi();
  serviceEth();
}

static void onConsoleLine(char* line) {
  trimInPlace(line);
  if (!line[0]) return;
  if (strcasecmp(line, "HELP") == 0 || strcmp(line, "?") == 0) {
    printLinkHelp(Serial);
    return;
  }
  if (strcasecmp(line, "MODE") == 0) {
    Serial.printf("eth=%d wifi=%d target=%s\n", ethUdpReady ? 1 : 0, wifiUdpReady ? 1 : 0,
                  appliedHome ? "home" : "ap");
    if (ethUdpReady) Serial.printf("lan ip=%s\n", Ethernet.localIP().toString().c_str());
    if (wifiUdpReady) Serial.printf("wifi ip=%s\n", WiFi.localIP().toString().c_str());
    return;
  }
  int rc = handleWlanLine(line, Serial);
  if (rc == 1 || rc == 2) reenterMode = true;
}

static void pollConsole() {
  while (Serial.available()) {
    int c = Serial.read();
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

static bool parseStatus(const char* json) {
  const char* pctp = strstr(json, "\"pct\":");
  const char* cmp = strstr(json, "\"cm\":");
  const char* alp = strstr(json, "\"alarm\":");
  const char* okp = strstr(json, "\"ok\":");
  if (!pctp || !alp || !okp) return false;
  tank.pct = atof(pctp + 6);
  if (tank.pct < 0) tank.pct = NAN;
  tank.cm = cmp ? atof(cmp + 5) : NAN;
  if (tank.cm < 0) tank.cm = NAN;
  tank.alarm = (uint8_t)atoi(alp + 8);
  tank.ok = atoi(okp + 5) != 0;
  const char* lp = strstr(json, "\"lvl\":");
  tank.level = lp && atoi(lp + 6) != 0;
  tank.lastRx = millis();
  return true;
}

static void blastHello(bool wifi, const char* buf) {
  IPAddress ip = wifi ? WiFi.localIP() : Ethernet.localIP();
  IPAddress sn = wifi ? WiFi.subnetMask() : Ethernet.subnetMask();
  if (ip[0] != 0) {
    if (wifi) sendWifiPkt(bcastOf(ip, sn), buf);
    else sendEth(bcastOf(ip, sn), buf);
  }
  if (wifi) {
    sendWifiPkt(IPAddress(255, 255, 255, 255), buf);
    if (!appliedHome) sendWifiPkt(TANK_AP_IP, buf);
  } else {
    sendEth(IPAddress(255, 255, 255, 255), buf);
  }
}

static void announceHello() {
  if (!wifiUdpReady && !ethUdpReady) return;
  char lanHex[13];
  char wifiHex[13];
  macToHex(ALARM_MAC, lanHex);
  macToHex(ALARM_WIFI_MAC, wifiHex);
  char buf[180];
  snprintf(buf, sizeof(buf),
           "{\"mac\":\"%s\",\"lan\":\"%s\",\"wifi\":\"%s\",\"role\":\"alarm\",\"hello\":1,\"mode\":%u}",
           myMacHex, lanHex, wifiHex, appliedHome ? (unsigned)MODE_BOTH_WIFI : (unsigned)MODE_TANK_AP);
  if (wifiUdpReady) blastHello(true, buf);
  if (ethUdpReady) blastHello(false, buf);
}

static int alarmStage() {
  static int stage = 0;
  if (tank.level) {
    stage = 4;
    return stage;
  }
  if (stage == 4) stage = 0;
  int want = 0;
  if (!isnan(tank.pct)) {
    if (tank.pct >= 95.0f) want = 3;
    else if (tank.pct >= 90.0f) want = 2;
    else if (tank.pct >= 80.0f) want = 1;
  }
  if (want >= stage) stage = want;
  else if (isnan(tank.pct) || tank.pct < 78.0f) stage = 0;
  else if (stage == 3 && tank.pct < 93.0f) stage = want;
  else if (stage == 2 && tank.pct < 88.0f) stage = want;
  return stage;
}

static bool highBeepRunning = false;

static bool beepOn(int stage, uint32_t now) {
  static int lastStage = -1;
  static uint32_t mark = 0;
  if (stage != lastStage) {
    lastStage = stage;
    mark = now;
  }
  if (stage <= 0) {
    highBeepRunning = false;
    return false;
  }
  if (stage == 4) {
    highBeepRunning = true;
    return true;
  }
  const uint32_t onMs = stage == 3 ? 4000 : 400;
  const uint32_t offMs = stage == 3 ? 1000 : 350;
  const int count = stage == 1 ? 2 : (stage == 2 ? 3 : 1);
  const uint32_t elapsed = now - mark;
  uint32_t t;
  if (stage == 1 || stage == 2) {
    const uint32_t once = (uint32_t)count * (onMs + offMs) - offMs;
    if (elapsed >= once) {
      highBeepRunning = false;
      return false;
    }
    highBeepRunning = true;
    t = elapsed;
  } else {
    highBeepRunning = true;
    t = elapsed % (onMs + offMs);
  }
  for (int i = 0; i < count; i++) {
    if (t < onMs) return true;
    t -= onMs;
    if (t < offMs) return false;
    t -= offMs;
  }
  return false;
}

// Two short beeps, five times, 2s between each pair. Plays once each time
// the level arrives at about 10%.
static bool lowRunning = false;

static bool lowWaterBeep(uint32_t now) {
  static bool latched = false;
  static float prev = NAN;
  static uint32_t mark = 0;

  const float pct = tank.pct;
  const bool known = !isnan(pct);
  if (known && !tank.level) {
    const bool near = pct <= 11.0f && pct >= 9.0f;
    const bool crossed = !isnan(prev) && ((prev > 11.0f && pct <= 10.0f) || (prev < 9.0f && pct >= 10.0f));
    if ((crossed || (isnan(prev) && near)) && !latched && !lowRunning) {
      lowRunning = true;
      latched = true;
      mark = now;
      Serial.println("low 10%");
    }
    if (pct > 13.0f || pct < 7.0f) latched = false;
  }
  if (known) prev = pct;
  if (!lowRunning) return false;

  const uint32_t groupLen = 300 + 200 + 300 + 2000;
  const uint32_t t = now - mark;
  if (t >= groupLen * 5) {
    lowRunning = false;
    return false;
  }
  const uint32_t g = t % groupLen;
  if (g < 300) return true;
  if (g < 500) return false;
  if (g < 800) return true;
  return false;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== alarm link modes ===");
  printLinkHelp(Serial);
  Serial.flush();

  macToHex(ALARM_MAC, myMacHex);
  uint8_t staMac[6];
  esp_read_mac(staMac, ESP_MAC_WIFI_STA);
  char staHex[13];
  char lanHex[13];
  macToHex(staMac, staHex);
  macToHex(ALARM_MAC, lanHex);
  digitalWrite(PIN_BUZZER, HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_MUTE, INPUT_PULLDOWN);
  pinMode(32, INPUT_PULLDOWN);
  muted = false;
  Serial.println("boot: alarm mode");
  Serial.printf("touch idle gpio33=%d gpio32=%d (hold either HIGH for 2s)\n",
                digitalRead(PIN_MUTE) == HIGH, digitalRead(32) == HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  digitalWrite(PIN_RELAY, LOW);
  Serial.printf("LAN  MAC %s\n", lanHex);
  Serial.printf("WIFI MAC %s\n", staHex);
  wlan = loadWlan();
  if (wlan.ok) Serial.printf("saved WLAN ssid=%s\n", wlan.ssid);
  else Serial.println("saved WLAN: (not set)");

  Wire.begin(PIN_SDA, PIN_SCL);
  delay(50);
  u8g2.begin();
  drawBootLogo();

  joinWifi(false);
  uiMode = UI_WAITING;
  drawStatusScreen(oledWord(), true);
  Serial.println("alarm ready");
  Serial.flush();
}

void loop() {
  static uint32_t lastHello = 0;
  static uint32_t lastUi = 0;
  const uint32_t now = millis();

  serviceNet();

  const uint32_t helloEvery = peerLocked ? HELLO_SLOW_MS : HELLO_FAST_MS;
  if (now - lastHello >= helloEvery) {
    lastHello = now;
    announceHello();
  }

  auto takeTank = [](const char* buf) {
    if (jsonFromTank(buf) && strstr(buf, "\"role\":\"tank\"") && parseStatus(buf)) {
      peerLocked = true;
    }
  };
  if (wifiUdpReady) {
    int ps = UdpWifi.parsePacket();
    while (ps > 0) {
      char buf[TANK_PACKET_MAX];
      int n = UdpWifi.read(buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = 0;
        takeTank(buf);
      }
      ps = UdpWifi.parsePacket();
    }
  }
  if (ethUdpReady) {
    int ps = UdpEth.parsePacket();
    while (ps > 0) {
      char buf[TANK_PACKET_MAX];
      int n = UdpEth.read(buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = 0;
        takeTank(buf);
      }
      ps = UdpEth.parsePacket();
    }
  }

  const int stage = alarmStage();
  static bool pressing = false;
  static bool acted = false;
  static uint32_t downAt = 0;
  static uint32_t lastHigh = 0;
  static bool raw33Was = false;
  static bool raw32Was = false;
  static float prevPct = NAN;
  if (!tank.level && !isnan(tank.pct) && !isnan(prevPct) && prevPct >= 50.0f && tank.pct < 50.0f && muted) {
    muted = false;
    Serial.println("auto reset");
  }
  if (!isnan(tank.pct)) prevPct = tank.pct;

  const bool raw33 = digitalRead(PIN_MUTE) == HIGH;
  const bool raw32 = digitalRead(32) == HIGH;
  if (raw33 != raw33Was) {
    Serial.printf("gpio33 %d\n", raw33 ? 1 : 0);
    raw33Was = raw33;
  }
  if (raw32 != raw32Was) {
    Serial.printf("gpio32 %d\n", raw32 ? 1 : 0);
    raw32Was = raw32;
  }
  const bool raw = raw33 || raw32;
  if (raw) lastHigh = now;
  if (raw && !pressing) {
    pressing = true;
    acted = false;
    downAt = now;
  }
  if (pressing && !acted && (now - downAt >= 2000) && (now - lastHigh < 200)) {
    acted = true;
    muted = !muted;
    Serial.printf("%s gpio33=%d gpio32=%d\n", muted ? "mute" : "alarm reset", raw33 ? 1 : 0, raw32 ? 1 : 0);
  }
  if (pressing && !raw && (now - lastHigh >= 200)) {
    if (!acted) Serial.printf("touch short %lums\n", (unsigned long)(now - downAt));
    pressing = false;
  }

  const uint32_t rxNow = millis();
  uint32_t rxAge = 0xFFFFFFFFu;
  if (tank.lastRx != 0) rxAge = (rxNow >= tank.lastRx) ? (rxNow - tank.lastRx) : 0;
  const bool fresh = rxAge <= STALE_MS;
  const bool linked = rxAge <= 25000;
  static uint32_t unlinkAt = 0;
  if (linked) unlinkAt = 0;
  else if (unlinkAt == 0) unlinkAt = rxNow;
  const bool live = tank.lastRx != 0 && (linked || (rxNow - unlinkAt) < 3000);
  if (fresh) {
    peerLocked = true;
    everConnected = true;
  } else if (!linked) {
    peerLocked = false;
  }

  const bool lowOn = lowWaterBeep(now);
  const bool highTone = beepOn(stage, now);
  const bool high = linked && !muted && highTone;
  const bool low = linked && !muted && stage == 0 && lowOn;
  digitalWrite(PIN_BUZZER, (high || low) ? LOW : HIGH);
  digitalWrite(PIN_RELAY, (linked && !muted && stage > 0) ? HIGH : LOW);

  const bool sounding = high || low;
  static bool shownSound = false;
  if (sounding != shownSound || now - lastUi >= 100) {
    shownSound = sounding;
    lastUi = now;
    const bool pattern = highBeepRunning || lowRunning;
    const bool borderOn = !(linked && !muted && pattern) || sounding;
    renderUi(live, borderOn);
  }
}
