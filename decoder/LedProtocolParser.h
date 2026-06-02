#pragma once

#include <Arduino.h>

#include "LedFrameGrid.h"

struct LedProtocolParser {
  enum State : uint8_t {
    READ_WIDTH = 0,
    READ_HEIGHT = 1,
    READ_BPP = 2,
    READ_INITIAL_PALETTE = 3,
    READ_RGB_FRAME = 4,
    READ_FRAME_PALETTE_CHANGE_BIT = 5,
    READ_PALETTE_UPDATE_COUNT = 6,
    READ_PALETTE_UPDATE_INDEX = 7,
    READ_PALETTE_UPDATE_RED = 8,
    READ_PALETTE_UPDATE_GREEN = 9,
    READ_PALETTE_UPDATE_BLUE = 10,
    READ_INDEXED_FRAME = 11,
    WAIT_FOR_SILENCE = 12
  };

  State state = READ_WIDTH;

  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t bpp = 0;
  uint16_t pixelCount = 0;

  RgbColor palette[256];
  uint16_t paletteEntryCount = 0;
  uint16_t paletteByteCursor = 0;

  uint16_t pixelCursor = 0;
  uint8_t rgbComponent = 0;
  RgbColor pendingRgb = {0, 0, 0};

  uint32_t bitBuffer = 0;
  uint8_t bitCount = 0;

  uint8_t paletteUpdatesRemaining = 0;
  uint8_t pendingPaletteIndex = 0;
  RgbColor pendingPaletteColor = {0, 0, 0};

  void reset();
  void feedBit(uint8_t bit);
  void feedByte(uint8_t value);

  void fail();
  void consumeHeaderByte(uint8_t value);
  void consumeInitialPaletteByte(uint8_t value);
  void consumeRgbFrameByte(uint8_t value);
  void beginIndexedFrame();
  bool readBits(uint8_t count, uint16_t &out);
  void parseAvailable();
};
