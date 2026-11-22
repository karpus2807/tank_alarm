#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Saved until Factory reset from the web page.
// Tank modes: Wi-Fi, LAN, AP. Alarm modes: Wi-Fi (WLAN) and LAN.
enum RunMode : uint8_t {
  MODE_NONE = 0,
  MODE_WIFI = 1,
  MODE_LAN = 2,
  MODE_AP = 3,
};

struct __attribute__((packed)) BeepPat {
  uint8_t count;
  uint16_t onMs;
  uint16_t innerMs;
  uint16_t groupMs;
  uint8_t times;  // 0 = keep going until mute
};

struct Cfg {
  uint8_t ver;
  uint8_t mode;
  char ssid[33];
  char pass[65];
  float emptyCm;
  float fullCm;
  uint8_t lowPct;
  uint8_t midPct;
  uint8_t highPct;
  uint8_t critPct;
  uint8_t resetPct;
  uint8_t levelSolid;
  BeepPat low;
  BeepPat mid;
  BeepPat high;
  BeepPat crit;
};

static const uint8_t CFG_VER = 1;
static const uint16_t HTTP_PORT = 5005;
static const char TANK_SSID[] = "tank";
static const char ALARM_SSID[] = "alarm";
static const char LINK_PASS[] = "Krajay@27988";
static const IPAddress TANK_AP_IP(192, 168, 4, 1);
static const IPAddress ALARM_AP_IP(192, 168, 5, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

struct Field {
  char key[8];
  char val[68];
};

inline void cfgDefaults(Cfg& c) {
  memset(&c, 0, sizeof(c));
  c.ver = CFG_VER;
  c.mode = MODE_NONE;
  c.emptyCm = 140.0f;
  c.fullCm = 25.0f;
  c.lowPct = 10;
  c.midPct = 80;
  c.highPct = 90;
  c.critPct = 95;
  c.resetPct = 50;
  c.levelSolid = 1;
  c.low = {2, 300, 200, 2000, 5};
  c.mid = {2, 400, 350, 350, 1};
  c.high = {3, 400, 350, 350, 1};
  c.crit = {1, 4000, 1000, 1000, 0};
}

inline void clampPat(BeepPat& p) {
  if (p.count < 1) p.count = 1;
  if (p.count > 5) p.count = 5;
  if (p.onMs < 50) p.onMs = 50;
  if (p.onMs > 10000) p.onMs = 10000;
  if (p.innerMs > 10000) p.innerMs = 10000;
  if (p.groupMs > 10000) p.groupMs = 10000;
  if (p.times > 20) p.times = 20;
}

inline void loadCfg(Cfg& c) {
  cfgDefaults(c);
  Preferences prefs;
  if (!prefs.begin("cfg", true)) return;
  if (prefs.getUChar("ver", 0) != CFG_VER) {
    prefs.end();
    return;
  }
  c.mode = prefs.getUChar("mode", MODE_NONE);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  strncpy(c.ssid, ssid.c_str(), sizeof(c.ssid) - 1);
  strncpy(c.pass, pass.c_str(), sizeof(c.pass) - 1);
  c.emptyCm = prefs.getFloat("empty", c.emptyCm);
  c.fullCm = prefs.getFloat("full", c.fullCm);
  c.lowPct = prefs.getUChar("low", c.lowPct);
  c.midPct = prefs.getUChar("mid", c.midPct);
  c.highPct = prefs.getUChar("high", c.highPct);
  c.critPct = prefs.getUChar("crit", c.critPct);
  c.resetPct = prefs.getUChar("reset", c.resetPct);
  c.levelSolid = prefs.getUChar("lvl", 1);
  if (c.mode > MODE_AP) c.mode = MODE_NONE;
  prefs.getBytes("plow", &c.low, sizeof(c.low));
  prefs.getBytes("pmid", &c.mid, sizeof(c.mid));
  prefs.getBytes("phigh", &c.high, sizeof(c.high));
  prefs.getBytes("pcrit", &c.crit, sizeof(c.crit));
  prefs.end();
  clampPat(c.low);
  clampPat(c.mid);
  clampPat(c.high);
  clampPat(c.crit);
}

inline void saveCfg(const Cfg& c) {
  Preferences prefs;
  if (!prefs.begin("cfg", false)) return;
  prefs.putUChar("ver", CFG_VER);
  prefs.putUChar("mode", c.mode);
  prefs.putString("ssid", c.ssid);
  prefs.putString("pass", c.pass);
  prefs.putFloat("empty", c.emptyCm);
  prefs.putFloat("full", c.fullCm);
  prefs.putUChar("low", c.lowPct);
  prefs.putUChar("mid", c.midPct);
  prefs.putUChar("high", c.highPct);
  prefs.putUChar("crit", c.critPct);
  prefs.putUChar("reset", c.resetPct);
  prefs.putUChar("lvl", c.levelSolid);
  prefs.putBytes("plow", &c.low, sizeof(c.low));
  prefs.putBytes("pmid", &c.mid, sizeof(c.mid));
  prefs.putBytes("phigh", &c.high, sizeof(c.high));
  prefs.putBytes("pcrit", &c.crit, sizeof(c.crit));
  prefs.end();
}

inline void clearCfg() {
  Preferences prefs;
  if (!prefs.begin("cfg", false)) return;
  prefs.clear();
  prefs.end();
}

// DevKit BOOT button is GPIO0, active low. RST/EN only reboots the chip.
// Hold BOOT for 5s to wipe saved settings, then release to restart into setup.
// Restart waits for release so the board does not fall into download mode.
inline int pollBootReset(Cfg& cfg) {
  static bool ready = false;
  static bool held = false;
  static bool armed = false;
  static uint32_t downAt = 0;
  if (!ready) {
    pinMode(0, INPUT_PULLUP);
    ready = true;
  }
  const bool down = digitalRead(0) == LOW;
  const uint32_t now = millis();
  int edge = 0;
  if (down && !held) {
    held = true;
    armed = false;
    downAt = now;
  }
  if (held && down && !armed && now - downAt >= 5000) {
    armed = true;
    clearCfg();
    cfgDefaults(cfg);
    edge = 1;
  }
  if (held && !down) {
    const bool restart = armed;
    held = false;
    armed = false;
    if (restart) ESP.restart();
  }
  return edge ? 1 : (armed ? 2 : 0);
}

inline const char* modeName(uint8_t mode) {
  if (mode == MODE_WIFI) return "wifi";
  if (mode == MODE_LAN) return "lan";
  if (mode == MODE_AP) return "ap";
  return "setup";
}

inline void urlDecode(char* s) {
  char* w = s;
  for (char* r = s; *r; r++) {
    if (*r == '+') {
      *w++ = ' ';
      continue;
    }
    if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
      char hex[3] = {r[1], r[2], 0};
      *w++ = (char)strtol(hex, nullptr, 16);
      r += 2;
      continue;
    }
    *w++ = *r;
  }
  *w = 0;
}

