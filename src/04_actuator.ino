// 04_actuator.ino
#include "openhandy.h"

// ---------- Encoder / motion state ----------
static RunState           s_state            = STATE_IDLE;
static unsigned long      s_stateEnterMs     = 0;
static volatile bool      s_dirUp            = true;
static volatile long      s_encoderCount     = 0;
static volatile long      s_encoderRaw       = 0;
static long               s_lastEncForVel    = 0;
static unsigned long      s_lastVelSampleMs  = 0;
static volatile uint8_t   s_lastHallState    = 0;
static int8_t             s_encDirUpSign     = 1;

// Motion control
static bool  s_motionOn            = false; // power button toggles this
static int   s_speedPercent        = 0;     // 0..100
static int   s_lastSpeedPercent    = 10;    // reference slowest
static bool  s_patternInitialised  = false;
static bool  s_patternDirUp        = true;  // used for simple full-stroke pattern
static float s_patternEncF         = 0.0f;  // target position in encoder counts (float)
static unsigned long s_lastPatternUpdateMs = 0;

static PatternMode s_patternMode   = PATTERN_SINE;
static uint8_t     s_patternPhase  = 0;     // internal phase within pattern
static uint8_t     s_manualPatternMode = 0;
static const float PATTERN_BOUNCE_DEPTH = 0.40f;  // 40% of stroke above bottom

// Limits & home
static bool  s_limitsValid      = false;
static long  s_posIr1           = 0;     // raw encoder at IR1
static long  s_posIr3           = 0;     // raw encoder at IR3
static long  s_posMin           = 0;     // effective min (after overshoot)
static long  s_posMax           = 0;     // effective max (after overshoot)
static long  s_posHome          = 0;

// Axis mapping (TCode 0..10000)
static const float TC_AXIS_MIN  = 0.0f;
static const float TC_AXIS_MAX  = 10000.0f;

static long  s_encMin       = 0;    // encoder at axisMin
static long  s_encMax       = 0;    // encoder at axisMax
static long  s_strokeCounts = 0;    // s_encMax - s_encMin (>0)
static bool  s_axisInverted = false;

// ---------- Dynamic stroke cropping (volatile) ----------
// Fractions of full stroke cut from each side, 0.0 .. 1.0
static float s_cutLowerFrac = 0.0f; // cut from s_posMin upward
static float s_cutUpperFrac = 0.0f; // cut from s_posMax downward

// Buttons (edge detection)
static bool          s_btnPwrPrev          = false;
static bool          s_btnUpPrev           = false;
static bool          s_btnDownPrev         = false;
static bool          s_btnLeftPrev         = false;
static bool          s_btnRightPrev        = false;
static unsigned long s_btnLeftPressMs      = 0;
static unsigned long s_btnRightPressMs     = 0;
static bool          s_btnLeftLongHandled  = false;
static bool          s_btnRightLongHandled = false;

// Simple stuck detection at IR during homing
static unsigned long s_homingLastMoveMs  = 0;
static long          s_homingLastMoveEnc = 0;

// Track which IR fired first/second (by ID: 1 or 3) and positions
static bool s_haveFirstEnd   = false;
static bool s_haveSecondEnd  = false;
static int  s_firstIrId      = 0;   // 1 or 3
static int  s_secondIrId     = 0;   // 1 or 3
static long s_encFirstEnd    = 0;
static long s_encSecondEnd   = 0;

// ---------- TCode / UDP Stuff ----------
static bool          s_udpActive        = false;
static bool          s_udpPaused        = false;
static long          s_udpTargetEnc     = 0;
static unsigned long s_udpLastCmdMs     = 0;
static const unsigned long UDP_TIMEOUT_MS = 1000; // ms without packets -> back to READY
static float         s_udpTargetAxis    = 5000.0f;

// PWM bounds for speed controller (max from g_cfg.maxPwm)
static const int PWM_MIN = 0;

// Velocity mapping
// Target speed in counts/sec = speedPercent * BASE_COUNTS_PER_SEC_PER_PERCENT
static const float BASE_COUNTS_PER_SEC_PER_PERCENT = 5.0f;

// ---------- Homing state machine ----------
enum HomingPhase {
  HOMING_IDLE = 0,
  HOMING_FIND_FIRST,   // move until first IR triggers (this becomes axis 10000)
  HOMING_GOTO_SECOND,  // reverse, move until the other IR triggers (axis 0)
  HOMING_GOTO_HOME,    // move to configured home (in axis-space)
  HOMING_DONE,
  HOMING_FAIL
};

static HomingPhase s_homingPhase = HOMING_IDLE;

