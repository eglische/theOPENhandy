// 01_configuration.ino
#include "openhandy.h"
#include <Preferences.h>

// pins
const int PIN_BRAKEZ   = 18;
const int PIN_ENABLE   = 19;
const int PIN_DIR      = 21;
const int PIN_IR1      = 34;
const int PIN_IR3      = 35;
const int PIN_THERMAL  = 33;

const int PIN_BTN_PWR   = 2;
const int PIN_BTN_UP    = 15;
const int PIN_BTN_DOWN  = 14;
const int PIN_BTN_LEFT  = 12;
const int PIN_BTN_RIGHT = 13;
const int PIN_BTN_WIFI  = 27;   // WiFi/portal button

const int PIN_HA       = 23;
const int PIN_HB       = 22;
const int PIN_HC       = 4;

const int PIN_LED_R    = 5;     // PIN_LED_R is GREEN, PIN_LED_G is BLUE PIN_LED_B is RED (sorry)
const int PIN_LED_G    = 16;
const int PIN_LED_B    = 17;

// pwm
const int PWM_CHANNEL    = 0;
const int PWM_FREQ_HZ    = 20000;
const int PWM_RESOLUTION = 10;
const int PWM_MAX_DUTY   = (1 << PWM_RESOLUTION) - 1;

// defaults
static const int           CFG_DEFAULT_MIN_PWM_RUN           = 160;
static const unsigned long CFG_DEFAULT_STALL_TIMEOUT_MS      = 1000;
static const float         CFG_DEFAULT_KP                    = 0.20f;
static const bool          CFG_DEFAULT_IR1_ACTIVE_LOW        = true;
static const bool          CFG_DEFAULT_IR3_ACTIVE_LOW        = true;
static const int           CFG_DEFAULT_THERMAL_THRESHOLD_ADC = 1500;
static const int           CFG_DEFAULT_OVERSHOOT_IR1         = 0;
static const int           CFG_DEFAULT_OVERSHOOT_IR3         = 0;
static const uint8_t       CFG_DEFAULT_HOME_MODE             = 2;
static const int           CFG_DEFAULT_MAX_PWM               = 1023;
static const int           CFG_DEFAULT_HOMING_PWM            = 180;

// new tuning defaults
static const float         CFG_DEFAULT_PATTERN_BASE_CPS      = 14.0f;
static const float         CFG_DEFAULT_AXIS_DEADBAND         = 30.0f;
static const float         CFG_DEFAULT_KP_SPEED_SCALE_MIN    = 1.0f;
static const uint8_t       CFG_DEFAULT_BOOT_SOUND            = 1;

// UDP default port
static const uint16_t      CFG_DEFAULT_UDP_PORT              = 8000;

const char* NVS_NAMESPACE_CONFIG = "openhandycfg";

extern Preferences prefs;
extern HandyConfig g_cfg;

NetworkConfig g_netCfg;

void config_init() {
  pinMode(PIN_BRAKEZ, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);
  pinMode(PIN_DIR,    OUTPUT);

  pinMode(PIN_IR1, INPUT);
  pinMode(PIN_IR3, INPUT);
  pinMode(PIN_THERMAL, INPUT);

  pinMode(PIN_BTN_PWR,   INPUT_PULLUP);
  pinMode(PIN_BTN_UP,    INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_BTN_WIFI,  INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);

  pinMode(PIN_HA, INPUT);
  pinMode(PIN_HB, INPUT);
  pinMode(PIN_HC, INPUT);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  // PWM init
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION);
  ledcAttachPin(PIN_ENABLE, PWM_CHANNEL);

  // default disabled (max duty, enable low)
  ledcWrite(PWM_CHANNEL, PWM_MAX_DUTY);
  digitalWrite(PIN_BRAKEZ, LOW);
  digitalWrite(PIN_DIR, HIGH);
}

