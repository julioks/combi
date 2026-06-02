#include "LedDriver.h"

#include <Arduino.h>

#include "Config.h"
#include "LedFrameGrid.h"

#if LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_SERIAL
#include "LedSerialDriver.h"
#elif LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_NEOPIXEL
#include "LedNeoPixelDriver.h"
#else
#error Unknown LED_OUTPUT_DRIVER selected in Config.h
#endif

static uint32_t lastDrivenFrameCounter = 0;

void setupLedDriver() {
#if LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_SERIAL
  setupLedSerialDriver();
#elif LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_NEOPIXEL
  setupLedNeoPixelDriver();
#endif
}

void serviceLedDriver() {
  const uint32_t frameCounter = ledGridFrameCounter;

  if (!ledGridFrameReady || frameCounter == lastDrivenFrameCounter) {
    return;
  }

  lastDrivenFrameCounter = frameCounter;

#if LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_SERIAL
  writeLedSerialFrame(frameCounter);
#elif LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_NEOPIXEL
  writeLedNeoPixelFrame(frameCounter);
#endif

  ledGridFrameReady = false;
}
