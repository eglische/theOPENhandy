// 03_wifi.ino
#include "openhandy.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

static DNSServer dnsServer;
static WebServer server(80);

static const byte DNS_PORT = 53;
static const char* AP_SSID = "OPENhandy-setup";
static const char* AP_PASS = "access123";

extern HandyConfig   g_cfg;
extern NetworkConfig g_netCfg;
extern bool          g_recoveryMode;

static bool dnsActive    = false;
static bool mdnsStarted  = false;

static const int DEBUG_LOG_MAX_LINES = 200;
static String    s_debugLog[DEBUG_LOG_MAX_LINES];
static int       s_debugLogStart = 0;
static int       s_debugLogCount = 0;

// ---------- HTML page builders (implemented in 08_html.ino) ----------
String html_pageRoot();
String html_pageMotion();
String html_pageConfigSaved();
String html_pageDebug(
  RunState st,
  bool ir1, bool ir3,
  long encAbs, long encAxis,
  bool limits, long posMin, long posMax,
  int thermAdc,
  bool collErr, bool thermErr,
  const String &log
);
String html_pageReboot();
String html_pageUpdateForm();
String html_pageUpdateResult(bool success, const String &msg);
String html_pageNetConfig();
String html_pageNetSaved();
String html_pageSettings();   // NEW

// ---------- Debug ring buffer ----------
void debug_append(const String &line) {
  String entry = String(millis()) + " ms: " + line;
  int idx = (s_debugLogStart + s_debugLogCount) % DEBUG_LOG_MAX_LINES;
  if (s_debugLogCount < DEBUG_LOG_MAX_LINES) {
    s_debugLogCount++;
  } else {
    s_debugLogStart = (s_debugLogStart + 1) % DEBUG_LOG_MAX_LINES;
  }
  s_debugLog[idx] = entry;
}

String debug_getLog() {
  String out;
  for (int i = 0; i < s_debugLogCount; ++i) {
    int idx = (s_debugLogStart + i) % DEBUG_LOG_MAX_LINES;
    out += s_debugLog[idx];
    out += "\n";
  }
  return out;
}

void debug_clearLog() {
  s_debugLogStart = 0;
  s_debugLogCount = 0;
}

// ---------- JSON helper (used by WiFi scan API) ----------
static String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '\\' || c == '\"') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      // Replace control chars with space
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

// ---------- Root / main config page ----------
static void handleRoot() {
  server.send(200, "text/html", html_pageRoot());
}

// ---------- Settings page (tuning) ----------
static void handleSettingsGet() {
  server.send(200, "text/html", html_pageSettings());
}

// ---------- Motion page (manual patterns + cropping) ----------
static void handleMotionGet() {
  server.send(200, "text/html", html_pageMotion());
}

// ---------- Motion HTTP API for Node-RED & UI ----------
static void sendMotionStateJson() {
  int    sp;
  float  cutL, cutU;
  uint8_t pat;
  actuator_getManualUiState(sp, cutL, cutU, pat);

  if (sp < 0)   sp = 0;
  if (sp > 100) sp = 100;

  String json = "{";
  json += "\"ok\":true,";
  json += "\"speed\":" + String(sp) + ",";
  json += "\"cutLower\":" + String(cutL, 3) + ",";
  json += "\"cutUpper\":" + String(cutU, 3) + ",";
  json += "\"pattern\":" + String((int)pat);
  json += "}";

  server.send(200, "application/json", json);
}

