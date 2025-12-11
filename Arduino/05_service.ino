// service.ino
#include "openhandy.h"

// WiFi button handling:
// - Short press  → discovery UDP broadcast burst
// - Long press   → force AP recovery mode (captive portal)

static bool          s_wifiBtnPrev       = false;
static bool          s_wifiLongTriggered = false;
static unsigned long s_wifiPressStartMs  = 0;

static const unsigned long WIFI_LONG_PRESS_MS    = 5000; // 5 seconds
static const unsigned long WIFI_DEBOUNCE_SHORTMS = 50;   // debounce for short press

void service_init() {
}

void service_loop() {
  bool wifiPressed = (digitalRead(PIN_BTN_WIFI) == LOW);
  unsigned long now = millis();

  // Rising edge: button pressed down
  if (wifiPressed && !s_wifiBtnPrev) {
    s_wifiPressStartMs  = now;
    s_wifiLongTriggered = false;
  }

  // Button held: check for long-press
  if (wifiPressed && s_wifiBtnPrev) {
    if (!s_wifiLongTriggered && (now - s_wifiPressStartMs) >= WIFI_LONG_PRESS_MS) {
      // Long-press: force AP recovery mode
      debug_append("WiFi button long-press: entering AP mode");

      g_recoveryMode = true;
      led_setMode(LEDMODE_RECOVERY_ACTIVE);

      wifi_startRecoveryMode();
      s_wifiLongTriggered = true;
    }
  }

  // Falling edge: button released
  if (!wifiPressed && s_wifiBtnPrev) {
    unsigned long dt = now - s_wifiPressStartMs;

    // Short press: start discovery UDP broadcast burst
    if (!s_wifiLongTriggered && dt >= WIFI_DEBOUNCE_SHORTMS && dt < WIFI_LONG_PRESS_MS) {
      debug_append("WiFi button short-press: discovery burst");
      udp_tcode_startDiscoveryBurst();
    }
  }

  s_wifiBtnPrev = wifiPressed;

  // future extensions here
}
