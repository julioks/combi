#include "LedSerialDriver.h"

#include <Arduino.h>

#include "Config.h"
#include "LedFrameGrid.h"

#if LED_SERIAL_OUTPUT_MODE != LED_SERIAL_OUTPUT_FULL_FRAME && \
    LED_SERIAL_OUTPUT_MODE != LED_SERIAL_OUTPUT_FRAME_NUMBER_ONLY
#error Unknown LED_SERIAL_OUTPUT_MODE selected in Config.h
#endif

#if LED_SERIAL_OUTPUT_MODE == LED_SERIAL_OUTPUT_FULL_FRAME
static void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}
#endif

void setupLedSerialDriver() {
}

void writeLedSerialFrame(uint32_t frameCounter) {
#if LED_SERIAL_OUTPUT_MODE == LED_SERIAL_OUTPUT_FRAME_NUMBER_ONLY
  Serial.print("frame ");
  Serial.println(frameCounter);
#else
  Serial.print("frame ");
  Serial.print(frameCounter);
  Serial.print(" grid ");
  Serial.print(ledGridWidth);
  Serial.print('x');
  Serial.print(ledGridHeight);
  Serial.print(" pixels ");
  Serial.println(ledGridPixelCount);

  for (uint8_t y = 0; y < ledGridHeight; y++) {
    Serial.print("row ");
    Serial.print(y);
    Serial.print(':');

    for (uint8_t x = 0; x < ledGridWidth; x++) {
      const uint16_t index = ((uint16_t)y * ledGridWidth) + x;
      const RgbColor &pixel = ledGrid[index];

      Serial.print(" #");
      printHexByte(pixel.r);
      printHexByte(pixel.g);
      printHexByte(pixel.b);
    }

    Serial.println();
  }

  Serial.println("end frame");
#endif
}