// ---------- Forward declarations ----------
static void  actuator_setState(RunState st);
static void  actuator_updateLed();
static void  actuator_checkThermal();
static bool  actuator_readIr1Active();
static bool  actuator_readIr3Active();
static void  actuator_handleButtons();
static float actuator_measureSpeed(long encNow, unsigned long now);
static void  actuator_homingStep(unsigned long now);
static void  actuator_updatePattern(unsigned long now);
static void  actuator_updateRemote(unsigned long now);
static void  actuator_applyAxisServo(float targetAxis);
static void  actuator_handleButtons();

// ---------- Axis mapping helpers ----------
static float tcodeAxisFromEnc(long enc) {
  if (s_strokeCounts <= 0) return TC_AXIS_MIN;

  float t = (float)(enc - s_encMin) / (float)s_strokeCounts; // ideally 0..1

  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  float axis;
  if (!s_axisInverted) {
    // encMin -> 0, encMax -> 10000
    axis = TC_AXIS_MIN + t * (TC_AXIS_MAX - TC_AXIS_MIN);
  } else {
    // encMin -> 10000, encMax -> 0
    axis = TC_AXIS_MAX - t * (TC_AXIS_MAX - TC_AXIS_MIN);
  }

  if (axis < TC_AXIS_MIN) axis = TC_AXIS_MIN;
  if (axis > TC_AXIS_MAX) axis = TC_AXIS_MAX;
  return axis;
}

static long encFromTcodeAxis(float axis) {
  if (axis < TC_AXIS_MIN) axis = TC_AXIS_MIN;
  if (axis > TC_AXIS_MAX) axis = TC_AXIS_MAX;
  if (s_strokeCounts <= 0) return s_encMin;

  float span = (TC_AXIS_MAX - TC_AXIS_MIN);
  if (span <= 0.0f) span = 1.0f;

  float t;
  if (!s_axisInverted) {
    // 0..10000 maps encMin..encMax
    t = (axis - TC_AXIS_MIN) / span;    // 0..1
  } else {
    // 10000..0 maps encMin..encMax
    t = (TC_AXIS_MAX - axis) / span;    // 0..1
  }

  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  long delta = (long)(t * (float)s_strokeCounts);
  return s_encMin + delta;
}

// Crop fractions sanity
static void clampCutFracs() {
  if (s_cutLowerFrac < 0.0f) s_cutLowerFrac = 0.0f;
  if (s_cutUpperFrac < 0.0f) s_cutUpperFrac = 0.0f;

  if (s_cutLowerFrac > 1.0f) s_cutLowerFrac = 1.0f;
  if (s_cutUpperFrac > 1.0f) s_cutUpperFrac = 1.0f;

  float totalCut        = s_cutLowerFrac + s_cutUpperFrac;
  const float minRemain = 0.20f;

  if (totalCut >= 1.0f) {
    // insane cut, reset
    s_cutLowerFrac = 0.0f;
    s_cutUpperFrac = 0.0f;
    return;
  }

  float remaining = 1.0f - totalCut;
  if (remaining < minRemain && totalCut > 0.0f) {
    float targetTotalCut = 1.0f - minRemain;
    float scale = targetTotalCut / totalCut;
    s_cutLowerFrac *= scale;
    s_cutUpperFrac *= scale;
  }
}

static long getEffectiveMin() {
  long fullLen = s_posMax - s_posMin;
  if (fullLen <= 0) return s_posMin;
  long cut = (long)(s_cutLowerFrac * (float)fullLen);
  return s_posMin + cut;
}

static long getEffectiveMax() {
  long fullLen = s_posMax - s_posMin;
  if (fullLen <= 0) return s_posMax;
  long cut = (long)(s_cutUpperFrac * (float)fullLen);
  return s_posMax - cut;
}

// ---------- Encoder ISR (3-hall decode) ----------
// Hall pattern table (index = H3 H2 H1):
// 001,011,010,110,100,101 are valid (0..5), 000/111 invalid (-1)
static const int8_t s_hallLut[8] = {
  -1, // 000
   0, // 001
   2, // 010
   1, // 011
   4, // 100
   5, // 101
   3, // 110
  -1  // 111
};

static void IRAM_ATTR encoder_isr_ha() {
  uint8_t h1 = (uint8_t)digitalRead(PIN_HA); // IO23
  uint8_t h2 = (uint8_t)digitalRead(PIN_HB); // IO22
  uint8_t h3 = (uint8_t)digitalRead(PIN_HC); // IO4
  uint8_t state = (uint8_t)((h3 << 2) | (h2 << 1) | h1);

  int8_t idx = s_hallLut[state];
  if (idx < 0) {
    s_lastHallState = state;
    return;
  }

  uint8_t lastState = s_lastHallState;
  if (lastState == state) {
    return;
  }

  int8_t lastIdx = s_hallLut[lastState];
  s_lastHallState = state;

  if (lastIdx < 0) {
    // first valid sample
    return;
  }

  int8_t diff = idx - lastIdx;
  // wrap-around: -5 == +1, +5 == -1
  if (diff == 1 || diff == -5) {
    s_encoderCount++;
    s_encoderRaw++;
  } else if (diff == -1 || diff == 5) {
    s_encoderCount--;
    s_encoderRaw++;
  } else {
    // illegal jump -> ignore
  }
}

