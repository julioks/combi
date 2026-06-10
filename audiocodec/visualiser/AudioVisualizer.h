#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "../audio/AudioAnalysis.h"

static constexpr float VISUALIZER_TWO_PI_F = 6.28318530718f;

extern uint8_t currentLedBrightness;

static inline float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static inline float wrap01(float value) {
  while (value < 0.0f) value += 1.0f;
  while (value >= 1.0f) value -= 1.0f;
  return value;
}

static inline uint8_t visualizerCorrectChannel(float value, uint16_t pixelIndex, uint8_t channel, uint8_t ditherFrame) {
  if (value <= 0.0f || currentLedBrightness == 0) {
    return 0;
  }
  if (value > 255.0f) {
    value = 255.0f;
  }

  float normalized = value / 255.0f;
  float corrected = normalized * normalized * sqrtf(normalized) * 255.0f;

  if (currentLedBrightness < 255) {
    corrected *= 255.0f / (float)currentLedBrightness;
  }
  if (corrected > 255.0f) {
    corrected = 255.0f;
  }

  uint8_t base = (uint8_t)corrected;
  float fraction = corrected - (float)base;
  uint8_t threshold = (uint8_t)((pixelIndex * 37u + channel * 67u + ditherFrame * 29u) & 0xFF);

  if (fraction * 255.0f > (float)threshold && base < 255) {
    base++;
  }

  return base;
}

static inline uint32_t visualizerColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, float r, float g, float b, uint8_t ditherFrame) {
  return pixels.Color(
    visualizerCorrectChannel(r, pixelIndex, 0, ditherFrame),
    visualizerCorrectChannel(g, pixelIndex, 1, ditherFrame),
    visualizerCorrectChannel(b, pixelIndex, 2, ditherFrame)
  );
}

enum VisualizerPaletteId : uint8_t {
  VISUALIZER_PALETTE_STANDARD = 0,
  VISUALIZER_PALETTE_WARM_CANDY = 1,
  VISUALIZER_PALETTE_AURORA = 2,
  VISUALIZER_PALETTE_COUNT = 3
};

