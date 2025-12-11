// 02_led.ino
#include "openhandy.h"

// Internal LED state
static LedMode      s_mode      = LEDMODE_OFF;
static unsigned long s_lastTick = 0;
static bool         s_phase     = false;


static void led_writeLogical(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t physR = g; // PIN_LED_R = physical GREEN
  uint8_t physG = b; // PIN_LED_G = physical BLUE
  uint8_t physB = r; // PIN_LED_B = physical RED

  digitalWrite(PIN_LED_R, physR ? HIGH : LOW);
  digitalWrite(PIN_LED_G, physG ? HIGH : LOW);
  digitalWrite(PIN_LED_B, physB ? HIGH : LOW);
}

void led_setRGB(uint8_t r, uint8_t g, uint8_t b) {
  led_writeLogical(r, g, b);
}

void led_off() {
  led_setRGB(0, 0, 0);
}

void led_red() {
  led_setRGB(1, 0, 0);
}

void led_green() {
  led_setRGB(0, 1, 0);
}

void led_yellow() {
  led_setRGB(1, 1, 0);
}

void led_violet() {
  led_setRGB(1, 0, 1);
}

void led_blue() {
  led_setRGB(0, 0, 1);
}

void led_init() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  s_mode      = LEDMODE_BOOTING;
  s_lastTick  = millis();
  s_phase     = false;

  led_green();  // boot start
}

void led_setMode(LedMode mode) {
  if (mode == s_mode) return;

  s_mode     = mode;
  s_lastTick = millis();
  s_phase    = false;

  switch (s_mode) {
    case LEDMODE_OFF:
      led_off();
      break;

    case LEDMODE_BOOTING:
      led_green();
      break;

    case LEDMODE_READY:
      led_green();
      break;

    case LEDMODE_RECOVERY_HOLD:
      led_red();
      break;

    case LEDMODE_RECOVERY_ACTIVE:
      led_violet();
      break;

    case LEDMODE_STATE_HOMING:
      led_violet();
      break;

    case LEDMODE_STATE_ERROR:
      led_red();
      break;

    case LEDMODE_STATE_COLLISION:
      led_red();
      break;

    case LEDMODE_STATE_THERMAL:
      led_red();
      break;

    case LEDMODE_STATE_UDP:
      led_blue();
      break;

    // NEW MODE → initial state for breathing
    case LEDMODE_DISCOVERY_BREATHING:
      led_blue();
      break;
  }
}

void led_loop() {
  unsigned long now = millis();

  switch (s_mode) {

    // Boot animation: green ↔ violet
    case LEDMODE_BOOTING: {
      const unsigned long interval = 700;
      if (now - s_lastTick >= interval) {
        s_lastTick = now;
        s_phase = !s_phase;
        if (s_phase) led_green();
        else         led_violet();
      }
      break;
    }

    // HOMING: violet blinking
    case LEDMODE_STATE_HOMING: {
      const unsigned long interval = 300;
      if (now - s_lastTick >= interval) {
        s_lastTick = now;
        s_phase = !s_phase;
        if (s_phase) led_violet();
        else         led_off();
      }
      break;
    }

    // COLLISION: fast red flicker
    case LEDMODE_STATE_COLLISION: {
      const unsigned long interval = 100;
      if (now - s_lastTick >= interval) {
        s_lastTick = now;
        s_phase = !s_phase;
        if (s_phase) led_red();
        else         led_off();
      }
      break;
    }

    // THERMAL: red/yellow alternating
    case LEDMODE_STATE_THERMAL: {
      const unsigned long interval = 200;
      if (now - s_lastTick >= interval) {
        s_lastTick = now;
        s_phase = !s_phase;
        if (s_phase) led_red();
        else         led_yellow();
      }
      break;
    }

    // NEW: WiFi searching → Blue breathing (blue ↔ off)
    case LEDMODE_DISCOVERY_BREATHING: {
      const unsigned long interval = 300;  // breathing rate
      if (now - s_lastTick >= interval) {
        s_lastTick = now;
        s_phase = !s_phase;
        if (s_phase) led_blue();
        else         led_off();
      }
      break;
    }

    // Static modes:
    case LEDMODE_READY:
    case LEDMODE_RECOVERY_HOLD:
    case LEDMODE_RECOVERY_ACTIVE:
    case LEDMODE_STATE_ERROR:
    case LEDMODE_STATE_UDP:
    case LEDMODE_OFF:
    default:
      break;
  }
}