// ---------- Helpers ----------
static void actuator_setState(RunState st) {
  if (st == s_state) return;
  s_state        = st;
  s_stateEnterMs = millis();
}

static void actuator_updateLed() {
  unsigned long elapsed = millis() - s_stateEnterMs;

  // UDP overrides normal state -> blue or red
  if (s_udpActive) {
    if (s_udpPaused) {
      led_setMode(LEDMODE_STATE_ERROR);   // red
    } else {
      led_setMode(LEDMODE_STATE_UDP);     // blue
    }
    return;
  }

  switch (s_state) {
    case STATE_HOMING:
      led_setMode(LEDMODE_STATE_HOMING);
      break;

    case STATE_READY:
    case STATE_RUNNING:
      led_setMode(LEDMODE_READY);
      break;

    case STATE_ERROR_COLLISION:
      if (elapsed < 3000UL) {
        led_setMode(LEDMODE_STATE_COLLISION);
      } else {
        led_setMode(LEDMODE_STATE_ERROR);
      }
      break;

    case STATE_ERROR_THERMAL:
      led_setMode(LEDMODE_STATE_THERMAL);
      break;

    case STATE_ERROR:
    default:
      led_setMode(LEDMODE_STATE_ERROR);
      break;
  }
}

// Thermal guard: lower ADC = hotter
static void actuator_checkThermal() {
  int raw = analogRead(PIN_THERMAL);
  if (raw <= g_cfg.thermalThresholdAdc) {
    actuator_setState(STATE_ERROR_THERMAL);
  }
}

// IR with configurable polarity
static bool actuator_readIr1Active() {
  int v = digitalRead(PIN_IR1);
  return g_cfg.ir1ActiveLow ? (v == LOW) : (v == HIGH);
}

static bool actuator_readIr3Active() {
  int v = digitalRead(PIN_IR3);
  return g_cfg.ir3ActiveLow ? (v == LOW) : (v == HIGH);
}

// ---------- Buttons (incl. left/right stroke control) ----------
static void actuator_handleButtons() {
  unsigned long now = millis();

  bool pwrNow   = (digitalRead(PIN_BTN_PWR)   == LOW);
  bool upNow    = (digitalRead(PIN_BTN_UP)    == LOW);
  bool downNow  = (digitalRead(PIN_BTN_DOWN)  == LOW);
  bool leftNow  = (digitalRead(PIN_BTN_LEFT)  == LOW);
  bool rightNow = (digitalRead(PIN_BTN_RIGHT) == LOW);

  const unsigned long MEDIUM_PRESS_MS = 1000UL;
  const unsigned long LONG_PRESS_MS   = 3000UL;
  const float STEP = 0.05f; // 5% per step

  // UDP mode: only power = pause/resume
  if (s_udpActive) {
    if (pwrNow && !s_btnPwrPrev) {
      s_udpPaused = !s_udpPaused;
      if (s_udpPaused) {
        setPwm(0);
        setBrake(true);
      }
    }

    s_btnPwrPrev    = pwrNow;
    s_btnUpPrev     = upNow;
    s_btnDownPrev   = downNow;
    s_btnLeftPrev   = leftNow;
    s_btnRightPrev  = rightNow;
    return;
  }

  // Normal/manual mode

  // Power: toggle motion (only when homing done)
  if (pwrNow && !s_btnPwrPrev) {
    if (s_state == STATE_READY || s_state == STATE_RUNNING) {
      s_motionOn = !s_motionOn;
      if (s_motionOn) {
        if (s_speedPercent <= 0) {
          if (s_lastSpeedPercent <= 0) {
            s_lastSpeedPercent = 10;
          }
          s_speedPercent = s_lastSpeedPercent;
        }
        s_patternInitialised = false;
        actuator_setState(STATE_RUNNING);
      } else {
        s_motionOn           = false;
        s_patternInitialised = false;
        actuator_setState(STATE_READY);
        setPwm(0);
        setBrake(true);
      }
    }
  }

  // Speed up
  if (upNow && !s_btnUpPrev) {
    s_speedPercent += 10;
    if (s_speedPercent > 100) s_speedPercent = 100;
    if (s_speedPercent > 0) s_lastSpeedPercent = s_speedPercent;
  }

  // Speed down
  if (downNow && !s_btnDownPrev) {
    s_speedPercent -= 10;
    if (s_speedPercent < 0) s_speedPercent = 0;
    if (s_speedPercent > 0) s_lastSpeedPercent = s_speedPercent;
  }

  // LEFT button -> crop upper end
  if (leftNow && !s_btnLeftPrev) {
    s_btnLeftPressMs     = now;
    s_btnLeftLongHandled = false;
  }

  if (leftNow && !s_btnLeftLongHandled) {
    unsigned long held = now - s_btnLeftPressMs;
    if (held >= LONG_PRESS_MS) {
      s_cutUpperFrac = 0.0f;
      clampCutFracs();
      s_btnLeftLongHandled = true;
    }
  }

  if (!leftNow && s_btnLeftPrev) {
    unsigned long held = now - s_btnLeftPressMs;
    if (!s_btnLeftLongHandled) {
      if (held >= MEDIUM_PRESS_MS) {
        s_cutUpperFrac -= STEP;   // extend back (less crop)
      } else {
        s_cutUpperFrac += STEP;   // crop more
      }
      clampCutFracs();
    }
  }

  // RIGHT button -> crop lower end
  if (rightNow && !s_btnRightPrev) {
    s_btnRightPressMs     = now;
    s_btnRightLongHandled = false;
  }

  if (rightNow && !s_btnRightLongHandled) {
    unsigned long held = now - s_btnRightPressMs;
    if (held >= LONG_PRESS_MS) {
      s_cutLowerFrac = 0.0f;
      clampCutFracs();
      s_btnRightLongHandled = true;
    }
  }

  if (!rightNow && s_btnRightPrev) {
    unsigned long held = now - s_btnRightPressMs;
    if (!s_btnRightLongHandled) {
      if (held >= MEDIUM_PRESS_MS) {
        s_cutLowerFrac -= STEP;   // extend back (less crop)
      } else {
        s_cutLowerFrac += STEP;   // crop more
      }
      clampCutFracs();
    }
  }

  s_btnPwrPrev    = pwrNow;
  s_btnUpPrev     = upNow;
  s_btnDownPrev   = downNow;
  s_btnLeftPrev   = leftNow;
  s_btnRightPrev  = rightNow;
}

