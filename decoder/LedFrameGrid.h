#pragma once

#include <Arduino.h>

#include "Config.h"

struct RgbColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

extern RgbColor ledGrid[MAX_GRID_PIXELS];
extern uint8_t ledGridWidth;
extern uint8_t ledGridHeight;
extern uint16_t ledGridPixelCount;
extern volatile uint32_t ledGridFrameCounter;
extern volatile bool ledGridFrameReady;

bool configureLedGrid(uint8_t width, uint8_t height);
void resetLedGrid();
void setLedGridPixel(uint16_t index, const RgbColor &color);
void publishLedGridFrame();
