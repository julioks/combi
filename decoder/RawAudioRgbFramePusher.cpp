#include "RawAudioRgbFramePusher.h"

void RawAudioRgbFramePusher::reset() {
  byteBuffer = 0;
  bitCount = 0;
  pixelCursor = 0;
  rgbComponent = 0;
  pendingRgb = {0, 0, 0};
  lastPublishUs = 0;
  frameDirty = false;
}

void RawAudioRgbFramePusher::ensureGrid() {
  if (ledGridWidth == RAW_AUDIO_RGB_WIDTH &&
      ledGridHeight == RAW_AUDIO_RGB_HEIGHT &&
      ledGridPixelCount == RAW_AUDIO_RGB_PIXEL_COUNT) {
    return;
  }

  configureLedGrid(RAW_AUDIO_RGB_WIDTH, RAW_AUDIO_RGB_HEIGHT);
}

void RawAudioRgbFramePusher::clear() {
  ensureGrid();

  const RgbColor black = {0, 0, 0};
  for (uint16_t i = 0; i < RAW_AUDIO_RGB_PIXEL_COUNT; i++) {
    setLedGridPixel(i, black);
  }

  publishLedGridFrame();
  lastPublishUs = micros();
  frameDirty = false;
}

void RawAudioRgbFramePusher::pushByte(uint8_t value) {
  ensureGrid();

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
  frameDirty = true;

  pixelCursor++;
  if (pixelCursor >= RAW_AUDIO_RGB_PIXEL_COUNT) {
    pixelCursor = 0;
  }

  rgbComponent = 0;
}

static uint8_t fadeChannel(uint8_t value) {
#if RAW_AUDIO_RGB_FADE_STEP > 0
  return value > RAW_AUDIO_RGB_FADE_STEP ? value - RAW_AUDIO_RGB_FADE_STEP : 0;
#else
  return value;
#endif
}

void RawAudioRgbFramePusher::fadeGrid() {
#if RAW_AUDIO_RGB_FADE_STEP > 0
  ensureGrid();

  for (uint16_t i = 0; i < RAW_AUDIO_RGB_PIXEL_COUNT; i++) {
    ledGrid[i].r = fadeChannel(ledGrid[i].r);
    ledGrid[i].g = fadeChannel(ledGrid[i].g);
    ledGrid[i].b = fadeChannel(ledGrid[i].b);
  }
#endif
}

void RawAudioRgbFramePusher::publishIfDue() {
#if RAW_AUDIO_RGB_FADE_STEP <= 0
  if (!frameDirty) {
    return;
  }
#else
  if (!frameDirty && lastPublishUs == 0) {
    return;
  }
#endif

  const uint32_t nowUs = micros();
  if (lastPublishUs != 0 &&
      (uint32_t)(nowUs - lastPublishUs) < RAW_AUDIO_RGB_PUBLISH_INTERVAL_US) {
    return;
  }

  fadeGrid();
  publishLedGridFrame();
  lastPublishUs = nowUs;
  frameDirty = false;
}

void RawAudioRgbFramePusher::pushBit(uint8_t rawBit) {
  uint8_t bit = rawBit & 0x01;

#if RAW_AUDIO_RGB_INVERT_BITS
  bit ^= 0x01;
#endif

  byteBuffer = (byteBuffer << 1) | bit;
  bitCount++;

  if (bitCount >= 8) {
    pushByte(byteBuffer);
    byteBuffer = 0;
    bitCount = 0;
  }

  publishIfDue();
}