// Simple speed measurement in counts/sec
static float actuator_measureSpeed(long encNow, unsigned long now) {
  if (s_lastVelSampleMs == 0) {
    s_lastVelSampleMs = now;
    s_lastEncForVel   = encNow;
    return 0.0f;
  }

  unsigned long dtMs = now - s_lastVelSampleMs;
  if (dtMs == 0) return 0.0f;

  long  dEnc = encNow - s_lastEncForVel;
  float cps  = (float)dEnc * 1000.0f / (float)dtMs;

  s_lastVelSampleMs = now;
  s_lastEncForVel   = encNow;

  return cps;
}

// ---------- Homing: "first IR = 10000, second IR = 0" ----------
static void actuator_homingStep(unsigned long now) {
  bool ir1 = actuator_readIr1Active();
  bool ir3 = actuator_readIr3Active();
  long enc = s_encoderCount;

  bool checkStuck = (s_homingPhase == HOMING_FIND_FIRST ||
                     s_homingPhase == HOMING_GOTO_SECOND);

  if (checkStuck) {
    if (s_homingLastMoveMs == 0) {
      s_homingLastMoveMs  = now;
      s_homingLastMoveEnc = enc;
    } else if (now - s_homingLastMoveMs >= 300UL) {
      if (labs(enc - s_homingLastMoveEnc) < 2 && (ir1 || ir3)) {
        actuator_setState(STATE_ERROR);
        s_homingPhase = HOMING_FAIL;
        setPwm(0);
        setBrake(true);
        return;
      }
      s_homingLastMoveMs  = now;
      s_homingLastMoveEnc = enc;
    }
  }

  switch (s_homingPhase) {

    // FIRST STEP:
    // Move in current direction until first IR becomes active.
    case HOMING_FIND_FIRST: {
      setBrake(false);
      if (s_dirUp) setDirectionUp(); else setDirectionDown();

      int hp = g_cfg.homingPwm;
      if (hp < 0) hp = 0;
      if (hp > g_cfg.maxPwm) hp = g_cfg.maxPwm;
      setPwm(hp);

      if (!s_haveFirstEnd && (ir1 || ir3)) {
        s_haveFirstEnd = true;
        s_encFirstEnd  = enc;

        if (ir1) {
          s_firstIrId = 1;
          s_posIr1    = enc;
        }
        if (ir3) {
          s_firstIrId = 3;
          s_posIr3    = enc;
        }

        s_dirUp = !s_dirUp;
        if (s_dirUp) setDirectionUp(); else setDirectionDown();

        s_homingLastMoveMs  = now;
        s_homingLastMoveEnc = enc;

        s_homingPhase = HOMING_GOTO_SECOND;
      }
      break;
    }

    // SECOND STEP:
    // Move in opposite direction, ignore the first IR and look only for the other one.
    case HOMING_GOTO_SECOND: {
      setBrake(false);
      if (s_dirUp) setDirectionUp(); else setDirectionDown();

      int hp = g_cfg.homingPwm;
      if (hp < 0) hp = 0;
      if (hp > g_cfg.maxPwm) hp = g_cfg.maxPwm;
      setPwm(hp);

      if (!s_haveSecondEnd) {
        bool gotSecond = false;

        if (s_firstIrId != 1 && ir1) {
          s_secondIrId = 1;
          s_posIr1     = enc;
          gotSecond    = true;
        } else if (s_firstIrId != 3 && ir3) {
          s_secondIrId = 3;
          s_posIr3     = enc;
          gotSecond    = true;
        }

        if (gotSecond) {
          s_haveSecondEnd = true;
          s_encSecondEnd  = enc;

          setPwm(0);
          setBrake(true);

          long e1 = s_encFirstEnd;
          long e2 = s_encSecondEnd;

          if (e1 == e2) {
            actuator_setState(STATE_ERROR);
            s_homingPhase = HOMING_FAIL;
            return;
          }

          int8_t signDelta = (e2 > e1) ? +1 : -1;
          if (s_dirUp) {
            s_encDirUpSign = signDelta;
          } else {
            s_encDirUpSign = -signDelta;
          }

          if (e1 < e2) {
            s_encMin = e1;
            s_encMax = e2;
          } else {
            s_encMin = e2;
            s_encMax = e1;
          }

          s_strokeCounts = s_encMax - s_encMin;
          if (s_strokeCounts < 10) {
            actuator_setState(STATE_ERROR);
            s_homingPhase = HOMING_FAIL;
            return;
          }

          // FIRST end == axis 10000?
          s_axisInverted = (s_encFirstEnd == s_encMin);

          // -----------------------------
          // Apply IR overshoot (TCode units from config)
          // overshootIr3Counts: IR3 / DOWN side
          // overshootIr1Counts: IR1 / UP side
          // Positive = crop; negative = extend.
          // -----------------------------
          float fullAxisSpan = (TC_AXIS_MAX - TC_AXIS_MIN);
          if (fullAxisSpan <= 0.0f) {
            fullAxisSpan = 1.0f;
          }

          float fracLower = (float)g_cfg.overshootIr3Counts / fullAxisSpan;
          float fracUpper = (float)g_cfg.overshootIr1Counts / fullAxisSpan;

          long deltaLower = (long)((float)s_strokeCounts * fracLower);
          long deltaUpper = (long)((float)s_strokeCounts * fracUpper);

          long posMin = s_encMin + deltaLower;
          long posMax = s_encMax - deltaUpper;

          if (posMax <= posMin) {
            posMin = s_encMin;
            posMax = s_encMax;
          }

          s_posMin      = posMin;
          s_posMax      = posMax;
          s_limitsValid = true;

          // Make axis mapping use overshoot-adjusted limits
          s_encMin       = s_posMin;
          s_encMax       = s_posMax;
          s_strokeCounts = s_encMax - s_encMin;
          if (s_strokeCounts < 10) {
            actuator_setState(STATE_ERROR);
            s_homingPhase = HOMING_FAIL;
            return;
          }

          // ---- Home position in axis space ----
          float homeAxis;
          switch (g_cfg.homeMode) {
            case 0: // home at axis 0 side
              homeAxis = TC_AXIS_MIN;
              break;
            case 1: // home at axis 10000 side
              homeAxis = TC_AXIS_MAX;
              break;
            case 2: // middle of usable range
            default:
              homeAxis = (TC_AXIS_MIN + TC_AXIS_MAX) * 0.5f;
              break;
          }

          s_posHome = encFromTcodeAxis(homeAxis);

          s_cutLowerFrac = 0.0f;
          s_cutUpperFrac = 0.0f;

          s_homingLastMoveMs  = now;
          s_homingLastMoveEnc = enc;

          s_homingPhase = HOMING_GOTO_HOME;
        }
      }
      break;
    }

    // THIRD STEP:
    // move to home position using encoder only.
    case HOMING_GOTO_HOME: {
      if (!s_limitsValid) {
        actuator_setState(STATE_ERROR);
        s_homingPhase = HOMING_FAIL;
        setPwm(0);
        setBrake(true);
        return;
      }

      long diff = s_posHome - enc;

      if (labs(diff) <= 5) {
        setPwm(0);
        setBrake(true);
        actuator_setState(STATE_READY);
        s_homingPhase  = HOMING_DONE;
        s_motionOn     = false;
        s_speedPercent = 0;
        return;
      }

      int8_t desiredSign = (diff > 0) ? +1 : -1;
      if (desiredSign * s_encDirUpSign > 0) {
        s_dirUp = true;
        setDirectionUp();
      } else {
        s_dirUp = false;
        setDirectionDown();
      }

      setBrake(false);
      int hp = g_cfg.homingPwm;
      if (hp < 0) hp = 0;
      if (hp > g_cfg.maxPwm) hp = g_cfg.maxPwm;
      setPwm(hp);
      break;
    }

    case HOMING_DONE:
    case HOMING_FAIL:
    default:
      break;
  }
}