static void handleMotionApi() {
  String action = server.arg("action");
  if (action.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing action\"}");
    return;
  }

  if (action == "status") {
    sendMotionStateJson();
    return;
  }

  if (action == "start") {
    actuator_setMotionOn(true);
    sendMotionStateJson();
    return;
  }

  if (action == "stop") {
    actuator_setMotionOn(false);
    sendMotionStateJson();
    return;
  }

  if (action == "faster") {
    actuator_manualSpeedStep(+10);
    sendMotionStateJson();
    return;
  }

  if (action == "slower") {
    actuator_manualSpeedStep(-10);
    sendMotionStateJson();
    return;
  }

  if (action == "setspeed") {
    if (!server.hasArg("sp")) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing sp\"}");
      return;
    }
    int sp = server.arg("sp").toInt();
    actuator_setManualSpeedPercent(sp);
    sendMotionStateJson();
    return;
  }

  if (action == "setpattern") {
    if (!server.hasArg("mode")) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing mode\"}");
      return;
    }
    int mode = server.arg("mode").toInt();
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    actuator_setManualPatternMode((uint8_t)mode);
    sendMotionStateJson();
    return;
  }

  if (action == "setcrop") {
    if (!server.hasArg("lower") || !server.hasArg("upper")) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing lower/upper\"}");
      return;
    }
    float lower = server.arg("lower").toFloat();
    float upper = server.arg("upper").toFloat();
    if (lower < 0.0f) lower = 0.0f;
    if (lower > 1.0f) lower = 1.0f;
    if (upper < 0.0f) upper = 0.0f;
    if (upper > 1.0f) upper = 1.0f;
    actuator_setManualCropping(lower, upper);
    sendMotionStateJson();
    return;
  }

  server.send(400, "application/json", "{\"ok\":false,\"err\":\"unknown action\"}");
}

// ---------- Motion start/stop (form POST) ----------
static void handleMotionStartPost() {
  actuator_setMotionOn(true);
  server.sendHeader("Location", "/motion", true);
  server.send(303, "text/plain", "");
}

static void handleMotionStopPost() {
  actuator_setMotionOn(false);
  server.sendHeader("Location", "/motion", true);
  server.send(303, "text/plain", "");
}

// ---------- Config POST: motion & sensors ----------
static void handleConfigPost() {
  if (!server.hasArg("kp") || !server.hasArg("stallMs")) {
    server.send(400, "text/plain", "Missing kp or stallMs");
    return;
  }

  float newKp = server.arg("kp").toFloat();
  if (newKp < 0.01f) newKp = 0.01f;
  if (newKp > 1.0f)  newKp = 1.0f;

  unsigned long newStall = (unsigned long)server.arg("stallMs").toInt();
  if (newStall < 100)   newStall = 100;
  if (newStall > 10000) newStall = 10000;

  int newMaxPwm = g_cfg.maxPwm;
  if (server.hasArg("maxPwm")) newMaxPwm = server.arg("maxPwm").toInt();
  if (newMaxPwm < 0)            newMaxPwm = 0;
  if (newMaxPwm > PWM_MAX_DUTY) newMaxPwm = PWM_MAX_DUTY;

  int newHomPwm = g_cfg.homingPwm;
  if (server.hasArg("homPwm")) newHomPwm = server.arg("homPwm").toInt();
  if (newHomPwm < 0)            newHomPwm = 0;
  if (newHomPwm > PWM_MAX_DUTY) newHomPwm = PWM_MAX_DUTY;

  bool newIr1Low = server.hasArg("ir1low");
  bool newIr3Low = server.hasArg("ir3low");

  int newThrAdc = g_cfg.thermalThresholdAdc;
  if (server.hasArg("thrAdc")) newThrAdc = server.arg("thrAdc").toInt();
  if (newThrAdc < 0)    newThrAdc = 0;
  if (newThrAdc > 4095) newThrAdc = 4095;

  int newOvrIr1 = g_cfg.overshootIr1Counts;
  if (server.hasArg("ovrIr1")) newOvrIr1 = server.arg("ovrIr1").toInt();

  int newOvrIr3 = g_cfg.overshootIr3Counts;
  if (server.hasArg("ovrIr3")) newOvrIr3 = server.arg("ovrIr3").toInt();

  uint8_t newHomeMode = g_cfg.homeMode;
  if (server.hasArg("homeMode")) {
    int hm = server.arg("homeMode").toInt();
    if (hm < 0) hm = 0;
    if (hm > 2) hm = 2;
    newHomeMode = (uint8_t)hm;
  }

  uint8_t newBootSound = g_netCfg.bootSound;
  if (server.hasArg("bootSound")) {
    int bs = server.arg("bootSound").toInt();
    if (bs < 0) bs = 0;
    if (bs > 10) bs = 10;
    newBootSound = (uint8_t)bs;
  }

  g_cfg.kp                  = newKp;
  g_cfg.stallTimeoutMs      = newStall;
  g_cfg.ir1ActiveLow        = newIr1Low;
  g_cfg.ir3ActiveLow        = newIr3Low;
  g_cfg.thermalThresholdAdc = newThrAdc;
  g_cfg.overshootIr1Counts  = newOvrIr1;
  g_cfg.overshootIr3Counts  = newOvrIr3;
  g_cfg.homeMode            = newHomeMode;
  g_cfg.maxPwm              = newMaxPwm;
  g_cfg.homingPwm           = newHomPwm;

  g_netCfg.bootSound        = newBootSound;

  config_save();
  netcfg_save();

  server.send(200, "text/html", html_pageConfigSaved());

  delay(1000);
  ESP.restart();
}