void config_load() {
  // Use unified NVS namespace for both motor and network configuration
  prefs.begin(NVS_NAMESPACE_CONFIG, false);

  String currentVer = getFwVersionFull();
  String storedVer  = prefs.getString("ver", "");

  if (storedVer != currentVer) {
    Serial.println("Config: FW version changed, clearing NVS and loading defaults");
    // Wipe all stored config (motor + network) and repopulate with defaults
    prefs.clear();
    prefs.putString("ver", currentVer);

    // ---- Motor defaults ----
    g_cfg.minPwmRun           = CFG_DEFAULT_MIN_PWM_RUN;
    g_cfg.stallTimeoutMs      = CFG_DEFAULT_STALL_TIMEOUT_MS;
    g_cfg.kp                  = CFG_DEFAULT_KP;
    g_cfg.ir1ActiveLow        = CFG_DEFAULT_IR1_ACTIVE_LOW;
    g_cfg.ir3ActiveLow        = CFG_DEFAULT_IR3_ACTIVE_LOW;
    g_cfg.thermalThresholdAdc = CFG_DEFAULT_THERMAL_THRESHOLD_ADC;
    g_cfg.overshootIr1Counts  = CFG_DEFAULT_OVERSHOOT_IR1;
    g_cfg.overshootIr3Counts  = CFG_DEFAULT_OVERSHOOT_IR3;
    g_cfg.homeMode            = CFG_DEFAULT_HOME_MODE;
    g_cfg.maxPwm              = CFG_DEFAULT_MAX_PWM;
    g_cfg.homingPwm           = CFG_DEFAULT_HOMING_PWM;
    g_cfg.patternBaseCps      = CFG_DEFAULT_PATTERN_BASE_CPS;
    g_cfg.axisDeadband        = CFG_DEFAULT_AXIS_DEADBAND;
    g_cfg.kpSpeedScaleMin     = CFG_DEFAULT_KP_SPEED_SCALE_MIN;

    // Persist motor defaults
    prefs.putInt("minPwm",     g_cfg.minPwmRun);
    prefs.putULong("stallMs",  g_cfg.stallTimeoutMs);
    prefs.putFloat("kp",       g_cfg.kp);
    prefs.putBool("ir1Low",    g_cfg.ir1ActiveLow);
    prefs.putBool("ir3Low",    g_cfg.ir3ActiveLow);
    prefs.putInt("thrAdc",     g_cfg.thermalThresholdAdc);
    prefs.putInt("ovrIr1",     g_cfg.overshootIr1Counts);
    prefs.putInt("ovrIr3",     g_cfg.overshootIr3Counts);
    prefs.putUChar("homeMode", g_cfg.homeMode);
    prefs.putInt("maxPwm",     g_cfg.maxPwm);
    prefs.putInt("homPwm",     g_cfg.homingPwm);
    prefs.putFloat("patBase",  g_cfg.patternBaseCps);
    prefs.putFloat("axisDb",   g_cfg.axisDeadband);
    prefs.putFloat("kpScale",  g_cfg.kpSpeedScaleMin);

    // ---- Network defaults ----
    g_netCfg.ssid      = "";
    g_netCfg.password  = "";
    g_netCfg.hostname  = "openhandy";
    g_netCfg.useDhcp   = true;
    g_netCfg.ip        = "";
    g_netCfg.gateway   = "";
    g_netCfg.netmask   = "";
    g_netCfg.dns       = "";
    g_netCfg.udpPort   = CFG_DEFAULT_UDP_PORT;
    g_netCfg.bootSound = CFG_DEFAULT_BOOT_SOUND;

    // Persist network defaults in the same namespace
    prefs.putString("ssid", g_netCfg.ssid);
    prefs.putString("pass", g_netCfg.password);
    prefs.putString("host", g_netCfg.hostname);
    prefs.putUShort("udpPort", g_netCfg.udpPort);
    prefs.putBool("dhcp",  g_netCfg.useDhcp);
    prefs.putString("ip",   g_netCfg.ip);
    prefs.putString("gw",   g_netCfg.gateway);
    prefs.putString("mask", g_netCfg.netmask);
    prefs.putString("dns",  g_netCfg.dns);
    prefs.putUChar("bootSound", g_netCfg.bootSound);

  } else {
    // ---- Motor config from NVS ----
    g_cfg.minPwmRun           = prefs.getInt("minPwm",     CFG_DEFAULT_MIN_PWM_RUN);
    g_cfg.stallTimeoutMs      = prefs.getULong("stallMs",  CFG_DEFAULT_STALL_TIMEOUT_MS);
    g_cfg.kp                  = prefs.getFloat("kp",       CFG_DEFAULT_KP);
    g_cfg.ir1ActiveLow        = prefs.getBool("ir1Low",    CFG_DEFAULT_IR1_ACTIVE_LOW);
    g_cfg.ir3ActiveLow        = prefs.getBool("ir3Low",    CFG_DEFAULT_IR3_ACTIVE_LOW);
    g_cfg.thermalThresholdAdc = prefs.getInt("thrAdc",     CFG_DEFAULT_THERMAL_THRESHOLD_ADC);
    g_cfg.overshootIr1Counts  = prefs.getInt("ovrIr1",     CFG_DEFAULT_OVERSHOOT_IR1);
    g_cfg.overshootIr3Counts  = prefs.getInt("ovrIr3",     CFG_DEFAULT_OVERSHOOT_IR3);
    g_cfg.homeMode            = prefs.getUChar("homeMode", CFG_DEFAULT_HOME_MODE);
    g_cfg.maxPwm              = prefs.getInt("maxPwm",     CFG_DEFAULT_MAX_PWM);
    g_cfg.homingPwm           = prefs.getInt("homPwm",     CFG_DEFAULT_HOMING_PWM);
    g_cfg.patternBaseCps      = prefs.getFloat("patBase",  CFG_DEFAULT_PATTERN_BASE_CPS);
    g_cfg.axisDeadband        = prefs.getFloat("axisDb",   CFG_DEFAULT_AXIS_DEADBAND);
    g_cfg.kpSpeedScaleMin     = prefs.getFloat("kpScale",  CFG_DEFAULT_KP_SPEED_SCALE_MIN);

    // ---- Network config from NVS ----
    g_netCfg.ssid     = prefs.getString("ssid", "");
    g_netCfg.password = prefs.getString("pass", "");
    g_netCfg.hostname = prefs.getString("host", "openhandy");
    g_netCfg.useDhcp  = prefs.getBool("dhcp", true);
    g_netCfg.ip       = prefs.getString("ip", "");
    g_netCfg.gateway  = prefs.getString("gw", "");
    g_netCfg.netmask  = prefs.getString("mask", "");
    g_netCfg.dns      = prefs.getString("dns", "");
    uint16_t udpPort  = prefs.getUShort("udpPort", CFG_DEFAULT_UDP_PORT);
    g_netCfg.bootSound = prefs.getUChar("bootSound", CFG_DEFAULT_BOOT_SOUND);
    if (udpPort == 0) {
      udpPort = CFG_DEFAULT_UDP_PORT;
    }
    g_netCfg.udpPort = udpPort;
  }

  Serial.print("Config loaded: minPwmRun=");
  Serial.print(g_cfg.minPwmRun);
  Serial.print(" stallTimeoutMs=");
  Serial.print(g_cfg.stallTimeoutMs);
  Serial.print(" kp=");
  Serial.print(g_cfg.kp);
  Serial.print(" homeMode=");
  Serial.print(g_cfg.homeMode);
  Serial.print(" maxPwm=");
  Serial.print(g_cfg.maxPwm);
  Serial.print(" homPwm=");
  Serial.print(g_cfg.homingPwm);
  Serial.print(" patBase=");
  Serial.print(g_cfg.patternBaseCps);
  Serial.print(" axisDb=");
  Serial.print(g_cfg.axisDeadband);
  Serial.print(" kpScale=");
  Serial.println(g_cfg.kpSpeedScaleMin);
}