// ---------- Pattern: full stroke between (cropped) posMin / posMax ----------
static void actuator_updatePattern(unsigned long now) {
  if (!s_limitsValid) {
    setPwm(0);
    setBrake(true);
    actuator_setState(STATE_ERROR);
    return;
  }

  long effMin = getEffectiveMin();
  long effMax = getEffectiveMax();

  if (effMax - effMin < 5) {
    setPwm(0);
    setBrake(true);
    actuator_setState(STATE_ERROR);
    return;
  }

  if (!s_patternInitialised) {
    long encNow = s_encoderCount;

    if (encNow < effMin) encNow = effMin;
    if (encNow > effMax) encNow = effMax;

    s_patternEncF          = (float)encNow;
    s_patternDirUp         = true;
    s_lastPatternUpdateMs  = now;
    s_patternInitialised   = true;
    s_patternPhase         = 0;
  }

  unsigned long dtMs = now - s_lastPatternUpdateMs;
  if (dtMs == 0) dtMs = 1;
  s_lastPatternUpdateMs = now;

  float cps = (float)s_speedPercent * g_cfg.patternBaseCps;
  if (cps < 0.0f) cps = 0.0f;

  float step = cps * ((float)dtMs / 1000.0f);

  if (s_patternMode == PATTERN_SINE) {
    if (s_patternDirUp) {
      s_patternEncF += step;
      if (s_patternEncF >= (float)effMax) {
        s_patternEncF = (float)effMax;
        s_patternDirUp = false;
      }
    } else {
      s_patternEncF -= step;
      if (s_patternEncF <= (float)effMin) {
        s_patternEncF = (float)effMin;
        s_patternDirUp = true;
      }
    }
  } else {
    float stroke = (float)(effMax - effMin);
    if (stroke < 1.0f) stroke = 1.0f;

    float bounceBottom = (float)effMax - PATTERN_BOUNCE_DEPTH * stroke;
    if (bounceBottom < (float)effMin) {
      bounceBottom = (float)effMin;
    }

    bool  goingUp = true;
    float target  = (float)effMax;

    if (s_patternMode == PATTERN_BOUNCE) {
      switch (s_patternPhase) {
        case 0: goingUp = true;  target = (float)effMax;      break;
        case 1: goingUp = false; target = bounceBottom;       break;
        case 2: goingUp = true;  target = (float)effMax;      break;
        case 3:
        default: goingUp = false; target = (float)effMin;     break;
      }
    } else {
      // PATTERN_DOUBLE_BOUNCE
      switch (s_patternPhase) {
        case 0: goingUp = true;  target = (float)effMax;      break;
        case 1: goingUp = false; target = bounceBottom;       break;
        case 2: goingUp = true;  target = (float)effMax;      break;
        case 3: goingUp = false; target = bounceBottom;       break;
        case 4: goingUp = true;  target = (float)effMax;      break;
        case 5:
        default: goingUp = false; target = (float)effMin;     break;
      }
    }

    float signedStep = goingUp ? step : -step;
    s_patternEncF += signedStep;

    if (goingUp) {
      if (s_patternEncF >= target) {
        s_patternEncF = target;
        s_patternPhase++;
      }
    } else {
      if (s_patternEncF <= target) {
        s_patternEncF = target;
        s_patternPhase++;
      }
    }

    if (s_patternMode == PATTERN_BOUNCE) {
      if (s_patternPhase > 3) s_patternPhase = 0;
    } else {
      if (s_patternPhase > 5) s_patternPhase = 0;
    }
  }

  long encTarget = (long)(s_patternEncF + 0.5f);
  if (encTarget < effMin) encTarget = effMin;
  if (encTarget > effMax) encTarget = effMax;

  float axisTarget = tcodeAxisFromEnc(encTarget);

  if (s_state != STATE_RUNNING) {
    actuator_setState(STATE_RUNNING);
  }

  actuator_applyAxisServo(axisTarget);
}

