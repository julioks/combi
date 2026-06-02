#include "LedFrameGrid.h"

RgbColor ledGrid[MAX_GRID_PIXELS];
uint8_t ledGridWidth = 0;
uint8_t ledGridHeight = 0;
uint16_t ledGridPixelCount = 0;
volatile uint32_t ledGridFrameCounter = 0;
volatile bool ledGridFrameReady = false;

bool configureLedGrid(uint8_t width, uint8_t height) {
  const uint32_t pixelCount = (uint32_t)width * (uint32_t)height;

  if (pixelCount == 0 || pixelCount > MAX_GRID_PIXELS) {
    resetLedGrid();
    return false;
  }

  ledGridWidth = width;
  ledGridHeight = height;
  ledGridPixelCount = (uint16_t)pixelCount;
  ledGridFrameReady = false;

  for (uint16_t i = 0; i < ledGridPixelCount; i++) {
    ledGrid[i] = {0, 0, 0};
  }

  return true;
}

void resetLedGrid() {
  ledGridWidth = 0;
  ledGridHeight = 0;
  ledGridPixelCount = 0;
  ledGridFrameReady = false;
}

void setLedGridPixel(uint16_t index, const RgbColor &color) {
  if (index >= ledGridPixelCount) {
    return;
  }

  ledGrid[index] = color;
}

void publishLedGridFrame() {
  ledGridFrameCounter++;
  ledGridFrameReady = true;
}
