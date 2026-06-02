#include "CaptureTask.h"

#include <Arduino.h>
#include "soc/gpio_struct.h"

#include "Config.h"
#include "EdgeRingBuffer.h"

void IRAM_ATTR captureISR() {
  const uint32_t now = micros();
  const uint8_t level = (GPIO.in >> CAPTURE_PIN) & 0x01;

  pushEdgeFromIsr(now, level);
}

void captureCore0Task(void *parameter) {
  (void)parameter;

  pinMode(CAPTURE_PIN, INPUT);

  // On ESP32 Arduino, the interrupt is allocated on the core that calls attachInterrupt.
  attachInterrupt(digitalPinToInterrupt(CAPTURE_PIN), captureISR, CHANGE);

  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}