inline int parseForm(const char* body, Field* out, int maxN) {
  int n = 0;
  if (!body) return 0;
  const char* p = body;
  while (*p && n < maxN) {
    const char* amp = strchr(p, '&');
    size_t len = amp ? (size_t)(amp - p) : strlen(p);
    char pair[96];
    if (len >= sizeof(pair)) len = sizeof(pair) - 1;
    memcpy(pair, p, len);
    pair[len] = 0;
    char* eq = strchr(pair, '=');
    if (eq) {
      *eq = 0;
      urlDecode(pair);
      urlDecode(eq + 1);
      strncpy(out[n].key, pair, sizeof(out[n].key) - 1);
      out[n].key[sizeof(out[n].key) - 1] = 0;
      strncpy(out[n].val, eq + 1, sizeof(out[n].val) - 1);
      out[n].val[sizeof(out[n].val) - 1] = 0;
      n++;
    }
    if (!amp) break;
    p = amp + 1;
  }
  return n;
}

inline const char* fieldGet(Field* f, int n, const char* key) {
  for (int i = 0; i < n; i++) {
    if (!strcmp(f[i].key, key)) return f[i].val;
  }
  return nullptr;
}

inline int fieldInt(Field* f, int n, const char* key, int fallback) {
  const char* v = fieldGet(f, n, key);
  if (!v || !v[0]) return fallback;
  return atoi(v);
}

