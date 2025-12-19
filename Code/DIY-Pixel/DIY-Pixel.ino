#include <Arduino.h>
extern "C" {
  #include "driver/ledc.h"
}

// --------- PIN MAP (QT Py ESP32 Pico) ----------
#define FAN_GATE_PIN  5      // A3 Fan MOSFET gate
#define LED_GATE_PIN  4      // A2 LED MOSFET gate

// --------- FAN PWM (25 kHz) ----------
#define FAN_LEDC_CH   LEDC_CHANNEL_0
#define FAN_LEDC_TMR  LEDC_TIMER_0
#define FAN_LEDC_HZ   25000
#define FAN_LEDC_RES  LEDC_TIMER_8_BIT  // 0..255

// --------- LED PWM (slow ramp, 2 kHz) ----------
#define LED_LEDC_CH   LEDC_CHANNEL_1
#define LED_LEDC_TMR  LEDC_TIMER_1
#define LED_LEDC_HZ   2000
#define LED_LEDC_RES  LEDC_TIMER_8_BIT

// --------- Fan control params ---------
static uint8_t  filtDuty = 0;
const float     alpha          = 0.2f;   // EMA smoothing
const uint8_t   deadband       = 10;     // Below this => OFF
const uint8_t   kickThreshold  = 40;     // Start-kick threshold
const uint16_t  kickTimeMs     = 200;
const uint32_t  kickCooldownMs = 800;

// Set to 1 if your fan interprets PWM inverted (rare with Noctua, but common elsewhere)
#define FAN_PWM_INVERT 0

// --------- LED ramp params ---------
static uint8_t  ledDuty = 0;
static int8_t   ledDir  = +1;
static uint32_t tLast   = 0;
const uint16_t  stepMs  = 20;          // 20 ms per step -> full cycle ≈ 10 s
const uint8_t   ledMin  = 200;
const uint8_t   ledMax  = 200;

// ---------- FAN PWM SETUP ----------
static void fanPwmInit() {
  ledc_timer_config_t t = { LEDC_LOW_SPEED_MODE, FAN_LEDC_RES, FAN_LEDC_TMR, FAN_LEDC_HZ, LEDC_AUTO_CLK };
  ledc_timer_config(&t);
  ledc_channel_config_t c = { FAN_GATE_PIN, LEDC_LOW_SPEED_MODE, FAN_LEDC_CH, LEDC_INTR_DISABLE, FAN_LEDC_TMR, 0, 0 };
  ledc_channel_config(&c);
}
static inline void fanSetDutyRaw(uint8_t d) {
  #if FAN_PWM_INVERT
    d = 255 - d;
  #endif
  ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CH, d);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CH);
}
static void fanSetDutySmoothed(uint8_t target) {
  // Deadband for true OFF
  if (target < deadband) target = 0;

  // Exponential smoothing
  filtDuty = (uint8_t)(alpha * target + (1.0f - alpha) * filtDuty);

  // Non-blocking kick at low duty
  static bool     kickActive   = false;
  static uint32_t kickEndMs    = 0;
  static uint32_t lastKickMs   = 0;
  uint32_t now = millis();

  // Start kick if needed
  if (!kickActive && filtDuty > 0 && filtDuty < kickThreshold && (now - lastKickMs) > kickCooldownMs) {
    kickActive = true;
    kickEndMs  = now + kickTimeMs;
    lastKickMs = now;
  }

  if (kickActive) {
    if (now < kickEndMs) {
      fanSetDutyRaw(255);
      return;
    } else {
      kickActive = false;
    }
  }

  fanSetDutyRaw(filtDuty);
}

