// 06_udp_tcode.ino
#include "openhandy.h"
#include <WiFiUdp.h>
#include <WiFi.h>
#include <string.h>

static WiFiUDP udp;
static uint16_t s_udpPortCurrent = 0;

// Discovery broadcast configuration
static const uint16_t UDP_DISCOVERY_PORT  = 5390;
static const uint8_t  UDP_DISCOVERY_COUNT = 10;
static const unsigned long UDP_DISCOVERY_INTERVAL_MS = 250;
static const char*    UDP_DISCOVERY_MAGIC = "OPENHANDY_DISCOVERY";

static bool           s_discoveryActive      = false;
static uint8_t        s_discoveryRemaining   = 0;
static unsigned long  s_discoveryNextMs      = 0;

// Forward declaration
static void udp_tcode_parseLine(const char* line);
static void udp_tcode_discoveryTick();

// ---------------------------------------------------------------------------
// Init: bind UDP socket using configured port
// ---------------------------------------------------------------------------
void udp_tcode_init() {
  // Make sure network config is loaded (usually already done in wifi_begin)
  netcfg_load();

  uint16_t port = g_netCfg.udpPort;
  if (port == 0) {
    port = 2000; // sane default if not set
  }

  s_udpPortCurrent = port;
  udp.begin(port);

  Serial.print("UDP TCode listening on port ");
  Serial.println(port);

  debug_append(String("UDP TCode listening on port ") + String(port));
}

// ---------------------------------------------------------------------------
// Discovery: start a short UDP broadcast burst with IP/hostname/ports
// ---------------------------------------------------------------------------
void udp_tcode_startDiscoveryBurst() {
  if (g_recoveryMode) {
    debug_append("UDP discovery: ignored (recovery mode)");
    return;
  }

  if (!WiFi.isConnected()) {
    debug_append("UDP discovery: WiFi not connected");
    return;
  }

  s_discoveryActive    = true;
  s_discoveryRemaining = UDP_DISCOVERY_COUNT;
  s_discoveryNextMs    = millis();

  led_setMode(LEDMODE_DISCOVERY_BREATHING);
  debug_append("UDP discovery: starting burst");
}

// ---------------------------------------------------------------------------
// Discovery tick: send one packet when due
// ---------------------------------------------------------------------------
static void udp_tcode_discoveryTick() {
  if (!s_discoveryActive || s_discoveryRemaining == 0) {
    return;
  }

  unsigned long now = millis();
  if (now < s_discoveryNextMs) {
    return;
  }

  IPAddress ip = WiFi.localIP();
  if ((uint32_t)ip == 0) {
    debug_append("UDP discovery: no valid IP");
  } else {
    String msg;
    msg.reserve(128);
    msg += UDP_DISCOVERY_MAGIC;
    msg += " ip=";
    msg += ip.toString();
    msg += " host=";
    msg += g_netCfg.hostname;
    msg += " tcode=";
    msg += String(s_udpPortCurrent);
    msg += " dport=";
    msg += String(UDP_DISCOVERY_PORT);
    msg += " fw=";
    msg += getFwVersionFull();

    IPAddress bcast(255, 255, 255, 255);
    udp.beginPacket(bcast, UDP_DISCOVERY_PORT);
    udp.write((const uint8_t*)msg.c_str(), msg.length());
    udp.endPacket();
  }

  s_discoveryRemaining--;
  s_discoveryNextMs = now + UDP_DISCOVERY_INTERVAL_MS;

  if (s_discoveryRemaining == 0) {
    s_discoveryActive = false;
    debug_append("UDP discovery: burst complete");
    if (!g_recoveryMode) {
      led_setMode(LEDMODE_READY);
    }
  }
}

// ---------------------------------------------------------------------------
// Parse one TCode line, e.g. "L05000" (v0) or "L0V05000" (v1 style).
// We only care about the L-axis (stroke) and ignore velocity modifiers.
// ---------------------------------------------------------------------------
static void udp_tcode_parseLine(const char* line) {
  if (!line) return;

  // Skip leading whitespace
  while (*line == ' ' || *line == '\t') {
    ++line;
  }

  if (*line == '\0') return;
  if (*line == '#')  return;  // comment line
  if (*line == ';')  return;  // another style of comment

  // We only handle L-axis here: L (v0) or L0 (v1)
  if (*line != 'L') return;
  ++line;

  // Optional channel digit (v1: L0..., we accept L0 only)
  int channel = -1;
  if (*line >= '0' && *line <= '9') {
    channel = *line - '0';
    ++line;
  }

  // If a channel is present and it's not 0, ignore
  if (channel > 0) {
    return;
  }

  // Optional 'V' in v1 (L0V05000)
  if (*line == 'V' || *line == 'v') {
    ++line;
  }

  // Read up to 5 digits as axis value (0..10000)
  char numBuf[8];
  int  n = 0;
  while (*line >= '0' && *line <= '9' && n < 5) {
    numBuf[n++] = *line++;
  }
  numBuf[n] = '\0';

  if (n == 0) {
    return; // no numeric payload
  }

  int val = atoi(numBuf);
  if (val < 0)     val = 0;
  if (val > 10000) val = 10000;

  float axis = (float)val;

  // Hand off to actuator (this enforces limits, homing, etc.)
  actuator_udpCommand(axis);
}

// ---------------------------------------------------------------------------
// Loop: receive UDP packets and feed them line-by-line to the parser,
//       then handle discovery-broadcast timing.
// ---------------------------------------------------------------------------
void udp_tcode_loop() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[256];
    if (packetSize >= (int)sizeof(buf)) {
      packetSize = sizeof(buf) - 1;
    }

    int len = udp.read(buf, packetSize);
    if (len > 0) {
      buf[len] = '\0';

      char* saveptr = nullptr;
      char* line = strtok_r(buf, (char*)"\r\n", &saveptr);
      while (line) {
        udp_tcode_parseLine(line);
        line = strtok_r(nullptr, (char*)"\r\n", &saveptr);
      }
    }
  }

  udp_tcode_discoveryTick();
}