struct VisualizerPaletteStop {
  float position;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static inline const char* visualizerPaletteName(VisualizerPaletteId palette) {
  switch (palette) {
    case VISUALIZER_PALETTE_STANDARD:
      return "standard-blue-green-red";
    case VISUALIZER_PALETTE_WARM_CANDY:
      return "warm-candy";
    case VISUALIZER_PALETTE_AURORA:
      return "aurora";
    default:
      return "standard-blue-green-red";
  }
}

static inline bool visualizerPaletteFromNumber(uint8_t number, VisualizerPaletteId& palette) {
  if (number >= VISUALIZER_PALETTE_COUNT) {
    return false;
  }

  palette = (VisualizerPaletteId)number;
  return true;
}

static inline void visualizerSampleStops(const VisualizerPaletteStop* stops, uint8_t count, float tone, float& r, float& g, float& b) {
  tone = clamp01(tone);

  if (count == 0) {
    r = 0.0f;
    g = 0.0f;
    b = 0.0f;
    return;
  }

  if (count == 1 || tone <= stops[0].position) {
    r = (float)stops[0].r;
    g = (float)stops[0].g;
    b = (float)stops[0].b;
    return;
  }

  for (uint8_t i = 0; i < count - 1; i++) {
    const VisualizerPaletteStop& a = stops[i];
    const VisualizerPaletteStop& z = stops[i + 1];
    if (tone > z.position) {
      continue;
    }

    float span = z.position - a.position;
    float mix = span > 0.0f ? (tone - a.position) / span : 0.0f;
    r = (float)a.r + ((float)z.r - (float)a.r) * mix;
    g = (float)a.g + ((float)z.g - (float)a.g) * mix;
    b = (float)a.b + ((float)z.b - (float)a.b) * mix;
    return;
  }

  r = (float)stops[count - 1].r;
  g = (float)stops[count - 1].g;
  b = (float)stops[count - 1].b;
}

static inline void visualizerSamplePalette(VisualizerPaletteId palette, float tone, float& r, float& g, float& b) {
  static constexpr VisualizerPaletteStop standard[] = {
    {0.00f,   0,  55, 255},
    {0.26f,   0, 210, 255},
    {0.50f,   0, 255,  76},
    {0.74f, 255, 225,   0},
    {1.00f, 255,   0,   0}
  };

  static constexpr VisualizerPaletteStop warmCandy[] = {
    {0.00f, 255,  36, 210},
    {0.30f, 255,  55, 130},
    {0.62f, 255, 112,  34},
    {1.00f, 255, 218,  38}
  };

  static constexpr VisualizerPaletteStop aurora[] = {
    {0.00f,  16, 248, 190},
    {0.33f,  44, 116, 255},
    {0.66f, 180,  64, 255},
    {1.00f, 248, 252, 210}
  };

  switch (palette) {
    case VISUALIZER_PALETTE_WARM_CANDY:
      visualizerSampleStops(warmCandy, sizeof(warmCandy) / sizeof(warmCandy[0]), tone, r, g, b);
      return;
    case VISUALIZER_PALETTE_AURORA:
      visualizerSampleStops(aurora, sizeof(aurora) / sizeof(aurora[0]), tone, r, g, b);
      return;
    case VISUALIZER_PALETTE_STANDARD:
    default:
      visualizerSampleStops(standard, sizeof(standard) / sizeof(standard[0]), tone, r, g, b);
      return;
  }
}

class VisualizerCanvas {
public:
  void clear() {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      red[i] = 0.0f;
      green[i] = 0.0f;
      blue[i] = 0.0f;
    }
  }

  void fade(float amount, float floor) {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      red[i] *= amount;
      green[i] *= amount;
      blue[i] *= amount;

      if (red[i] < floor) red[i] = 0.0f;
      if (green[i] < floor) green[i] = 0.0f;
      if (blue[i] < floor) blue[i] = 0.0f;
    }
  }

  void setPixel(uint16_t index, float r, float g, float b) {
    if (index >= LED_COUNT) {
      return;
    }

    red[index] = clampChannel(r);
    green[index] = clampChannel(g);
    blue[index] = clampChannel(b);
  }

  void addPixel(uint16_t index, float r, float g, float b) {
    if (index >= LED_COUNT) {
      return;
    }

    red[index] = clampChannel(red[index] + r);
    green[index] = clampChannel(green[index] + g);
    blue[index] = clampChannel(blue[index] + b);
  }

  void addPixelSafe(int16_t x, int16_t y, float r, float g, float b) {
    if (x < 0 || x >= LED_DRIVER_GRID_WIDTH || y < 0 || y >= LED_DRIVER_GRID_HEIGHT) {
      return;
    }

    addPixel(visualizerLedIndexXY((uint16_t)x, (uint16_t)y), r, g, b);
  }

  void blendAdd(const VisualizerCanvas& source) {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      addPixel(i, source.red[i], source.green[i], source.blue[i]);
    }
  }

  void writeTo(Adafruit_NeoPixel& pixels, uint8_t ditherFrame) const {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, visualizerColor(pixels, i, red[i], green[i], blue[i], ditherFrame));
    }
  }

private:
  float red[LED_COUNT];
  float green[LED_COUNT];
  float blue[LED_COUNT];

  float clampChannel(float value) const {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 255.0f) {
      return 255.0f;
    }
    return value;
  }
};

class VisualizerLayerEffect {
public:
  virtual const char* name() const = 0;
  virtual void begin() {
    reset();
  }
  virtual void reset() = 0;
  virtual void render(VisualizerCanvas& canvas, const AudioAnalysisFrame& audio, VisualizerPaletteId palette) = 0;
};
