// 00_openhandy.ino
#include "openhandy.h"
#include <Preferences.h>

const char* FW_VERSION = "0.4.0";
const char* FW_HASH    = "md5-openhandy_v0.1_21112025_build022";

bool        g_recoveryMode = false;
HandyConfig g_cfg;

Preferences prefs;

String getFwVersionFull() {
  return String(FW_VERSION) + "-" + FW_HASH;
}

void setup() {
  led_setMode(LEDMODE_BOOTING);
  Serial.begin(115200);
  delay(200);
  Serial.print("boot, FW=");
  Serial.println(getFwVersionFull());

  // Hardware init
  config_init();
  led_init();
  config_load();
  sound_playBootOnStartup();

  actuator_init();
  service_init();

  const unsigned long LONG_PRESS_MS = 5000;
  unsigned long startMs = millis();

  bool pwrHeld = (digitalRead(PIN_BTN_PWR) == LOW);

  if (pwrHeld) {
    Serial.println("Power button held at boot; monitoring for long-press...");

    led_setMode(LEDMODE_RECOVERY_HOLD);

    while (millis() - startMs < LONG_PRESS_MS) {
      if (digitalRead(PIN_BTN_PWR) != LOW) {
        Serial.println("Power button released before 5s -> normal boot");
        pwrHeld = false;
        break;
      }
      delay(50);
    }

    if (pwrHeld) {
      Serial.println("Long-press detected -> entering recovery, yellow flicker then violet");

      for (int i = 0; i < 4; ++i) {
        if (i % 2 == 0) led_yellow();
        else            led_off();
        delay(500);
      }

      g_recoveryMode = true;
      led_setMode(LEDMODE_RECOVERY_ACTIVE);

      wifi_startRecoveryMode();
      return;
    } else {
      led_setMode(LEDMODE_BOOTING);
    }
  }

  Serial.println("Normal mode boot");

  wifi_begin();        // may set g_recoveryMode = true on WiFi failure
  udp_tcode_init();

  if (g_recoveryMode) {
    // WiFi failed → AP / recovery mode, but we STILL allow full motion + buttons
    led_setMode(LEDMODE_RECOVERY_ACTIVE);   // violet
  } else {
    // WiFi OK → normal manual/UDP mode
    led_setMode(LEDMODE_READY);             // green
  }
}

void loop() {
  led_loop();
  wifi_loop();
  udp_tcode_loop();
  //if (g_recoveryMode) return;
  actuator_loop();
  service_loop();
}