// ---------- LED PWM SETUP ----------
static void ledPwmInit() {
  ledc_timer_config_t t = { LEDC_LOW_SPEED_MODE, LED_LEDC_RES, LED_LEDC_TMR, LED_LEDC_HZ, LEDC_AUTO_CLK };
  ledc_timer_config(&t);
  ledc_channel_config_t c = { LED_GATE_PIN, LEDC_LOW_SPEED_MODE, LED_LEDC_CH, LEDC_INTR_DISABLE, LED_LEDC_TMR, 0, 0 };
  ledc_channel_config(&c);
}
static inline void ledSetDuty(uint8_t d) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CH, d);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CH);
}
static inline void ledRampNonBlocking() {
  uint32_t now = millis();
  if (now - tLast >= stepMs) {
    tLast = now;
    int next = (int)ledDuty + ledDir;
    if (next >= ledMax) { next = ledMax; ledDir = -1; }
    if (next <= ledMin) { next = ledMin; ledDir = +1; }
    ledDuty = (uint8_t)next;
    ledSetDuty(ledDuty);
  }
}

// ---------- FAN CYCLER (unsynced with LED) ----------
enum FanState : uint8_t { FAN_HOLD_OFF=0, FAN_RAMP_UP, FAN_HOLD_ON, FAN_RAMP_DOWN };
static FanState  fanState        = FAN_HOLD_OFF;
static uint32_t  fanStateStartMs = 0;
static uint32_t  fanStepLastMs   = 0;
static uint8_t   fanTarget       = 0;

// Choose timings that don't align with the 10 s LED cycle
const uint32_t holdOffMs   = 2000;   // 2 s OFF hold
const uint32_t holdOnMs    = 4000;   // 4 s ON hold
const uint16_t rampStepMs  = 15;     // 15 ms per duty step (~3.8 s end-to-end)
const uint8_t  rampStepVal = 1;      // duty increment per step

static void fanCycleUpdate() {
  uint32_t now = millis();

  switch (fanState) {
    case FAN_HOLD_OFF:
      fanTarget = 0;
      if (now - fanStateStartMs >= holdOffMs) {
        fanState = FAN_RAMP_UP;
        fanStepLastMs = now;
      }
      break;

    case FAN_RAMP_UP:
      if (now - fanStepLastMs >= rampStepMs) {
        fanStepLastMs = now;
        if (fanTarget + rampStepVal >= 255) {
          fanTarget = 255;
          fanState = FAN_HOLD_ON;
          fanStateStartMs = now;
        } else {
          fanTarget += rampStepVal;
        }
      }
      break;

    case FAN_HOLD_ON:
      fanTarget = 255;
      if (now - fanStateStartMs >= holdOnMs) {
        fanState = FAN_RAMP_DOWN;
        fanStepLastMs = now;
      }
      break;

    case FAN_RAMP_DOWN:
      if (now - fanStepLastMs >= rampStepMs) {
        fanStepLastMs = now;
        if (fanTarget <= rampStepVal) {
          fanTarget = 0;
          fanState = FAN_HOLD_OFF;
          fanStateStartMs = now;
        } else {
          fanTarget -= rampStepVal;
        }
      }
      break;
  }

  // Apply smoothed duty to hardware (with kick support)
  fanSetDutySmoothed(fanTarget);
}

// ---------- SETUP ----------
void setup() {
  delay(250);
  Serial.begin(115200);
  delay(150);

  pinMode(FAN_GATE_PIN, OUTPUT);
  fanPwmInit();

  pinMode(LED_GATE_PIN, OUTPUT);
  ledPwmInit();

  fanState        = FAN_HOLD_OFF;
  fanStateStartMs = millis();
  fanStepLastMs   = millis();

  Serial.println("QT Py ESP32 Pico: Fan cycle (no POT) + slow LED ramp");
}

// ---------- LOOP ----------
void loop() {
  // Fan autonomous cycle (unsynced with LED)
  fanCycleUpdate();

  // LED slow ramp
  ledRampNonBlocking();

  // Occasional telemetry
  static uint32_t tLastPrint = 0;
  uint32_t now = millis();
  if (now - tLastPrint >= 800) {
    tLastPrint = now;
    Serial.print("fanTarget=");
    Serial.print((int)fanTarget);
    Serial.print("  filtDuty=");
    Serial.print((int)filtDuty);
    Serial.print("  LED=");
    Serial.println((int)ledDuty);
  }

  delay(2);
}