#pragma once

#include <Arduino.h>

#include "Config.h"
#include "LedFrameGrid.h"

struct RawAudioRgbFramePusher {
  uint8_t byteBuffer = 0;
  uint8_t bitCount = 0;
  uint16_t pixelCursor = 0;
  uint8_t rgbComponent = 0;
  RgbColor pendingRgb = {0, 0, 0};
  uint32_t lastPublishUs = 0;
  bool frameDirty = false;

  void reset();
  void clear();
  void pushBit(uint8_t rawBit);
  void pushByte(uint8_t value);
  void fadeGrid();
  void publishIfDue();
  void ensureGrid();
};
