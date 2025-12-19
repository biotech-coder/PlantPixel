#include <Arduino.h>
extern "C" {
  #include "driver/ledc.h"
}
#include <ModbusRTU.h>

// --------- PIN MAP ----------
#define FAN_GATE_PIN    5
#define LED_GATE_PIN    4

#define RS485_RX_PIN    20
#define RS485_TX_PIN    21
#define RS485_DE_RE_PIN 10

// --------- Modbus ----------
ModbusRTU mb;
static const uint8_t MODBUS_ID = 1;   // <-- change to 2 for your second slave, etc.

// --------- FAN PWM (25 kHz) ----------
#define FAN_LEDC_CH   LEDC_CHANNEL_0
#define FAN_LEDC_TMR  LEDC_TIMER_0
#define FAN_LEDC_HZ   25000
#define FAN_LEDC_RES  LEDC_TIMER_8_BIT  // 0..255

// --------- LED PWM (2 kHz) ----------
#define LED_LEDC_CH   LEDC_CHANNEL_1
#define LED_LEDC_TMR  LEDC_TIMER_1
#define LED_LEDC_HZ   2000
#define LED_LEDC_RES  LEDC_TIMER_8_BIT

#define FAN_PWM_INVERT 0

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

static const uint8_t LED_FIXED_DUTY = 200;
static const uint8_t FAN_FIXED_DUTY = 0;

void setup() {
  delay(250);
  Serial.begin(115200);
  delay(150);

  pinMode(FAN_GATE_PIN, OUTPUT);
  fanPwmInit();

  pinMode(LED_GATE_PIN, OUTPUT);
  ledPwmInit();

  // Modbus slave init
  Serial1.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  mb.begin(&Serial1, RS485_DE_RE_PIN);
  mb.slave(MODBUS_ID);

  // Set fixed outputs once at boot
  fanSetDutyRaw(FAN_FIXED_DUTY);
  ledSetDuty(LED_FIXED_DUTY);

  Serial.print("SLAVE ");
  Serial.print(MODBUS_ID);
  Serial.println(": Fan fixed HIGH + LED fixed duty (200)");
}

void loop() {
  mb.task();

  // Re-assert outputs periodically (defensive)
  static uint32_t tLast = 0;
  uint32_t now = millis();
  if (now - tLast >= 500) {
    tLast = now;
    fanSetDutyRaw(FAN_FIXED_DUTY);
    ledSetDuty(LED_FIXED_DUTY);

    Serial.print("[SLAVE ");
    Serial.print(MODBUS_ID);
    Serial.print("] FAN=");
    Serial.print((int)FAN_FIXED_DUTY);
    Serial.print(" LED=");
    Serial.println((int)LED_FIXED_DUTY);
  }

  delay(2);
}