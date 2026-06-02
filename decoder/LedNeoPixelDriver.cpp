#include "Config.h"

#if LED_OUTPUT_DRIVER == LED_OUTPUT_DRIVER_NEOPIXEL

#include "LedNeoPixelDriver.h"

#include <Adafruit_NeoPixel.h>

#include "LedFrameGrid.h"

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

static uint16_t physicalPixelIndex(uint8_t x, uint8_t y) {
#if LED_DRIVER_LAYOUT == LED_DRIVER_LAYOUT_ROW_MAJOR
  return ((uint16_t)y * LED_DRIVER_GRID_WIDTH) + x;
#elif LED_DRIVER_LAYOUT == LED_DRIVER_LAYOUT_ROW_SERPENTINE
  if (y & 0x01) {
    x = (LED_DRIVER_GRID_WIDTH - 1) - x;
  }

  return ((uint16_t)y * LED_DRIVER_GRID_WIDTH) + x;
#elif LED_DRIVER_LAYOUT == LED_DRIVER_LAYOUT_COLUMN_MAJOR
  return ((uint16_t)x * LED_DRIVER_GRID_HEIGHT) + y;
#elif LED_DRIVER_LAYOUT == LED_DRIVER_LAYOUT_COLUMN_SERPENTINE
  if (x & 0x01) {
    y = (LED_DRIVER_GRID_HEIGHT - 1) - y;
  }

  return ((uint16_t)x * LED_DRIVER_GRID_HEIGHT) + y;
#else
#error Unknown LED_DRIVER_LAYOUT selected in Config.h
#endif
}

void setupLedNeoPixelDriver() {
  strip.begin();
  strip.clear();
  strip.show();
}

void writeLedNeoPixelFrame(uint32_t frameCounter) {
  (void)frameCounter;

  for (uint8_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
    for (uint8_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
      RgbColor pixel = {0, 0, 0};

      // No scaling: crop oversized input to 16x16 and leave missing pixels off.
      if (x < ledGridWidth && y < ledGridHeight) {
        const uint16_t sourceIndex = ((uint16_t)y * ledGridWidth) + x;
        if (sourceIndex < ledGridPixelCount) {
          pixel = ledGrid[sourceIndex];
        }
      }

      strip.setPixelColor(physicalPixelIndex(x, y), strip.Color(pixel.r, pixel.g, pixel.b));
    }
  }

  strip.show();
}

#endif
