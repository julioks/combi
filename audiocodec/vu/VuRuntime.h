#pragma once

#include <math.h>

#include "../visualiser/SpectrumGridVisualizer.h"
#include "VuProgramStore.h"

static constexpr uint8_t VU_VM_STACK_SIZE = 24;
static constexpr float VU_TWO_PI_F = 6.28318530718f;

class VuRuntime {
public:
  void begin(VuProgramStore* programStore) {
    store = programStore;
    fallbackSpectrum.begin();
    frameCanvas.clear();
    trailCanvas.clear();
    ditherFrame = 0;
    clearLayerStates();
  }

  void reset() {
    fallbackSpectrum.reset();
    frameCanvas.clear();
    trailCanvas.clear();
    ditherFrame = 0;
    clearLayerStates();
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) {
    frameCanvas.clear();

    if (store == nullptr || !store->hasLayers()) {
      trailCanvas.clear();
      fallbackSpectrum.render(frameCanvas, audio, VISUALIZER_PALETTE_STANDARD);
      frameCanvas.writeTo(pixels, ditherFrame++);
      return;
    }

    const uint8_t layerCount = store->count();
    bool renderedTrailLayer = false;
    bool fadedTrailCanvas = false;
    for (uint8_t i = 0; i < layerCount; i++) {
      const VuLayerProgram& layer = store->layer(i);
      if (!layer.loaded || layer.pixelProgramLength == 0) {
        continue;
      }

      if ((layer.flags & VU_LAYER_FLAG_TRAIL) != 0) {
        if (!fadedTrailCanvas) {
          trailCanvas.fade(layerTrailFade(i));
          fadedTrailCanvas = true;
        }
        renderedTrailLayer = true;
        renderPixelProgram(layer, i, audio, trailCanvas);
      } else {
        renderPixelProgram(layer, i, audio, frameCanvas);
      }
    }

    if (renderedTrailLayer) {
      trailCanvas.blendInto(frameCanvas);
    } else {
      trailCanvas.clear();
    }

    frameCanvas.writeTo(pixels, ditherFrame++);
  }

private:
  struct VmStack {
    float values[VU_VM_STACK_SIZE];
    uint8_t count = 0;

    void clear() {
      count = 0;
    }

    bool push(float value) {
      if (count >= VU_VM_STACK_SIZE) {
        return false;
      }
      values[count++] = value;
      return true;
    }

    bool pop(float& value) {
      if (count == 0) {
        value = 0.0f;
        return false;
      }
      value = values[--count];
      return true;
    }
  };

  struct CompactTrailCanvas {
    uint8_t red[LED_COUNT];
    uint8_t green[LED_COUNT];
    uint8_t blue[LED_COUNT];

    void clear() {
      for (uint16_t i = 0; i < LED_COUNT; i++) {
        red[i] = 0;
        green[i] = 0;
        blue[i] = 0;
      }
    }

    void fade(float amount) {
      const uint16_t scale = (uint16_t)(clamp01(amount) * 255.0f);
      for (uint16_t i = 0; i < LED_COUNT; i++) {
        red[i] = (uint8_t)(((uint16_t)red[i] * scale) / 255U);
        green[i] = (uint8_t)(((uint16_t)green[i] * scale) / 255U);
        blue[i] = (uint8_t)(((uint16_t)blue[i] * scale) / 255U);
      }
    }

    void addPixel(uint16_t index, float r, float g, float b) {
      if (index >= LED_COUNT) {
        return;
      }

      red[index] = addChannel(red[index], r);
      green[index] = addChannel(green[index], g);
      blue[index] = addChannel(blue[index], b);
    }

    void blendInto(VisualizerCanvas& target) const {
      for (uint16_t i = 0; i < LED_COUNT; i++) {
        target.addPixel(i, red[i], green[i], blue[i]);
      }
    }

    uint8_t addChannel(uint8_t current, float amount) const {
      if (amount <= 0.0f) {
        return current;
      }
      const uint16_t next = (uint16_t)current + toByte(amount);
      return next > 255U ? 255U : (uint8_t)next;
    }

    uint8_t toByte(float value) const {
      if (value <= 0.0f) {
        return 0;
      }
      if (value >= 255.0f) {
        return 255;
      }
      return (uint8_t)(value + 0.5f);
    }
  };

  VuProgramStore* store = nullptr;
  SpectrumGridEffect fallbackSpectrum;
  VisualizerCanvas frameCanvas;
  CompactTrailCanvas trailCanvas;
  float layerState[VU_MAX_LAYERS][VU_LAYER_STATE_SLOTS];
  uint8_t ditherFrame = 0;

  void clearLayerStates() {
    for (uint8_t layer = 0; layer < VU_MAX_LAYERS; layer++) {
      for (uint8_t slot = 0; slot < VU_LAYER_STATE_SLOTS; slot++) {
        layerState[layer][slot] = 0.0f;
      }
    }
  }

