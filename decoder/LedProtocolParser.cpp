#include "LedProtocolParser.h"

#include "Config.h"

void LedProtocolParser::reset() {
  state = READ_WIDTH;

  width = 0;
  height = 0;
  bpp = 0;
  pixelCount = 0;

  paletteEntryCount = 0;
  paletteByteCursor = 0;

  pixelCursor = 0;
  rgbComponent = 0;
  pendingRgb = {0, 0, 0};

  bitBuffer = 0;
  bitCount = 0;

  paletteUpdatesRemaining = 0;
  pendingPaletteIndex = 0;
  pendingPaletteColor = {0, 0, 0};
}

void LedProtocolParser::feedByte(uint8_t value) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    feedBit((value >> bit) & 0x01);
  }
}

void LedProtocolParser::feedBit(uint8_t bit) {
  if (state == WAIT_FOR_SILENCE) {
    return;
  }

  bitBuffer = (bitBuffer << 1) | (bit & 0x01);
  bitCount++;

  if (bitCount > 24) {
    fail();
    return;
  }

  parseAvailable();
}

void LedProtocolParser::fail() {
  Serial.println("error");
  state = WAIT_FOR_SILENCE;
}

void LedProtocolParser::consumeHeaderByte(uint8_t value) {
  if (state == READ_WIDTH) {
    width = value == 0 ? 1 : value;
    state = READ_HEIGHT;
    return;
  }

  if (state == READ_HEIGHT) {
    height = value == 0 ? 1 : value;
    state = READ_BPP;
    return;
  }

  if (state != READ_BPP) {
    fail();
    return;
  }

  bpp = value;

  if (bpp > 8) {
    fail();
    return;
  }

  const uint32_t headerPixelCount = (uint32_t)width * (uint32_t)height;
  if (headerPixelCount == 0 || headerPixelCount > MAX_GRID_PIXELS) {
    fail();
    return;
  }

  pixelCount = (uint16_t)headerPixelCount;
  if (!configureLedGrid(width, height)) {
    fail();
    return;
  }

  if (bpp == 0) {
    pixelCursor = 0;
    rgbComponent = 0;
    pendingRgb = {0, 0, 0};
    state = READ_RGB_FRAME;
    return;
  }

  paletteEntryCount = (uint16_t)1U << bpp;
  paletteByteCursor = 0;
  state = READ_INITIAL_PALETTE;
}

void LedProtocolParser::consumeInitialPaletteByte(uint8_t value) {
  const uint16_t paletteIndex = paletteByteCursor / 3;
  const uint8_t component = paletteByteCursor % 3;

  if (paletteIndex >= paletteEntryCount) {
    fail();
    return;
  }

  if (component == 0) {
    palette[paletteIndex].r = value;
  } else if (component == 1) {
    palette[paletteIndex].g = value;
  } else {
    palette[paletteIndex].b = value;
  }

  paletteByteCursor++;

  if (paletteByteCursor >= paletteEntryCount * 3U) {
    bitBuffer = 0;
    bitCount = 0;
    state = READ_FRAME_PALETTE_CHANGE_BIT;
  }
}

void LedProtocolParser::consumeRgbFrameByte(uint8_t value) {
  if (rgbComponent == 0) {
    pendingRgb.r = value;
    rgbComponent = 1;
    return;
  }

  if (rgbComponent == 1) {
    pendingRgb.g = value;
    rgbComponent = 2;
    return;
  }

  pendingRgb.b = value;
  setLedGridPixel(pixelCursor, pendingRgb);

  pixelCursor++;
  rgbComponent = 0;

  if (pixelCursor >= pixelCount) {
    publishLedGridFrame();
    pixelCursor = 0;
  }
}

void LedProtocolParser::beginIndexedFrame() {
  pixelCursor = 0;
  state = READ_INDEXED_FRAME;
}

bool LedProtocolParser::readBits(uint8_t count, uint16_t &out) {
  if (count == 0 || bitCount < count) {
    return false;
  }

  const uint32_t mask = (1UL << count) - 1UL;
  out = (uint16_t)((bitBuffer >> (bitCount - count)) & mask);
  bitCount -= count;

  if (bitCount == 0) {
    bitBuffer = 0;
  } else {
    bitBuffer &= (1UL << bitCount) - 1UL;
  }

  return true;
}

void LedProtocolParser::parseAvailable() {
  while (state != WAIT_FOR_SILENCE) {
    uint16_t value = 0;

    if (state == READ_WIDTH || state == READ_HEIGHT || state == READ_BPP) {
      if (!readBits(8, value)) {
        return;
      }

      consumeHeaderByte((uint8_t)value);
      continue;
    }

    if (state == READ_INITIAL_PALETTE) {
      if (!readBits(8, value)) {
        return;
      }

      consumeInitialPaletteByte((uint8_t)value);
      continue;
    }

    if (state == READ_RGB_FRAME) {
      if (!readBits(8, value)) {
        return;
      }

      consumeRgbFrameByte((uint8_t)value);
      continue;
    }

    if (state == READ_FRAME_PALETTE_CHANGE_BIT) {
      if (!readBits(1, value)) {
        return;
      }

      if (value == 0) {
        beginIndexedFrame();
      } else {
        state = READ_PALETTE_UPDATE_COUNT;
      }
      continue;
    }

    if (state == READ_PALETTE_UPDATE_COUNT) {
      if (!readBits(8, value)) {
        return;
      }

      paletteUpdatesRemaining = (uint8_t)value;
      if (paletteUpdatesRemaining == 0) {
        beginIndexedFrame();
      } else {
        state = READ_PALETTE_UPDATE_INDEX;
      }
      continue;
    }

    if (state == READ_PALETTE_UPDATE_INDEX) {
      if (!readBits(8, value)) {
        return;
      }

      if (value >= paletteEntryCount) {
        fail();
        return;
      }

      pendingPaletteIndex = (uint8_t)value;
      state = READ_PALETTE_UPDATE_RED;
      continue;
    }

    if (state == READ_PALETTE_UPDATE_RED) {
      if (!readBits(8, value)) {
        return;
      }

      pendingPaletteColor.r = (uint8_t)value;
      state = READ_PALETTE_UPDATE_GREEN;
      continue;
    }

    if (state == READ_PALETTE_UPDATE_GREEN) {
      if (!readBits(8, value)) {
        return;
      }

      pendingPaletteColor.g = (uint8_t)value;
      state = READ_PALETTE_UPDATE_BLUE;
      continue;
    }

    if (state == READ_PALETTE_UPDATE_BLUE) {
      if (!readBits(8, value)) {
        return;
      }

      pendingPaletteColor.b = (uint8_t)value;
      palette[pendingPaletteIndex] = pendingPaletteColor;
      paletteUpdatesRemaining--;

      if (paletteUpdatesRemaining == 0) {
        beginIndexedFrame();
      } else {
        state = READ_PALETTE_UPDATE_INDEX;
      }
      continue;
    }

    if (state == READ_INDEXED_FRAME) {
      if (!readBits(bpp, value)) {
        return;
      }

      if (value >= paletteEntryCount) {
        fail();
        return;
      }

      setLedGridPixel(pixelCursor, palette[value]);
      pixelCursor++;

      if (pixelCursor >= pixelCount) {
        publishLedGridFrame();
        state = READ_FRAME_PALETTE_CHANGE_BIT;
      }
      continue;
    }

    fail();
    return;
  }
}