// ---------- Debug page ----------
static void handleDebugGet() {
  // Download log as text file
  if (server.hasArg("download")) {
    String logTxt = debug_getLog();
    server.sendHeader("Content-Disposition", "attachment; filename=\"openhandy-debug.txt\"");
    server.sendHeader("Content-Type", "text/plain");
    server.send(200, "text/plain", logTxt);
    return;
  }

  // Clear log
  if (server.hasArg("clear")) {
    debug_clearLog();
  }

  bool ir1, ir3, limits, collErr, thermErr;
  long encAbs, encAxis, posMin, posMax;
  int  thermAdc;
  RunState st;

  debug_getStatus(ir1, ir3, encAbs, encAxis, thermAdc,
                  st, limits, posMin, posMax, collErr, thermErr);

  String log = debug_getLog();

  server.send(
    200,
    "text/html",
    html_pageDebug(
      st,
      ir1, ir3,
      encAbs, encAxis,
      limits, posMin, posMax,
      thermAdc,
      collErr, thermErr,
      log
    )
  );
}


// ---------- Reboot ----------
static void handleReboot() {
  server.send(200, "text/html", html_pageReboot());

  delay(500);
  ESP.restart();
}

// ---------- OTA update ----------
static void handleUpdateGet() {
  server.send(200, "text/html", html_pageUpdateForm());
}

static void handleUpdatePost() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    debug_append("OTA: start '" + upload.filename + "'");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      debug_append("OTA: begin failed");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      debug_append("OTA: write failed");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      debug_append("OTA: success, rebooting");
      server.send(
        200,
        "text/html",
        html_pageUpdateResult(true, String("Firmware updated. Rebooting..."))
      );
      delay(1000);
      ESP.restart();
    } else {
      Update.printError(Serial);
      debug_append("OTA: end failed");
      server.send(
        500,
        "text/html",
        html_pageUpdateResult(false, String("Update failed."))
      );
    }
  }
}

