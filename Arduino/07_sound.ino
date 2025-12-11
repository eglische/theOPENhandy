// 07_sound.ino
#include "openhandy.h"
#include <Arduino.h>

extern HandyConfig   g_cfg;
extern NetworkConfig g_netCfg;

// ---------------------------------------------------------------------------
// Melodies
// ---------------------------------------------------------------------------

static const BootNote BOOT_MELODY_IMPERIAL[] = {
  { 440, 500 },  // A
  { 440, 500 },  // A
  { 440, 500 },  // A
  { 349, 350 },  // F
  { 523, 150 },  // C5
  { 440, 500 },  // A
  { 349, 350 },  // F
  { 523, 150 },  // C5
  { 440, 650 }   // A
};

static const BootNote BOOT_MELODY_MARIO[] = {
    { 330, 100 }, // E4 (short, staccato)
    {   0,  50 }, // Brief gap
    { 330, 100 }, // E4 (short, staccato)
    {   0, 150 }, // Longer pause
    { 330, 100 }, // E4 (short, staccato)
    {   0, 150 }, // Pause
    { 262, 100 }, // C4 (quick lead-in)
    { 330, 200 }, // E4 (held longer)
    {   0,  50 }, // Brief gap
    { 392, 250 }, // G4 (triumphant, held)
    {   0, 300 }, // Long pause before it would drop
    { 196, 250 }  // G3 (the low note that follows)
};

static const BootNote BOOT_MELODY_STREETFIGHTER[] = {
    { 440, 150 }, // A4
    { 330, 120 }, // E4
    { 494, 150 }, // B4
    {   0, 60  }, // break
    { 660, 240 }  // E5 accent
};

static const BootNote BOOT_MELODY_ZARATHUSTRA[] = {
  // --- First feierlich call: C → G → big C + three thumps ---

  {  65, 1200 },  // C2  - long "taaaaaa"
  {   0,  400 },
  {  98, 1200 },  // G2  - long "taaaaaa"
  {   0,  400 },
  { 131,  400 },  // C3  - big "TA TAAAAAAA" hit
  {   0,  200 },
  { 131,  800 }, 
  {   0,  400 },
  // three "BUMB BUMB BUMB" on the root
  {  65,  220 },  // C2
  {   0,  200 },
  {  65,  220 },  // C2
  {   0,  200 },
  {  65,  400 },  // C2 (slightly longer last hit)

  /*/ --- Second call, up a semitone: C# → G# → big C# + three thumps ---

  {  69, 1000 },  // C#2 - long
  {   0,  120 },
  { 104, 1000 },  // G#2 - long
  {   0,  120 },
  { 139,  800 },  // C#3 - big hit

  {  69,  220 },  // C#2
  {   0,   80 },
  {  69,  220 },  // C#2
  {   0,   80 },
  {  69,  400 },  // C#2 - last, held a bit longer */
};
// ---------------------------------------------------------------------------
// Core playback with IR1/IR3 ping-pong
// ---------------------------------------------------------------------------

static void playBootMelodyPingPong(const BootNote *melody, size_t count) {
  if (!melody || count == 0) return;

  // Base duty from config
  int runDuty = g_cfg.homingPwm;
  if (runDuty <= 0) runDuty = g_cfg.minPwmRun;
  if (runDuty > g_cfg.maxPwm) runDuty = g_cfg.maxPwm;

  setBrake(false);
  bool dirUp = true;
  setDirectionUp();
  setPwm(0);

  bool lastIr1 = false;
  bool lastIr3 = false;

  for (size_t i = 0; i < count; ++i) {
    uint16_t freq = melody[i].freq;
    uint16_t dur  = melody[i].durationMs;

    if (freq > 0 && dur > 0) {
      // Simple visual feedback; you can theme this per melody if you like
      led_setRGB(255, 0, 0);
      led_loop();

      // Temporarily retune PWM to audio frequency
      ledcSetup(PWM_CHANNEL, freq, PWM_RESOLUTION);
      setPwm(runDuty);

      unsigned long start = millis();
      while (millis() - start < dur) {
        int v1 = digitalRead(PIN_IR1);
        int v3 = digitalRead(PIN_IR3);
        bool ir1 = g_cfg.ir1ActiveLow ? (v1 == LOW) : (v1 == HIGH);
        bool ir3 = g_cfg.ir3ActiveLow ? (v3 == LOW) : (v3 == HIGH);

        bool edgeIr1 = ir1 && !lastIr1;
        bool edgeIr3 = ir3 && !lastIr3;

        // Ping-pong between IR1 and IR3 on rising edges
        if (edgeIr1 || edgeIr3) {
          dirUp = !dirUp;
          if (dirUp) setDirectionUp();
          else       setDirectionDown();
        }

        lastIr1 = ir1;
        lastIr3 = ir3;

        delay(5);
      }
    }

    // Brief gap between notes
    setPwm(0);
    led_off();
    delay(80);
  }

  // Restore normal PWM configuration and motor state
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION);
  setPwm(0);
  setBrake(true);
  led_setMode(LEDMODE_BOOTING);
}

// ---------------------------------------------------------------------------
// Public entry point (called from 00_openhandy.ino)
// ---------------------------------------------------------------------------

void sound_playBootOnStartup() {
  uint8_t idx = g_netCfg.bootSound;

  if (idx == 0) {
    // 0 = disabled
    return;
  }

  switch (idx) {
    case 1: {
      const size_t N = sizeof(BOOT_MELODY_IMPERIAL) / sizeof(BOOT_MELODY_IMPERIAL[0]);
      playBootMelodyPingPong(BOOT_MELODY_IMPERIAL, N);
      break;
    }
    case 2: {
      const size_t N = sizeof(BOOT_MELODY_MARIO) / sizeof(BOOT_MELODY_MARIO[0]);
      playBootMelodyPingPong(BOOT_MELODY_MARIO, N);
      break;
    }
    case 3: {
      const size_t N = sizeof(BOOT_MELODY_STREETFIGHTER) / sizeof(BOOT_MELODY_STREETFIGHTER[0]);
      playBootMelodyPingPong(BOOT_MELODY_STREETFIGHTER, N);
      break;
    }
    case 4: {
      const size_t N = sizeof(BOOT_MELODY_ZARATHUSTRA) / sizeof(BOOT_MELODY_ZARATHUSTRA[0]);
      playBootMelodyPingPong(BOOT_MELODY_ZARATHUSTRA, N);
      break;
    }
    default:
      // Unknown index -> do nothing
      break;
  }
}