static void actuator_applyAxisServo(float targetAxis) {
  if (!s_limitsValid) {
    setPwm(0);
    setBrake(true);
    return;
  }

  float sp    = (float)s_speedPercent / 100.0f;
  if (sp < 0.0f) sp = 0.0f;
  if (sp > 1.0f) sp = 1.0f;

  float scale = g_cfg.kpSpeedScaleMin + (1.0f - g_cfg.kpSpeedScaleMin) * sp;
  float kpAxis = g_cfg.kp * scale;

  long  encNow  = s_encoderCount;
  float axisNow = tcodeAxisFromEnc(encNow);

  float errAxis = targetAxis - axisNow;

  if (fabs(errAxis) <= g_cfg.axisDeadband) {
    setPwm(0);
    setBrake(true);
    return;
  }

  int8_t axisSign   = (errAxis > 0.0f) ? +1 : -1;
  int8_t monoSign   = s_axisInverted ? -1 : +1;
  int8_t desiredEnc = axisSign * monoSign;

  if (desiredEnc * s_encDirUpSign > 0) {
    s_dirUp = true;
    setDirectionUp();
  } else {
    s_dirUp = false;
    setDirectionDown();
  }

  setBrake(false);

  float maxP = (float)g_cfg.maxPwm;
  if (maxP < (float)PWM_MIN) maxP = (float)PWM_MIN;

  if (kpAxis < 0.0f) kpAxis = 0.0f;

  float pwmFloat = fabs(errAxis) * kpAxis;

  if (pwmFloat > maxP)                   pwmFloat = maxP;
  if (pwmFloat < (float)g_cfg.minPwmRun) pwmFloat = (float)g_cfg.minPwmRun;

  int pwmCmd = (int)pwmFloat;
  setPwm(pwmCmd);
}