// ---------- WiFi scan API (for SSID dropdown) ----------
static void handleWifiScanApi() {
  int16_t n = WiFi.scanNetworks();
  if (n < 0) {
    server.send(500, "application/json",
                "{\"ok\":false,\"err\":\"scan failed\"}");
    return;
  }

  String json = "{\"ok\":true,\"nets\":[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";

    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    json += "{";
    json += "\"ssid\":\"" + jsonEscape(ssid) + "\"";
    json += ",\"rssi\":" + String(rssi);
    json += ",\"open\":" + String(isOpen ? "true" : "false");
    json += "}";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

// ---------- Network config (WiFi + UDP port) ----------
static void handleNetGet() {
  server.send(200, "text/html", html_pageNetConfig());
}

static void handleNetPost() {
  g_netCfg.ssid     = server.arg("ssid");
  g_netCfg.password = server.arg("pass");
  g_netCfg.hostname = server.arg("host");
  g_netCfg.useDhcp  = server.hasArg("dhcp");
  g_netCfg.ip       = server.arg("ip");
  g_netCfg.gateway  = server.arg("gw");
  g_netCfg.netmask  = server.arg("mask");
  g_netCfg.dns      = server.arg("dns");

  // UDP port
  uint16_t newUdpPort = g_netCfg.udpPort;
  if (server.hasArg("udpport")) {
    int p = server.arg("udpport").toInt();
    if (p <= 0)    p = 9000;   // sane default
    if (p > 65535) p = 65535;
    newUdpPort = (uint16_t)p;
  }
  g_netCfg.udpPort = newUdpPort;

  netcfg_save();

  server.send(200, "text/html", html_pageNetSaved());

  delay(1000);
  ESP.restart();
}

// ---------- 404 / captive ----------
static void handleNotFound() {
  if (dnsActive) {
    handleRoot();  // captive portal: always send main UI
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------- route registration ----------
static void wifi_registerRoutes() {
  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/config",       HTTP_POST, handleConfigPost);

  server.on("/settings",     HTTP_GET,  handleSettingsGet);

  server.on("/motion",       HTTP_GET,  handleMotionGet);
  server.on("/motion/start", HTTP_POST, handleMotionStartPost);
  server.on("/motion/stop",  HTTP_POST, handleMotionStopPost);
  server.on("/api/motion",   HTTP_GET,  handleMotionApi);

  server.on("/debug",        HTTP_GET,  handleDebugGet);
  server.on("/reboot",       HTTP_GET,  handleReboot);
  server.on("/update",       HTTP_GET,  handleUpdateGet);
  server.on("/update",       HTTP_POST, [](){}, handleUpdatePost);

  server.on("/net",          HTTP_GET,  handleNetGet);
  server.on("/net",          HTTP_POST, handleNetPost);

  // WiFi scan API for SSID dropdown
  server.on("/api/wifiscan", HTTP_GET,  handleWifiScanApi);

  server.onNotFound(handleNotFound);
}

// ---------- WiFi helpers ----------
static bool wifi_startStation() {
  netcfg_load();
  Serial.println("wifi_startStation():");
  Serial.print("  ssid='"); Serial.print(g_netCfg.ssid); Serial.println("'");
  Serial.print("  host='"); Serial.print(g_netCfg.hostname); Serial.println("'");
  Serial.print("  useDhcp="); Serial.println(g_netCfg.useDhcp ? "true" : "false");
  if (g_netCfg.ssid.length() == 0) {
    Serial.println("  no SSID configured, aborting STA");
    return false;
  }
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  String host = g_netCfg.hostname.length() > 0 ? g_netCfg.hostname : "openhandy";
  WiFi.setHostname(host.c_str());
  if (!g_netCfg.useDhcp &&
      g_netCfg.ip.length() > 0 &&
      g_netCfg.gateway.length() > 0 &&
      g_netCfg.netmask.length() > 0) {
    IPAddress ip, gw, mask, dns1;
    ip.fromString(g_netCfg.ip);
    gw.fromString(g_netCfg.gateway);
    mask.fromString(g_netCfg.netmask);
    if (g_netCfg.dns.length() > 0) dns1.fromString(g_netCfg.dns);
    Serial.print("  static IP: "); Serial.println(ip);
    WiFi.config(ip, gw, mask, dns1);
  } else {
    Serial.println("  DHCP mode");
  }

  Serial.print("  connecting to '"); Serial.print(g_netCfg.ssid); Serial.println("'");

  WiFi.begin(g_netCfg.ssid.c_str(), g_netCfg.password.c_str());
  led_setMode(LEDMODE_DISCOVERY_BREATHING);
  unsigned long t0 = millis();
  const unsigned long timeoutMs = 8000;
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    led_loop();
    delay(25);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  WiFi connect failed");
    return false;
  }
  IPAddress ip = WiFi.localIP();
  Serial.print("  connected, IP="); Serial.println(ip);
  dnsActive   = false;
  mdnsStarted = false;
  if (MDNS.begin(host.c_str())) {
    mdnsStarted = true;
    Serial.print("  mDNS responder started: ");
    Serial.print(host);
    Serial.println(".local");
  } else {
    Serial.println("  mDNS.begin failed");
  }
  wifi_registerRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80 (station mode)");
  return true;
}

// ---------- Public WiFi API ----------
void wifi_startRecoveryMode() {
  Serial.println("wifi_startRecoveryMode(): AP + captive portal");
  g_recoveryMode = true;
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  if (!ok) {
    Serial.println("  WiFi.softAP failed");
  }
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("  AP SSID: "); Serial.println(AP_SSID);
  Serial.print("  AP IP:   "); Serial.println(apIP);

  dnsServer.start(DNS_PORT, "*", apIP);
  dnsActive = true;

  wifi_registerRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80 (AP mode)");
}

void wifi_begin() {
  // If boot logic already marked recovery mode, go straight to AP/captive
  if (g_recoveryMode) {
    wifi_startRecoveryMode();
    return;
  }
  // Normal boot: try station, fall back to AP/captive if it fails
  if (!wifi_startStation()) {
    Serial.println("wifi_begin(): station failed, falling back to AP");
    g_recoveryMode = true;
    wifi_startRecoveryMode();
  }
}

void wifi_loop() {
  if (dnsActive) dnsServer.processNextRequest();
  server.handleClient();
  delay(5);
}
