#include <Arduino.h>
#include <ModbusRTU.h>

// =====================
// Adafruit QT Py ESP32-C3 pin labels
// A2 = GPIO1, A3 = GPIO0 on this board variant. 
// =====================
#define FAN_GATE_PIN   A3   // use the board-labeled pin
#define LED_GATE_PIN   A2   // use the board-labeled pin

// RS-485 pins (keep as you wired them; these are board/PCB dependent)
#define RS485_RX_PIN    20
#define RS485_TX_PIN    21
#define RS485_DE_RE_PIN 10

ModbusRTU mb;

// Fan PWM is through an open-drain MOSFET stage -> inverted logic
#define FAN_PWM_INVERT 1

// LEDC config (Arduino-ESP32 core 3.x)
static const int FAN_PWM_HZ   = 25000;
static const int LED_PWM_HZ   = 2000;
static const int PWM_RES_BITS = 8;      // 0..255

static const uint8_t FAN_FIXED_DUTY = 255; // logical "full on" (will invert if FAN_PWM_INVERT=1)
static const uint8_t LED_FIXED_DUTY = 200;

static void pwmInit() {
  // Attach PWM to pins using Arduino-ESP32 core 3.x API:
  // ledcAttach(pin, freq, resolution_bits)
  ledcAttach(FAN_GATE_PIN, FAN_PWM_HZ, PWM_RES_BITS);
  ledcAttach(LED_GATE_PIN, LED_PWM_HZ, PWM_RES_BITS);
}

static inline void fanWrite(uint8_t duty) {
#if FAN_PWM_INVERT
  duty = 255 - duty;
#endif
  ledcWrite(FAN_GATE_PIN, duty);
}

static inline void ledWrite(uint8_t duty) {
  ledcWrite(LED_GATE_PIN, duty);
}

void setup() {
  delay(250);
  Serial.begin(115200);
  delay(150);

  pwmInit();

  // Modbus master alive for bus testing
  Serial1.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  mb.begin(&Serial1, RS485_DE_RE_PIN);
  mb.master();

  // Set fixed outputs
  fanWrite(FAN_FIXED_DUTY);
  ledWrite(LED_FIXED_DUTY);

  Serial.println("QT Py ESP32-C3 MASTER: Fan fixed ON + LED fixed duty=200 (pins by label)");
}

void loop() {
  mb.task();

  // Defensive re-assert
  static uint32_t tLast = 0;
  uint32_t now = millis();
  if (now - tLast >= 500) {
    tLast = now;
    fanWrite(FAN_FIXED_DUTY);
    ledWrite(LED_FIXED_DUTY);

    Serial.print("[MASTER] FAN(logical)=");
    Serial.print((int)FAN_FIXED_DUTY);
    Serial.print(" LED=");
    Serial.println((int)LED_FIXED_DUTY);
  }

  delay(2);
}