static void actuator_updateRemote(unsigned long now) {
  (void)now;

  if (!s_limitsValid) {
    setPwm(0);
    setBrake(true);
    s_udpActive = false;
    return;
  }

  actuator_applyAxisServo(s_udpTargetAxis);
}

// ---------- Public API ----------
void actuator_init() {
  setPwm(0);
  setBrake(true);

  s_dirUp        = true;
  s_encoderCount = 0;
  s_encoderRaw   = 0;

  int h1 = digitalRead(PIN_HA);
  int h2 = digitalRead(PIN_HB);
  int h3 = digitalRead(PIN_HC);
  s_lastHallState = (uint8_t)((h3 << 2) | (h2 << 1) | h1);

  attachInterrupt(digitalPinToInterrupt(PIN_HA), encoder_isr_ha, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_HB), encoder_isr_ha, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_HC), encoder_isr_ha, CHANGE);

  s_homingPhase       = HOMING_FIND_FIRST;
  s_homingLastMoveMs  = millis();
  s_homingLastMoveEnc = s_encoderCount;

  s_limitsValid       = false;
  s_haveFirstEnd      = false;
  s_haveSecondEnd     = false;
  s_firstIrId         = 0;
  s_secondIrId        = 0;
  s_encFirstEnd       = 0;
  s_encSecondEnd      = 0;
  s_axisInverted      = false;

  s_motionOn         = false;
  s_speedPercent     = 0;
  s_lastSpeedPercent = 10;
  s_cutLowerFrac     = 0.0f;
  s_cutUpperFrac     = 0.0f;

  s_btnPwrPrev       = false;
  s_btnUpPrev        = false;
  s_btnDownPrev      = false;
  s_btnLeftPrev      = false;
  s_btnRightPrev     = false;
  s_btnLeftPressMs   = 0;
  s_btnRightPressMs  = 0;
  s_btnLeftLongHandled  = false;
  s_btnRightLongHandled = false;

  s_patternInitialised   = false;
  s_patternDirUp         = true;
  s_patternEncF          = 0.0f;
  s_lastPatternUpdateMs  = 0;
  s_manualPatternMode    = 0;

  s_encMin           = 0;
  s_encMax           = 0;
  s_strokeCounts     = 0;
  s_udpActive        = false;
  s_udpPaused        = false;
  s_udpTargetEnc     = 0;
  s_udpLastCmdMs     = 0;

  actuator_setState(STATE_HOMING);
}

void actuator_loop() {
  unsigned long now = millis();

  actuator_checkThermal();
  actuator_updateLed();

  if (s_state == STATE_ERROR_THERMAL ||
      s_state == STATE_ERROR        ||
      s_state == STATE_ERROR_COLLISION) {
    setPwm(0);
    setBrake(true);
    delay(2);
    return;
  }

  if (s_state == STATE_HOMING || s_homingPhase != HOMING_DONE) {
    actuator_homingStep(now);
    actuator_updateLed();
    delay(2);
    return;
  }

  actuator_handleButtons();

  if (s_udpActive) {
    if (s_udpLastCmdMs != 0 && (now - s_udpLastCmdMs) > UDP_TIMEOUT_MS) {
      setPwm(0);
      setBrake(true);
      s_udpActive = false;
      s_udpPaused = false;
      actuator_setState(STATE_READY);
      actuator_updateLed();
      delay(2);
      return;
    }

    if (!s_udpPaused) {
      actuator_updateRemote(now);
    } else {
      setPwm(0);
      setBrake(true);
    }

    delay(2);
    return;
  }

  if (!s_motionOn || s_speedPercent <= 0) {
    setPwm(0);
    setBrake(true);
    s_patternInitialised = false;
    if (s_state != STATE_READY) {
      actuator_setState(STATE_READY);
      actuator_updateLed();
    }
    delay(2);
    return;
  }

  actuator_updatePattern(now);

  delay(2);
}