inline void readPatFields(Field* f, int n, const char* prefix, BeepPat& p) {
  char k[8];
  snprintf(k, sizeof(k), "%sn", prefix);
  p.count = (uint8_t)fieldInt(f, n, k, p.count);
  snprintf(k, sizeof(k), "%so", prefix);
  p.onMs = (uint16_t)fieldInt(f, n, k, p.onMs);
  snprintf(k, sizeof(k), "%si", prefix);
  p.innerMs = (uint16_t)fieldInt(f, n, k, p.innerMs);
  snprintf(k, sizeof(k), "%sg", prefix);
  p.groupMs = (uint16_t)fieldInt(f, n, k, p.groupMs);
  snprintf(k, sizeof(k), "%st", prefix);
  p.times = (uint8_t)fieldInt(f, n, k, p.times);
  clampPat(p);
}

// Returns false and writes err when the form cannot be saved.
inline bool applyFields(Cfg& c, bool tank, Field* f, int n, char* err, size_t errN) {
  Cfg next = c;
  const char* mode = fieldGet(f, n, "mode");
  if (!mode) {
    snprintf(err, errN, "Pick a mode");
    return false;
  }
  if (!strcmp(mode, "wifi")) next.mode = MODE_WIFI;
  else if (!strcmp(mode, "lan")) next.mode = MODE_LAN;
  else if (tank && !strcmp(mode, "ap")) next.mode = MODE_AP;
  else {
    snprintf(err, errN, "That mode is not on this device");
    return false;
  }

  bool joinTank = !tank && fieldGet(f, n, "tankap") && !strcmp(fieldGet(f, n, "tankap"), "1");
  if (next.mode == MODE_WIFI) {
    if (joinTank) {
      strncpy(next.ssid, TANK_SSID, sizeof(next.ssid) - 1);
      strncpy(next.pass, LINK_PASS, sizeof(next.pass) - 1);
    } else {
      const char* ssid = fieldGet(f, n, "ssid");
      const char* pass = fieldGet(f, n, "pass");
      if (!ssid || !ssid[0] || strlen(ssid) > 32) {
        snprintf(err, errN, "Wi-Fi name is missing");
        return false;
      }
      if (!pass || !pass[0]) {
        if (strcmp(ssid, c.ssid) || !c.pass[0]) {
          snprintf(err, errN, "Wi-Fi password is missing");
          return false;
        }
        strncpy(next.pass, c.pass, sizeof(next.pass) - 1);
      } else if (strlen(pass) < 8 || strlen(pass) > 64) {
        snprintf(err, errN, "Password must be 8 to 64 characters");
        return false;
      } else {
        strncpy(next.pass, pass, sizeof(next.pass) - 1);
      }
      strncpy(next.ssid, ssid, sizeof(next.ssid) - 1);
      next.ssid[sizeof(next.ssid) - 1] = 0;
      next.pass[sizeof(next.pass) - 1] = 0;
    }
  }

  if (tank) {
    float emptyCm = c.emptyCm;
    float fullCm = c.fullCm;
    const char* emptyVal = fieldGet(f, n, "empty");
    const char* fullVal = fieldGet(f, n, "full");
    if (emptyVal) emptyCm = atof(emptyVal);
    if (fullVal) fullCm = atof(fullVal);
    if (!(emptyCm > fullCm + 2.0f) || emptyCm > 600 || fullCm < 1) {
      snprintf(err, errN, "Empty distance must be above the full distance");
      return false;
    }
    int low = fieldInt(f, n, "low", c.lowPct);
    int mid = fieldInt(f, n, "mid", c.midPct);
    int high = fieldInt(f, n, "high", c.highPct);
    int crit = fieldInt(f, n, "crit", c.critPct);
    int reset = fieldInt(f, n, "rb", c.resetPct);
    if (low < 1 || mid < 2 || high < 3 || crit < 4 || crit > 100 || !(low < mid && mid < high && high < crit)) {
      snprintf(err, errN, "Levels must rise: low, then 80, then 90, then 95");
      return false;
    }
    if (reset < 1 || reset >= mid) {
      snprintf(err, errN, "Auto reset must be below the 80 percent level");
      return false;
    }
    next.emptyCm = emptyCm;
    next.fullCm = fullCm;
    next.lowPct = (uint8_t)low;
    next.midPct = (uint8_t)mid;
    next.highPct = (uint8_t)high;
    next.critPct = (uint8_t)crit;
    next.resetPct = (uint8_t)reset;
    next.levelSolid = 1;
    readPatFields(f, n, "l", next.low);
    readPatFields(f, n, "m", next.mid);
    readPatFields(f, n, "h", next.high);
    readPatFields(f, n, "c", next.crit);
  }

  c = next;
  c.ver = CFG_VER;
  return true;
}

