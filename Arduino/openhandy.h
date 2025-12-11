#pragma once

#include <Arduino.h>

// ============================================================================
//  Motor state
// ============================================================================
enum RunState {
  STATE_IDLE = 0,
  STATE_HOMING,
  STATE_READY,
  STATE_RUNNING,
  STATE_PATTERN,
  STATE_ERROR,
  STATE_ERROR_COLLISION,
  STATE_ERROR_THERMAL
};

// Firmware info
extern const char* FW_VERSION;
extern const char* FW_HASH;
String getFwVersionFull();

// ============================================================================
// Unified persistent configuration namespace
// - Both HandyConfig (motor/sensors) and NetworkConfig (WiFi/hostname/UDP)
//   are stored in a single NVS namespace so a FW MD5/version change can
//   reliably wipe ALL configuration in one place.
// ============================================================================
extern const char* NVS_NAMESPACE_CONFIG;

// ============================================================================
// Persistent configuration (Motor / Sensors)
// ============================================================================
struct HandyConfig {
  int           minPwmRun;
  unsigned long stallTimeoutMs;
  float         kp;
  bool          ir1ActiveLow;
  bool          ir3ActiveLow;
  int           thermalThresholdAdc;
  int           overshootIr1Counts;
  int           overshootIr3Counts;
  uint8_t       homeMode;
  int           maxPwm;
  int           homingPwm;
  float         patternBaseCps;       // counts/sec per 1% speed
  float         axisDeadband;         // axis deadband (0..10000)
  float         kpSpeedScaleMin;      // low-speed Kp scale
};

// ============================================================================
// Network configuration
// - Stored alongside HandyConfig in the unified NVS namespace.
// - hostname is used for both DHCP hostname and mDNS (<hostname>.local).
// ============================================================================
struct NetworkConfig {
  String   ssid;
  String   password;
  String   hostname;
  bool     useDhcp;
  String   ip;
  String   gateway;
  String   netmask;
  String   dns;
  uint16_t udpPort;
  uint8_t  bootSound;
};

// Global objects
extern HandyConfig   g_cfg;
extern NetworkConfig g_netCfg;

extern bool g_recoveryMode;

// ============================================================================
// NVS: config load/save (unified motor + network config)
// ============================================================================
void config_init();
void config_load();
void config_save();

// Network config helpers (operate on the same unified namespace)
void netcfg_load();
void netcfg_save();

// ============================================================================
// WiFi / Portal / OTA
// ============================================================================
void wifi_begin();
void wifi_startRecoveryMode();
void wifi_loop();

// ============================================================================
// Actuator (motor)
// ============================================================================
void actuator_init();
void actuator_loop();
void actuator_udpCommand(float axis);
void actuator_setPatternMode(uint8_t mode);

// ============================================================================
// Motion / Manual UI Extensions
// ============================================================================
bool actuator_getMotionOn();
void actuator_setMotionOn(bool on);

// Pattern modes (for manual mode)
enum PatternMode : uint8_t {
  PATTERN_SINE = 0,
  PATTERN_BOUNCE = 1,
  PATTERN_DOUBLE_BOUNCE = 2
};

// Read live manual UI state
void actuator_getManualUiState(int &speedPercent,
                               float &cutLowerFrac,
                               float &cutUpperFrac,
                               uint8_t &patternMode);

// Change speed (+10 / -10 via UI buttons)
void actuator_manualSpeedStep(int delta);

// Set speed directly (0..100)
void actuator_setManualSpeedPercent(int sp);

// Set cropping from UI (0..1)
void actuator_setManualCropping(float lowerFrac, float upperFrac);

// Directly set pattern mode (sine / bounce / dbl bounce)
void actuator_setManualPatternMode(uint8_t mode);

// ---- Actuator (motor) ----
void actuator_init();
void actuator_loop();
void actuator_udpCommand(float axis);

