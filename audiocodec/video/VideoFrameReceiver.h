#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

static constexpr uint32_t VIDEO_MAX_GRID_PIXELS = 1024;

struct VideoRgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

class VideoFrameReceiver {
public:
  void reset() {
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
    frameReady = false;
  }

  void feedBit(uint8_t bit) {
    if (state == WAIT_FOR_SILENCE) {
      return;
    }

    bitBuffer = (bitBuffer << 1) | (uint32_t)(bit & 0x01);
    bitCount++;

    if (bitCount > 24) {
      fail();
      return;
    }

    parseAvailable();
  }

  void service(Adafruit_NeoPixel& pixels) {
    if (!frameReady || frameCounter == lastDrivenFrameCounter) {
      return;
    }

    lastDrivenFrameCounter = frameCounter;
    frameReady = false;

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        VideoRgb pixel = {0, 0, 0};
        if (x < width && y < height) {
          const uint16_t sourceIndex = ((uint16_t)y * width) + x;
          if (sourceIndex < pixelCount) {
            pixel = grid[sourceIndex];
          }
        }

        pixels.setPixelColor(ledIndexXY(x, y), pixels.Color(pixel.r, pixel.g, pixel.b));
      }
    }

    pixels.show();
  }

private:
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
  VideoRgb grid[VIDEO_MAX_GRID_PIXELS];

  VideoRgb palette[256];
  uint16_t paletteEntryCount = 0;
  uint16_t paletteByteCursor = 0;

  uint16_t pixelCursor = 0;
  uint8_t rgbComponent = 0;
  VideoRgb pendingRgb = {0, 0, 0};

  uint32_t bitBuffer = 0;
  uint8_t bitCount = 0;

  uint8_t paletteUpdatesRemaining = 0;
  uint8_t pendingPaletteIndex = 0;
  VideoRgb pendingPaletteColor = {0, 0, 0};

  uint32_t frameCounter = 0;
  uint32_t lastDrivenFrameCounter = 0;
  bool frameReady = false;

  bool configureGrid(uint8_t nextWidth, uint8_t nextHeight) {
    const uint32_t nextPixelCount = (uint32_t)nextWidth * (uint32_t)nextHeight;
    if (nextPixelCount == 0 || nextPixelCount > VIDEO_MAX_GRID_PIXELS) {
      width = 0;
      height = 0;
      pixelCount = 0;
      return false;
    }

    width = nextWidth;
    height = nextHeight;
    pixelCount = (uint16_t)nextPixelCount;
    for (uint16_t i = 0; i < pixelCount; i++) {
      grid[i] = {0, 0, 0};
    }
    return true;
  }

  void setPixel(uint16_t index, const VideoRgb& color) {
    if (index < pixelCount) {
      grid[index] = color;
    }
  }

  void publishFrame() {
    frameCounter++;
    frameReady = true;
  }

  void fail() {
    Serial.println("error");
    state = WAIT_FOR_SILENCE;
  }

  bool readBits(uint8_t count, uint16_t& out) {
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

  void consumeHeaderByte(uint8_t value) {
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
    if (bpp > 8 || !configureGrid(width, height)) {
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

  void consumeInitialPaletteByte(uint8_t value) {
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

  void consumeRgbFrameByte(uint8_t value) {
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
    setPixel(pixelCursor, pendingRgb);
    pixelCursor++;
    rgbComponent = 0;

    if (pixelCursor >= pixelCount) {
      publishFrame();
      pixelCursor = 0;
    }
  }

  void beginIndexedFrame() {
    pixelCursor = 0;
    state = READ_INDEXED_FRAME;
  }

  void parseAvailable() {
    while (state != WAIT_FOR_SILENCE) {
      uint16_t value = 0;

      if (state == READ_WIDTH || state == READ_HEIGHT || state == READ_BPP) {
        if (!readBits(8, value)) return;
        consumeHeaderByte((uint8_t)value);
        continue;
      }

      if (state == READ_INITIAL_PALETTE) {
        if (!readBits(8, value)) return;
        consumeInitialPaletteByte((uint8_t)value);
        continue;
      }

      if (state == READ_RGB_FRAME) {
        if (!readBits(8, value)) return;
        consumeRgbFrameByte((uint8_t)value);
        continue;
      }

      if (state == READ_FRAME_PALETTE_CHANGE_BIT) {
        if (!readBits(1, value)) return;
        state = value == 0 ? READ_INDEXED_FRAME : READ_PALETTE_UPDATE_COUNT;
        if (value == 0) {
          beginIndexedFrame();
        }
        continue;
      }

      if (state == READ_PALETTE_UPDATE_COUNT) {
        if (!readBits(8, value)) return;
        paletteUpdatesRemaining = (uint8_t)value;
        state = paletteUpdatesRemaining == 0 ? READ_INDEXED_FRAME : READ_PALETTE_UPDATE_INDEX;
        if (paletteUpdatesRemaining == 0) {
          beginIndexedFrame();
        }
        continue;
      }

      if (state == READ_PALETTE_UPDATE_INDEX) {
        if (!readBits(8, value)) return;
        if (value >= paletteEntryCount) {
          fail();
          return;
        }
        pendingPaletteIndex = (uint8_t)value;
        state = READ_PALETTE_UPDATE_RED;
        continue;
      }

      if (state == READ_PALETTE_UPDATE_RED) {
        if (!readBits(8, value)) return;
        pendingPaletteColor.r = (uint8_t)value;
        state = READ_PALETTE_UPDATE_GREEN;
        continue;
      }

      if (state == READ_PALETTE_UPDATE_GREEN) {
        if (!readBits(8, value)) return;
        pendingPaletteColor.g = (uint8_t)value;
        state = READ_PALETTE_UPDATE_BLUE;
        continue;
      }

      if (state == READ_PALETTE_UPDATE_BLUE) {
        if (!readBits(8, value)) return;
        pendingPaletteColor.b = (uint8_t)value;
        palette[pendingPaletteIndex] = pendingPaletteColor;
        paletteUpdatesRemaining--;
        state = paletteUpdatesRemaining == 0 ? READ_INDEXED_FRAME : READ_PALETTE_UPDATE_INDEX;
        if (paletteUpdatesRemaining == 0) {
          beginIndexedFrame();
        }
        continue;
      }

      if (state == READ_INDEXED_FRAME) {
        if (!readBits(bpp, value)) return;
        if (value >= paletteEntryCount) {
          fail();
          return;
        }
        setPixel(pixelCursor, palette[value]);
        pixelCursor++;
        if (pixelCursor >= pixelCount) {
          publishFrame();
          state = READ_FRAME_PALETTE_CHANGE_BIT;
        }
        continue;
      }

      fail();
      return;
    }
  }
};