  float layerTrailFade(uint8_t layerIndex) const {
    float fade = 0.82f;
    if (layerIndex < VU_MAX_LAYERS) {
      fade = layerState[layerIndex][VU_TRAIL_FADE_STATE_SLOT];
    }
    if (fade <= 0.01f || fade > 1.0f) {
      fade = 0.82f;
    }
    return fade;
  }

  template <typename CanvasT>
  void renderPixelProgram(const VuLayerProgram& layer, uint8_t layerIndex, const AudioAnalysisFrame& audio, CanvasT& targetCanvas) {
    const uint32_t nowMs = millis();

    if (layer.frameProgramLength > 0) {
      float ignoredR = 0.0f;
      float ignoredG = 0.0f;
      float ignoredB = 0.0f;
      runVm(
        layer,
        layerIndex,
        audio,
        layer.bytes + layer.frameProgramOffset,
        layer.frameProgramLength,
        0,
        0,
        nowMs,
        ignoredR,
        ignoredG,
        ignoredB
      );
    }

    const uint8_t* program = layer.bytes + layer.pixelProgramOffset;
    const uint16_t length = layer.pixelProgramLength;

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        runVm(layer, layerIndex, audio, program, length, x, y, nowMs, r, g, b);
        if (r > 0.0f || g > 0.0f || b > 0.0f) {
          targetCanvas.addPixel(visualizerLedIndexXY(x, y), r * 255.0f, g * 255.0f, b * 255.0f);
        }
      }
    }
  }

  void runVm(
    const VuLayerProgram& layer,
    uint8_t layerIndex,
    const AudioAnalysisFrame& audio,
    const uint8_t* program,
    uint16_t length,
    uint16_t x,
    uint16_t y,
    uint32_t nowMs,
    float& outR,
    float& outG,
    float& outB
  ) {
    VmStack stack;
    uint16_t pc = 0;
    uint16_t guard = 0;

    while (pc < length && guard++ < 768) {
      const uint8_t op = program[pc++];

      if (op == VU_OP_END) {
        return;
      }

      if (op == VU_OP_PUSH_U8) {
        if (pc >= length) return;
        stack.push((float)program[pc++] / 255.0f);
        continue;
      }

      if (op == VU_OP_PUSH_S8) {
        if (pc >= length) return;
        stack.push((float)((int8_t)program[pc++]) / 127.0f);
        continue;
      }

      if (op == VU_OP_PUSH_FEATURE) {
        if (pc >= length) return;
        stack.push(featureValue(audio, program[pc++]));
        continue;
      }

      if (op == VU_OP_PUSH_BAND) {
        if (pc >= length) return;
        const uint8_t band = program[pc++];
        stack.push(band < AUDIO_ANALYSIS_BANDS ? clamp01(audio.bands[band]) : 0.0f);
        continue;
      }

      if (op == VU_OP_PUSH_WAVEFORM_M) {
        if (pc >= length) return;
        const uint8_t point = program[pc++];
        stack.push(point < AUDIO_ANALYSIS_WAVEFORM_POINTS ? audio.waveformM[point] : 0.0f);
        continue;
      }

      if (op == VU_OP_PUSH_X) {
        stack.push((float)x);
        continue;
      }

      if (op == VU_OP_PUSH_Y) {
        stack.push((float)y);
        continue;
      }

      if (op == VU_OP_PUSH_XN) {
        stack.push(LED_DRIVER_GRID_WIDTH > 1 ? (float)x / (float)(LED_DRIVER_GRID_WIDTH - 1) : 0.0f);
        continue;
      }

      if (op == VU_OP_PUSH_YN) {
        stack.push(LED_DRIVER_GRID_HEIGHT > 1 ? (float)y / (float)(LED_DRIVER_GRID_HEIGHT - 1) : 0.0f);
        continue;
      }

      if (op == VU_OP_PUSH_TIME) {
        stack.push((float)nowMs * 0.001f);
        continue;
      }

      if (op == VU_OP_PUSH_RANDOM) {
        stack.push(random01(x, y, layerIndex, nowMs / 33U));
        continue;
      }

      if (op == VU_OP_PUSH_STATE) {
        if (pc >= length) return;
        const uint8_t slot = program[pc++];
        stack.push((layerIndex < VU_MAX_LAYERS && slot < VU_LAYER_STATE_SLOTS) ? layerState[layerIndex][slot] : 0.0f);
        continue;
      }

      if (op == VU_OP_STORE_STATE) {
        if (pc >= length) return;
        const uint8_t slot = program[pc++];
        float value = 0.0f;
        stack.pop(value);
        if (layerIndex < VU_MAX_LAYERS && slot < VU_LAYER_STATE_SLOTS) {
          layerState[layerIndex][slot] = value;
        }
        continue;
      }

      if (op == VU_OP_SAMPLE_PALETTE) {
        float tone = 0.0f;
        stack.pop(tone);
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        samplePalette(layer.palette, tone, r, g, b);
        stack.push(r);
        stack.push(g);
        stack.push(b);
        continue;
      }

      if (op == VU_OP_EMIT_RGB) {
        float b = 0.0f;
        float g = 0.0f;
        float r = 0.0f;
        stack.pop(b);
        stack.pop(g);
        stack.pop(r);
        outR = clamp01(r);
        outG = clamp01(g);
        outB = clamp01(b);
        continue;
      }

      if (op == VU_OP_EMIT_PALETTE) {
        float level = 0.0f;
        float tone = 0.0f;
        stack.pop(level);
        stack.pop(tone);
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        samplePalette(layer.palette, tone, r, g, b);
        level = clamp01(level);
        outR = r * level;
        outG = g * level;
        outB = b * level;
        continue;
      }

      if (op == VU_OP_SCALE_RGB) {
        float scale = 0.0f;
        stack.pop(scale);
        if (stack.count >= 3) {
          stack.values[stack.count - 1] *= scale;
          stack.values[stack.count - 2] *= scale;
          stack.values[stack.count - 3] *= scale;
        }
        continue;
      }

      if (op == VU_OP_DUP) {
        if (stack.count > 0) {
          stack.push(stack.values[stack.count - 1]);
        }
        continue;
      }

      if (op == VU_OP_DROP) {
        float ignored = 0.0f;
        stack.pop(ignored);
        continue;
      }

      if (op == VU_OP_SWAP) {
        if (stack.count >= 2) {
          float& a = stack.values[stack.count - 1];
          float& b = stack.values[stack.count - 2];
          const float temp = a;
          a = b;
          b = temp;
        }
        continue;
      }

      executeMathOp(op, stack);
    }
  }

  void executeMathOp(uint8_t op, VmStack& stack) {
    float a = 0.0f;
    float b = 0.0f;

    switch (op) {
      case VU_OP_ADD:
        stack.pop(b); stack.pop(a); stack.push(a + b);
        return;
      case VU_OP_SUB:
        stack.pop(b); stack.pop(a); stack.push(a - b);
        return;
      case VU_OP_MUL:
        stack.pop(b); stack.pop(a); stack.push(a * b);
        return;
      case VU_OP_DIV:
        stack.pop(b); stack.pop(a); stack.push(fabsf(b) > 0.000001f ? a / b : 0.0f);
        return;
      case VU_OP_MIN:
        stack.pop(b); stack.pop(a); stack.push(a < b ? a : b);
        return;
      case VU_OP_MAX:
        stack.pop(b); stack.pop(a); stack.push(a > b ? a : b);
        return;
      case VU_OP_ABS:
        stack.pop(a); stack.push(fabsf(a));
        return;
      case VU_OP_NEG:
        stack.pop(a); stack.push(-a);
        return;
      case VU_OP_CLAMP01:
        stack.pop(a); stack.push(clamp01(a));
        return;
      case VU_OP_WRAP01:
        stack.pop(a); stack.push(wrap01(a));
        return;
      case VU_OP_SIN01:
        stack.pop(a); stack.push(0.5f + sinf(a * VU_TWO_PI_F) * 0.5f);
        return;
      case VU_OP_COS01:
        stack.pop(a); stack.push(0.5f + cosf(a * VU_TWO_PI_F) * 0.5f);
        return;
      case VU_OP_LESS:
        stack.pop(b); stack.pop(a); stack.push(a < b ? 1.0f : 0.0f);
        return;
      case VU_OP_GREATER:
        stack.pop(b); stack.pop(a); stack.push(a > b ? 1.0f : 0.0f);
        return;
      case VU_OP_SELECT: {
        float falseValue = 0.0f;
        float trueValue = 0.0f;
        float condition = 0.0f;
        stack.pop(falseValue);
        stack.pop(trueValue);
        stack.pop(condition);
        stack.push(condition >= 0.5f ? trueValue : falseValue);
        return;
      }
      case VU_OP_SMOOTHSTEP:
        stack.pop(a);
        a = clamp01(a);
        stack.push(a * a * (3.0f - 2.0f * a));
        return;
      case VU_OP_HYPOT:
        stack.pop(b); stack.pop(a); stack.push(sqrtf(a * a + b * b));
        return;
      default:
        return;
    }
  }

  float featureValue(const AudioAnalysisFrame& audio, uint8_t feature) const {
    switch (feature) {
      case VU_FEATURE_RMS: return clamp01(audio.rms);
      case VU_FEATURE_PEAK: return clamp01(audio.peak);
      case VU_FEATURE_LOUDNESS: return clamp01(audio.loudness);
      case VU_FEATURE_SUB_BASS: return clamp01(audio.subBass);
      case VU_FEATURE_KICK: return clamp01(audio.kick);
      case VU_FEATURE_BASS: return clamp01(audio.bass);
      case VU_FEATURE_LOW_MID: return clamp01(audio.lowMid);
      case VU_FEATURE_MID: return clamp01(audio.mid);
      case VU_FEATURE_TREBLE: return clamp01(audio.treble);
      case VU_FEATURE_TRANSIENT: return clamp01(audio.transient);
      case VU_FEATURE_BASS_TRANSIENT: return clamp01(audio.bassTransient);
      case VU_FEATURE_TREBLE_TRANSIENT: return clamp01(audio.trebleTransient);
      case VU_FEATURE_STEREO_BALANCE: return audio.stereoBalance;
      case VU_FEATURE_STEREO_WIDTH: return clamp01(audio.stereoWidth);
      case VU_FEATURE_STEREO_CORRELATION: return audio.stereoCorrelation;
      case VU_FEATURE_SPECTRAL_TILT: return clamp01(audio.spectralTilt);
      case VU_FEATURE_DOMINANT_FREQUENCY: return normalizedFrequency(audio.dominantFrequencyHz);
      case VU_FEATURE_ZERO_CROSSING: return clamp01(audio.zeroCrossing);
      case VU_FEATURE_AUDIO_FLOW_X: return audio.audioFlowX;
      case VU_FEATURE_AUDIO_FLOW_Y: return audio.audioFlowY;
      case VU_FEATURE_AUDIO_CURL: return audio.audioCurl;
      case VU_FEATURE_AUDIO_CHAOS: return clamp01(audio.audioChaos);
      case VU_FEATURE_COLOR_BASE: return wrap01(audio.colorBase);
      case VU_FEATURE_WAVEFORM_GAIN: return clamp01(audio.waveformGain / 18.0f);
      default: return 0.0f;
    }
  }

  float normalizedFrequency(float frequencyHz) const {
    if (frequencyHz <= 30.0f) {
      return 0.0f;
    }
    return clamp01(logf(frequencyHz / 30.0f) / logf(20000.0f / 30.0f));
  }

  void samplePalette(const VuPaletteProgram& palette, float tone, float& r, float& g, float& b) const {
    tone = clamp01(tone);

    if (!palette.loaded || palette.stopCount == 0) {
      float rr = 0.0f;
      float gg = 0.0f;
      float bb = 0.0f;
      visualizerSamplePalette(VISUALIZER_PALETTE_STANDARD, tone, rr, gg, bb);
      r = rr / 255.0f;
      g = gg / 255.0f;
      b = bb / 255.0f;
      return;
    }

    const uint8_t position = (uint8_t)(tone * 255.0f);
    const VuPaletteStop& first = palette.stops[0];
    if (position <= first.position || palette.stopCount == 1) {
      r = (float)first.r / 255.0f;
      g = (float)first.g / 255.0f;
      b = (float)first.b / 255.0f;
      return;
    }

    for (uint8_t i = 0; i < palette.stopCount - 1; i++) {
      const VuPaletteStop& a = palette.stops[i];
      const VuPaletteStop& z = palette.stops[i + 1];
      if (position > z.position) {
        continue;
      }

      if (palette.interpolation == VU_PALETTE_STEP || z.position <= a.position) {
        r = (float)a.r / 255.0f;
        g = (float)a.g / 255.0f;
        b = (float)a.b / 255.0f;
        return;
      }

      const float mix = (float)(position - a.position) / (float)(z.position - a.position);
      r = ((float)a.r + ((float)z.r - (float)a.r) * mix) / 255.0f;
      g = ((float)a.g + ((float)z.g - (float)a.g) * mix) / 255.0f;
      b = ((float)a.b + ((float)z.b - (float)a.b) * mix) / 255.0f;
      return;
    }

    const VuPaletteStop& last = palette.stops[palette.stopCount - 1];
    r = (float)last.r / 255.0f;
    g = (float)last.g / 255.0f;
    b = (float)last.b / 255.0f;
  }

  float random01(uint16_t x, uint16_t y, uint8_t layerIndex, uint32_t frameIndex) const {
    uint32_t value = 0x9E3779B9UL;
    value ^= (uint32_t)x * 0x85EBCA6BUL;
    value ^= (uint32_t)y * 0xC2B2AE35UL;
    value ^= (uint32_t)layerIndex * 0x27D4EB2DUL;
    value ^= frameIndex * 0x165667B1UL;
    value ^= value >> 16;
    value *= 0x7FEB352DUL;
    value ^= value >> 15;
    return (float)(value & 0x00FFFFFFUL) / 16777215.0f;
  }
};