// manual control helpers (web/API)
void actuator_getManualUiState(int &speedPercent,
                               float &cutLowerFrac,
                               float &cutUpperFrac,
                               uint8_t &patternMode);
void actuator_manualSpeedStep(int delta);
void actuator_setManualSpeedPercent(int sp);
void actuator_setManualCropping(float lowerFrac, float upperFrac);
void actuator_setManualPatternMode(uint8_t mode);

// NEW: start/stop motion from web/API
bool actuator_getMotionOn();
void actuator_setMotionOn(bool on);

// ============================================================================
// Service (buttons, safety, integrations)
// ============================================================================
void service_init();
void service_loop();

// ============================================================================
// Pins (exported from 01_configuration.ino)
// ============================================================================
extern const int PIN_BRAKEZ;
extern const int PIN_ENABLE;
extern const int PIN_DIR;

extern const int PIN_IR1;
extern const int PIN_IR3;
extern const int PIN_THERMAL;

extern const int PIN_BTN_PWR;
extern const int PIN_BTN_UP;
extern const int PIN_BTN_DOWN;
extern const int PIN_BTN_LEFT;
extern const int PIN_BTN_RIGHT;
extern const int PIN_BTN_WIFI;

extern const int PIN_HA;
extern const int PIN_HB;
extern const int PIN_HC;

extern const int PIN_LED_R;
extern const int PIN_LED_G;
extern const int PIN_LED_B;

// ============================================================================
// PWM settings
// ============================================================================
extern const int PWM_CHANNEL;
extern const int PWM_FREQ_HZ;
extern const int PWM_RESOLUTION;
extern const int PWM_MAX_DUTY;

// ============================================================================
// LED modes
// ============================================================================
enum LedMode {
  LEDMODE_OFF = 0,
  LEDMODE_BOOTING,
  LEDMODE_READY,
  LEDMODE_RECOVERY_HOLD,
  LEDMODE_RECOVERY_ACTIVE,
  LEDMODE_STATE_HOMING,
  LEDMODE_STATE_ERROR,
  LEDMODE_STATE_COLLISION,
  LEDMODE_STATE_THERMAL,
  LEDMODE_STATE_UDP,
  LEDMODE_WIFI_SCANNING,
  LEDMODE_WIFI_CONNECTING,
  LEDMODE_WIFI_ERROR,
  LEDMODE_WIFI_SUCCESS,
  LEDMODE_DISCOVERY_BREATHING   // breathing blue while sending discovery broadcast
};

// LED control interface (full original)
void led_init();
void led_setRGB(uint8_t r, uint8_t g, uint8_t b);
void led_off();
void led_red();
void led_green();
void led_yellow();
void led_violet();
void led_setMode(LedMode mode);
void led_loop();

// ============================================================================
// Motor helpers
// ============================================================================
void setPwm(int duty);
void setBrake(bool on);
void setDirectionUp();
void setDirectionDown();

// ============================================================================
// Debug / diagnostics
// ============================================================================
void debug_append(const String &line);
String debug_getLog();
void debug_clearLog();
void debug_getStatus(bool &ir1Active, bool &ir3Active,
                     long &encoderAbs, long &encoderAxis,
                     int &thermalAdc,
                     RunState &state,
                     bool &limitsValid,
                     long &posMin, long &posMax,
                     bool &collisionError,
                     bool &thermalError);

// ============================================================================
// TCode / UDP
// ============================================================================
void udp_tcode_init();
void udp_tcode_loop();

// Discovery burst: broadcast device IP/hostname/UDP port a few times after
// a short WiFi button press. Implemented in 06_udp_tcode.ino.
void udp_tcode_startDiscoveryBurst();

// ============================================================================
// Sound / boot jingles
// ============================================================================
struct BootNote {
  uint16_t freq;       // Hertz (0 = rest)
  uint16_t durationMs; // Duration in milliseconds
};

// Play boot sound based on g_netCfg.bootSound (0 = none, 1 = imperial, ...)
void sound_playBootOnStartup();