void config_save() {
  prefs.putInt("minPwm",     g_cfg.minPwmRun);
  prefs.putULong("stallMs",  g_cfg.stallTimeoutMs);
  prefs.putFloat("kp",       g_cfg.kp);
  prefs.putBool("ir1Low",    g_cfg.ir1ActiveLow);
  prefs.putBool("ir3Low",    g_cfg.ir3ActiveLow);
  prefs.putInt("thrAdc",     g_cfg.thermalThresholdAdc);
  prefs.putInt("ovrIr1",     g_cfg.overshootIr1Counts);
  prefs.putInt("ovrIr3",     g_cfg.overshootIr3Counts);
  prefs.putUChar("homeMode", g_cfg.homeMode);
  prefs.putInt("maxPwm",     g_cfg.maxPwm);
  prefs.putInt("homPwm",     g_cfg.homingPwm);
  prefs.putFloat("patBase",  g_cfg.patternBaseCps);
  prefs.putFloat("axisDb",   g_cfg.axisDeadband);
  prefs.putFloat("kpScale",  g_cfg.kpSpeedScaleMin);

  Serial.println("Config saved.");
}

// network cfg

void netcfg_load() {
  // Reload only network-related config from unified NVS namespace
  prefs.begin(NVS_NAMESPACE_CONFIG, false);

  g_netCfg.ssid     = prefs.getString("ssid", "");
  g_netCfg.password = prefs.getString("pass", "");
  g_netCfg.hostname = prefs.getString("host", "openhandy");
  g_netCfg.useDhcp  = prefs.getBool("dhcp", true);
  g_netCfg.ip       = prefs.getString("ip", "");
  g_netCfg.gateway  = prefs.getString("gw", "");
  g_netCfg.netmask  = prefs.getString("mask", "");
  g_netCfg.dns      = prefs.getString("dns", "");
  uint16_t udpPort  = prefs.getUShort("udpPort", CFG_DEFAULT_UDP_PORT);
  if (udpPort == 0) {
    udpPort = CFG_DEFAULT_UDP_PORT;
  }
  g_netCfg.udpPort  = udpPort;
  g_netCfg.bootSound = prefs.getUChar("bootSound", CFG_DEFAULT_BOOT_SOUND);
}