// ---- Debug status helper ----
void debug_getStatus(bool &ir1Active,
                     bool &ir3Active,
                     long &encoderAbs,
                     long &encoderAxis,
                     int &thermalAdc,
                     RunState &state,
                     bool &limitsValid,
                     long &posMin,
                     long &posMax,
                     bool &collisionError,
                     bool &thermalError)
{
  ir1Active = actuator_readIr1Active();
  ir3Active = actuator_readIr3Active();

  noInterrupts();
  long raw = s_encoderRaw;
  long enc = s_encoderCount;
  interrupts();

  encoderAbs = raw;

  if (s_limitsValid && s_strokeCounts > 0) {
    float axis = tcodeAxisFromEnc(enc);
    encoderAxis = (long)axis;
  } else {
    encoderAxis = 0;
  }

  thermalAdc   = analogRead(PIN_THERMAL);
  state        = s_state;
  limitsValid  = s_limitsValid;
  posMin       = s_posMin;
  posMax       = s_posMax;

  collisionError = (s_state == STATE_ERROR_COLLISION);
  thermalError   = (s_state == STATE_ERROR_THERMAL);
}

// ---- Manual UI helpers for web / API ----
void actuator_getManualUiState(int &speedPercent,
                               float &cutLowerFrac,
                               float &cutUpperFrac,
                               uint8_t &patternMode)
{
  speedPercent = s_speedPercent;
  cutLowerFrac = s_cutLowerFrac;
  cutUpperFrac = s_cutUpperFrac;
  patternMode  = s_manualPatternMode;
}

// step speed by +/-delta (percent)
void actuator_manualSpeedStep(int delta)
{
  s_speedPercent += delta;
  if (s_speedPercent > 100) s_speedPercent = 100;
  if (s_speedPercent < 0)   s_speedPercent = 0;
  if (s_speedPercent > 0)   s_lastSpeedPercent = s_speedPercent;
}

// set absolute speed 0..100%
void actuator_setManualSpeedPercent(int sp)
{
  if (sp < 0)   sp = 0;
  if (sp > 100) sp = 100;
  s_speedPercent = sp;
  if (s_speedPercent > 0) s_lastSpeedPercent = s_speedPercent;
}

// set cropping fractions 0..1 from each end
void actuator_setManualCropping(float lowerFrac, float upperFrac)
{
  s_cutLowerFrac = lowerFrac;
  s_cutUpperFrac = upperFrac;
  clampCutFracs();
}

void actuator_setManualPatternMode(uint8_t mode)
{
  if (mode > PATTERN_DOUBLE_BOUNCE) {
    mode = PATTERN_SINE;
  }
  s_manualPatternMode = mode;
  s_patternMode = (PatternMode)mode;
  s_patternInitialised = false;
  s_patternPhase       = 0;
}

// ---- NEW: start/stop from web/API ----
bool actuator_getMotionOn()
{
  return s_motionOn;
}

void actuator_setMotionOn(bool on)
{
  if (!s_limitsValid) {
    s_motionOn = false;
    setPwm(0);
    setBrake(true);
    actuator_setState(STATE_READY);
    return;
  }

  s_motionOn = on;

  if (!s_motionOn) {
    setPwm(0);
    setBrake(true);
    if (s_state != STATE_ERROR &&
        s_state != STATE_ERROR_COLLISION &&
        s_state != STATE_ERROR_THERMAL) {
      actuator_setState(STATE_READY);
    }
  } else {
    if (s_speedPercent <= 0) {
      if (s_lastSpeedPercent <= 0) s_lastSpeedPercent = 10;
      s_speedPercent = s_lastSpeedPercent;
    }
    s_patternInitialised = false;
    actuator_setState(STATE_RUNNING);
  }
}

// ---- UDP / TCode entrypoint ----
void actuator_udpCommand(float axis)
{
  if (!s_limitsValid) return;

  if (s_motionOn) return;
  if (!s_udpActive && s_state != STATE_READY) return;

  if (axis < TC_AXIS_MIN) axis = TC_AXIS_MIN;
  if (axis > TC_AXIS_MAX) axis = TC_AXIS_MAX;

  // mapping already includes overshoot via s_encMin/s_encMax
  s_udpTargetAxis = axis;
  s_udpTargetEnc  = encFromTcodeAxis(axis);

  s_udpLastCmdMs = millis();

  if (!s_udpActive) {
    s_udpActive = true;
    s_udpPaused = false;
    actuator_setState(STATE_PATTERN);
  }
}