inline String htmlEscape(const char* s) {
  String o;
  if (!s) return o;
  for (const char* p = s; *p; p++) {
    if (*p == '&') o += "&amp;";
    else if (*p == '<') o += "&lt;";
    else if (*p == '>') o += "&gt;";
    else if (*p == '"') o += "&quot;";
    else o += *p;
  }
  return o;
}

inline String patRow(const char* title, const char* prefix, uint8_t pct, const char* pctName, const BeepPat& p) {
  String s = "<tr><td>";
  s += title;
  s += "</td><td><input name='";
  s += pctName;
  s += "' value='";
  s += pct;
  s += "' size=3></td>";
  const uint16_t vals[] = {p.count, p.onMs, p.innerMs, p.groupMs, p.times};
  const char* suf = "noigt";
  for (int i = 0; i < 5; i++) {
    s += "<td><input name='";
    s += prefix;
    s += suf[i];
    s += "' value='";
    s += vals[i];
    s += "' size=4></td>";
  }
  s += "</tr>";
  return s;
}

inline String portalPage(bool tank, const Cfg& c, const char* hotspotIp, const char* modeIp, const char* err) {
  String s;
  s.reserve(9800);
  s += "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>";
  s += "<title>";
  s += tank ? "Tank" : "Alarm";
  s += "</title></head><body>";
  s += tank ? "<h2>Tank</h2>" : "<h2>Alarm</h2>";
  s += "<p>Hotspot stays at <b>http://";
  s += hotspotIp;
  s += ":5005</b> even when the saved Wi-Fi is wrong.<br>Page and API are also on <b>http://";
  s += modeIp[0] ? modeIp : hotspotIp;
  s += ":5005</b>. Bluetooth shows that IP, and follows it if the address changes.</p>";
  s += "<pre id=live>waiting</pre>";
  if (err && err[0]) {
    s += "<p><b>";
    s += htmlEscape(err);
    s += "</b></p>";
  }
  s += "<form method=POST action=/save>";
  if (tank) {
    s += "<p><label><input type=radio name=mode value=wifi";
    if (c.mode == MODE_WIFI) s += " checked";
    s += "> Home Wi-Fi</label> <label><input type=radio name=mode value=lan";
    if (c.mode == MODE_LAN) s += " checked";
    s += "> LAN</label> <label><input type=radio name=mode value=ap";
    if (c.mode == MODE_AP || c.mode == MODE_NONE) s += " checked";
    s += "> Hotspot (alarm joins tank)</label></p>";
  } else {
    s += "<p><label><input type=radio name=mode value=lan";
    if (c.mode == MODE_LAN) s += " checked";
    s += "> LAN</label> <label><input type=radio name=mode value=wifi";
    if (c.mode == MODE_WIFI || c.mode == MODE_NONE) s += " checked";
    s += "> Wi-Fi</label></p>";
    s += "<p><label><input type=checkbox name=tankap value=1> Join tank hotspot</label></p>";
  }
  s += "<p>Wi-Fi name <input name=ssid maxlength=32 value='";
  s += htmlEscape(c.ssid);
  s += "'><br>Password <input name=pass type=password maxlength=64 placeholder='blank keeps old'></p>";
  s += "<p><button type=button onclick='scanWifi()'>Show Wi-Fi networks</button></p><div id=wifis></div>";
  if (tank) {
    s += "<p>Empty cm <input name=empty value='";
    s += String(c.emptyCm, 1);
    s += "'> Full cm <input name=full value='";
    s += String(c.fullCm, 1);
    s += "'><br>Level sensor 1 holds a solid beep until touch mute.<br>Auto reset below <input name=rb value='";
    s += c.resetPct;
    s += "' size=3>%</p>";
    s += "<table border=1 cellpadding=4><tr><th></th><th>%</th><th>beeps</th><th>on ms</th><th>gap</th><th>group</th><th>times 0=mute</th></tr>";
    s += patRow("Low", "l", c.lowPct, "low", c.low);
    s += patRow("Mid", "m", c.midPct, "mid", c.mid);
    s += patRow("High", "h", c.highPct, "high", c.high);
    s += patRow("Full", "c", c.critPct, "crit", c.crit);
    s += "</table>";
  }
  s += "<p><button type=submit>Save and reboot</button></p></form>";
  if (tank) {
    s += "<h3>Ultrasonic calibration</h3>";
    s += "<p>Sensor is on top of a 140 cm tank. Empty cm is about 140. Full cm is often 20 to 30. Current % is how full the tank looks right now. Calibration measures for a few seconds and saves only when every reading stays within 1.5 cm. A jumping sensor is rejected.</p>";
    s += "<form method=POST action=/calibrate>Empty cm <input name=cempty value='";
    s += String(c.emptyCm, 1);
    s += "'> Full cm <input name=cfull value='";
    s += String(c.fullCm, 1);
    s += "'> Current water % <input name=cnow size=4> <button type=submit>Calibrate</button></form>";
    s += "<h3>Firmware update</h3>";
    s += "<p>Choose the tank firmware .bin. Flash works on the LAN IP and on the hotspot. Progress shows here. The board reboots when the write finishes.</p>";
    s += "<input id=fw type=file accept=.bin> <button type=button onclick='flashFw()'>Flash</button>";
    s += "<br><progress id=fwbar max=100 value=0 style='width:16em'></progress> <span id=fwmsg></span>";
  }
  s += "<form method=POST action=/reset><button type=submit>Factory reset</button></form>";
  s += "<script>async function tick(){try{let j=await (await fetch('/api/status')).json();let t='mode '+j.mode+'\\nIP '+j.ip+'\\nhotspot '+j.ap+'\\n';";
  s += "t+='distance '+(j.cm<0?'--':j.cm+' cm')+'\\nlevel '+(j.pct<0?'--':j.pct+' %')+'\\n';";
  s += "if(j.stable!=null)t+='reading '+(j.stable?'steady':'settling')+'\\n';";
  s += "if(j.spread!=null)t+='spread '+j.spread+' cm\\n';";
  s += "if(j.share)t+='hotspot is sharing internet\\n';document.getElementById('live').textContent=t;}catch(e){}}setInterval(tick,2000);tick();";
  s += "function flashFw(){let f=document.getElementById('fw');let bar=document.getElementById('fwbar');let msg=document.getElementById('fwmsg');";
  s += "if(!f||!f.files[0]){if(msg)msg.textContent='Choose the tank .bin';return;}let xhr=new XMLHttpRequest();xhr.open('POST','/update');";
  s += "xhr.upload.onprogress=function(e){if(!e.lengthComputable)return;let p=Math.round(100*e.loaded/e.total);bar.value=p;msg.textContent='Flashing '+p+'%';};";
  s += "xhr.onload=function(){if(xhr.status==200){bar.value=100;msg.textContent='Flashed. Rebooting.';setTimeout(function(){location.reload();},8000);}else msg.textContent='Flash failed';};";
  s += "xhr.onerror=function(){msg.textContent='Link dropped. If it rebooted, open the page again.';};let body=new FormData();body.append('firmware',f.files[0],f.files[0].name);xhr.send(body);};";
  s += "async function scanWifi(){let el=document.getElementById('wifis');if(!el)return;el.textContent='Scanning. If this page drops, rejoin this hotspot.';";
  s += "try{await fetch('/api/wifi?start=1');}catch(e){}for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,1000));try{let j=await (await fetch('/api/wifi')).json();";
  s += "if(j.scanning)continue;el.innerHTML='';if(!j.networks||!j.networks.length){el.textContent='No network found';return;}";
  s += "j.networks.forEach(n=>{let b=document.createElement('button');b.type='button';b.textContent=n.ssid+'  '+n.rssi+' dBm';";
  s += "b.onclick=function(){document.querySelector('input[name=ssid]').value=n.ssid;var box=document.querySelector('input[name=tankap]');if(box)box.checked=(n.ssid==='tank');};el.appendChild(b);el.appendChild(document.createTextNode(' '));});return;}catch(e){}}el.textContent='Scan failed';}</script>";
  s += "</body></html>";
  return s;
}