void netcfg_save() {
  prefs.begin(NVS_NAMESPACE_CONFIG, false);
  prefs.putString("ssid", g_netCfg.ssid);
  prefs.putString("pass", g_netCfg.password);
  prefs.putString("host", g_netCfg.hostname);
  prefs.putUShort("udpPort", g_netCfg.udpPort);
  prefs.putBool("dhcp",  g_netCfg.useDhcp);
  prefs.putString("ip",   g_netCfg.ip);
  prefs.putString("gw",   g_netCfg.gateway);
  prefs.putString("mask", g_netCfg.netmask);
  prefs.putString("dns",  g_netCfg.dns);
  prefs.putUChar("bootSound", g_netCfg.bootSound);
}

// motor helpers

void setPwm(int duty) {
  if (duty < 0) duty = 0;
  if (duty > PWM_MAX_DUTY) duty = PWM_MAX_DUTY;

  // hardware is active-low on ENABLE
  int hwDuty = PWM_MAX_DUTY - duty;
  ledcWrite(PWM_CHANNEL, hwDuty);
}

void setBrake(bool on) {
  digitalWrite(PIN_BRAKEZ, on ? LOW : HIGH);
}

void setDirectionUp() {
  digitalWrite(PIN_DIR, HIGH);
}

void setDirectionDown() {
  digitalWrite(PIN_DIR, LOW);
}
