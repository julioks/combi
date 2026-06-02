#pragma once

#include "SpectrumGridVisualizer.h"
#include "DriftRippleVisualizer.h"
#include "SparkParticleVisualizer.h"
#include "AfterglowStarsEffect.h"
#include <string.h>
#include <stdlib.h>

static constexpr uint8_t VISUALIZER_STACK_MAX_LAYERS = 8;
static constexpr float VISUALIZER_FILTER_SILENCE_DB = -96.0f;

float estimatedLedFps();

enum VisualizerEffectId : uint8_t {
  VISUALIZER_EFFECT_RIPPLES = 1,
  VISUALIZER_EFFECT_SPARKLES = 2,
  VISUALIZER_EFFECT_AFTERGLOW_STARS = 3
};

struct VisualizerFrequencyRange {
  bool enabled;
  uint16_t lowHz;
  uint16_t highHz;
};

struct VisualizerLayer {
  VisualizerEffectId effect;
  VisualizerPaletteId palette;
  VisualizerFrequencyRange frequency;
  uint8_t noiseFloorPercent;
  uint32_t lastFilterSequence;
  float previousFilteredLevel;
  float previousFilteredLowLevel;
  float previousFilteredHighLevel;
  float filteredTransient;
  float filteredBassTransient;
  float filteredTrebleTransient;
};

class VisualizerStack {
public:
  void begin() {
    resetAllEffects();
    layerCount = 0;
    ditherFrame = 0;
    pendingFrequency.enabled = false;
    pendingFrequency.lowHz = 0;
    pendingFrequency.highHz = 0;
    pendingNoiseFloorPercent = 0;
    frameCanvas.clear();
    layerCanvas.clear();
  }

  void clearStack() {
    layerCount = 0;
    pendingFrequency.enabled = false;
    pendingFrequency.lowHz = 0;
    pendingFrequency.highHz = 0;
    pendingNoiseFloorPercent = 0;
    resetAllEffects();
    Serial.println("Visualizer cleared: mode 0 spectrum on p0 standard-blue-green-red.");
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) {
    frameCanvas.clear();

    if (layerCount == 0) {
      baseSpectrum.render(frameCanvas, audio, VISUALIZER_PALETTE_STANDARD);
    } else {
      for (uint8_t i = 0; i < layerCount; i++) {
        layerCanvas.clear();
        renderLayer(layers[i], audio);
        frameCanvas.blendAdd(layerCanvas);
      }
    }

    frameCanvas.writeTo(pixels, ditherFrame);
    ditherFrame++;
  }

  void handleCommandLine(char* commandLine) {
    bool handledAny = false;
    char* cursor = commandLine;

    while (cursor != nullptr && *cursor != '\0') {
      char* next = strchr(cursor, ',');
      if (next != nullptr) {
        *next = '\0';
        next++;
      }

      char* token = trim(cursor);
      if (*token != '\0') {
        handleToken(token);
        handledAny = true;
      }

      cursor = next;
    }

    if (!handledAny) {
      printHelp();
    }
  }

  void printHelp() const {
    Serial.println("Visualizer commands:");
    Serial.println("  clear        mode 0 spectrum on p0");
    Serial.println("  e1/e2/e3     append ripples/sparkles/afterglow-stars");
    Serial.println("  p0/p1/p2     set palette on latest effect");
    Serial.println("  lf100/hf250  set latest effect to 100-250 Hz");
    Serial.println("  f20/floor20  set latest effect noise floor to 20%");
    Serial.println("  list         print current stack");
    Serial.println("  brightness <0-255>");
    Serial.println("Palettes: p0 standard-blue-green-red, p1 warm-candy, p2 aurora.");
    Serial.print("LEDs: ");
    Serial.print(LED_COUNT);
    Serial.print(" estimated max FPS on one WS2812 data pin: ");
    Serial.println(estimatedLedFps(), 1);
    Serial.print("Audio analysis bands: ");
    Serial.println(AUDIO_ANALYSIS_BANDS);
  }

  void printStack() const {
    if (layerCount == 0) {
      Serial.println("Mode 0 spectrum on p0 standard-blue-green-red.");
      return;
    }

    Serial.println("Visualizer stack bottom -> top:");
    for (uint8_t i = 0; i < layerCount; i++) {
      Serial.print("  ");
      Serial.print(i);
      Serial.print(": e");
      Serial.print((uint8_t)layers[i].effect);
      Serial.print(" ");
      Serial.print(effectName(layers[i].effect));
      Serial.print(" / p");
      Serial.print((uint8_t)layers[i].palette);
      Serial.print(" ");
      Serial.print(visualizerPaletteName(layers[i].palette));
      Serial.print(" / ");
      printFrequencyRange(layers[i].frequency);
      Serial.print(" / ");
      printNoiseFloor(layers[i].noiseFloorPercent);
      Serial.println();
    }
  }

private:
  SpectrumGridEffect baseSpectrum;
  DriftRippleEffect rippleEffect;
  SparkParticleEffect sparkEffect;
  AfterglowStarsEffect starEffect;
  VisualizerCanvas frameCanvas;
  VisualizerCanvas layerCanvas;
  AudioAnalysisFrame filteredAudio;
  VisualizerLayer layers[VISUALIZER_STACK_MAX_LAYERS];
  VisualizerFrequencyRange pendingFrequency = { false, 0, 0 };
  uint8_t pendingNoiseFloorPercent = 0;
  uint8_t layerCount = 0;
  uint8_t ditherFrame = 0;

  void handleToken(const char* token) {
    if (strcmp(token, "clear") == 0) {
      clearStack();
      return;
    }

    if (strcmp(token, "list") == 0 || strcmp(token, "help") == 0) {
      printHelp();
      printStack();
      return;
    }

    uint8_t number = 0;
    if (parsePrefixedNumber(token, 'e', number)) {
      appendEffect(number);
      return;
    }

    if (parsePrefixedNumber(token, 'p', number)) {
      setLatestPalette(number);
      return;
    }

    uint16_t frequencyHz = 0;
    if (parseFrequencyToken(token, "lf", frequencyHz)) {
      setFrequencyLimit(true, frequencyHz);
      return;
    }

    if (parseFrequencyToken(token, "hf", frequencyHz)) {
      setFrequencyLimit(false, frequencyHz);
      return;
    }

    uint8_t floorPercent = 0;
    if (parseNoiseFloorToken(token, floorPercent)) {
      setNoiseFloor(floorPercent);
      return;
    }

    Serial.print("Unknown visualizer command: ");
    Serial.println(token);
    printHelp();
  }

  void appendEffect(uint8_t effectNumber) {
    VisualizerEffectId effect = VISUALIZER_EFFECT_RIPPLES;
    if (!effectFromNumber(effectNumber, effect)) {
      Serial.print("Unknown effect e");
      Serial.println(effectNumber);
      return;
    }

    if (layerCount >= VISUALIZER_STACK_MAX_LAYERS) {
      Serial.println("Visualizer stack is full; send clear before adding more layers.");
      return;
    }

    bool wasAlreadyActive = effectInStack(effect);
    layers[layerCount].effect = effect;
    layers[layerCount].palette = VISUALIZER_PALETTE_STANDARD;
    layers[layerCount].frequency = pendingFrequency;
    layers[layerCount].noiseFloorPercent = pendingNoiseFloorPercent;
    resetLayerFilterState(layers[layerCount]);
    layerCount++;
    pendingFrequency.enabled = false;
    pendingFrequency.lowHz = 0;
    pendingFrequency.highHz = 0;
    pendingNoiseFloorPercent = 0;

    if (!wasAlreadyActive) {
      resetEffect(effect);
    }

    Serial.print("Added e");
    Serial.print(effectNumber);
    Serial.print(" ");
    Serial.print(effectName(effect));
    Serial.print(" on p0 standard-blue-green-red / ");
    printFrequencyRange(layers[layerCount - 1].frequency);
    Serial.print(" / ");
    printNoiseFloor(layers[layerCount - 1].noiseFloorPercent);
    Serial.println(".");
    printStack();
  }

  void setLatestPalette(uint8_t paletteNumber) {
    if (layerCount == 0) {
      Serial.println("No effect layer to recolor yet; add e1, e2, or e3 first.");
      return;
    }

    VisualizerPaletteId palette = VISUALIZER_PALETTE_STANDARD;
    if (!visualizerPaletteFromNumber(paletteNumber, palette)) {
      Serial.print("Unknown palette p");
      Serial.println(paletteNumber);
      return;
    }

    layers[layerCount - 1].palette = palette;
    Serial.print("Layer ");
    Serial.print(layerCount - 1);
    Serial.print(" palette set to p");
    Serial.print(paletteNumber);
    Serial.print(" ");
    Serial.println(visualizerPaletteName(palette));
    printStack();
  }

  void setFrequencyLimit(bool lowLimit, uint16_t frequencyHz) {
    VisualizerFrequencyRange& frequency = editableFrequencyRange();
    frequency.enabled = true;
    if (lowLimit) {
      frequency.lowHz = frequencyHz;
    } else {
      frequency.highHz = frequencyHz;
    }
    normalizeFrequencyRange(frequency);

    if (layerCount > 0) {
      resetLayerFilterState(layers[layerCount - 1]);
      Serial.print("Layer ");
      Serial.print(layerCount - 1);
      Serial.print(" frequency set to ");
      printFrequencyRange(frequency);
      Serial.println(".");
      printStack();
    } else {
      Serial.print("Pending next effect frequency set to ");
      printFrequencyRange(frequency);
      Serial.println(".");
    }
  }

  void setNoiseFloor(uint8_t floorPercent) {
    if (layerCount > 0) {
      layers[layerCount - 1].noiseFloorPercent = floorPercent;
      resetLayerFilterState(layers[layerCount - 1]);
      Serial.print("Layer ");
      Serial.print(layerCount - 1);
      Serial.print(" noise floor set to ");
      printNoiseFloor(floorPercent);
      Serial.println(".");
      printStack();
      return;
    }

    pendingNoiseFloorPercent = floorPercent;
    Serial.print("Pending next effect noise floor set to ");
    printNoiseFloor(floorPercent);
    Serial.println(".");
  }

  void renderLayer(VisualizerLayer& layer, const AudioAnalysisFrame& audio) {
    const AudioAnalysisFrame& layerAudio = filteredAudioForLayer(layer, audio);
    switch (layer.effect) {
      case VISUALIZER_EFFECT_RIPPLES:
        rippleEffect.render(layerCanvas, layerAudio, layer.palette);
        return;
      case VISUALIZER_EFFECT_SPARKLES:
        sparkEffect.render(layerCanvas, layerAudio, layer.palette);
        return;
      case VISUALIZER_EFFECT_AFTERGLOW_STARS:
        starEffect.render(layerCanvas, layerAudio, layer.palette);
        return;
      default:
        return;
    }
  }

  void resetAllEffects() {
    baseSpectrum.begin();
    rippleEffect.begin();
    sparkEffect.begin();
    starEffect.begin();
  }

  void resetEffect(VisualizerEffectId effect) {
    switch (effect) {
      case VISUALIZER_EFFECT_RIPPLES:
        rippleEffect.reset();
        return;
      case VISUALIZER_EFFECT_SPARKLES:
        sparkEffect.reset();
        return;
      case VISUALIZER_EFFECT_AFTERGLOW_STARS:
        starEffect.reset();
        return;
      default:
        return;
    }
  }

  bool effectInStack(VisualizerEffectId effect) const {
    for (uint8_t i = 0; i < layerCount; i++) {
      if (layers[i].effect == effect) {
        return true;
      }
    }
    return false;
  }

  bool effectFromNumber(uint8_t number, VisualizerEffectId& effect) const {
    switch (number) {
      case 1:
        effect = VISUALIZER_EFFECT_RIPPLES;
        return true;
      case 2:
        effect = VISUALIZER_EFFECT_SPARKLES;
        return true;
      case 3:
        effect = VISUALIZER_EFFECT_AFTERGLOW_STARS;
        return true;
      default:
        return false;
    }
  }

  const char* effectName(VisualizerEffectId effect) const {
    switch (effect) {
      case VISUALIZER_EFFECT_RIPPLES:
        return rippleEffect.name();
      case VISUALIZER_EFFECT_SPARKLES:
        return sparkEffect.name();
      case VISUALIZER_EFFECT_AFTERGLOW_STARS:
        return starEffect.name();
      default:
        return "unknown";
    }
  }

  const AudioAnalysisFrame& filteredAudioForLayer(VisualizerLayer& layer, const AudioAnalysisFrame& audio) {
    if ((!layer.frequency.enabled && layer.noiseFloorPercent == 0) || !audio.ready) {
      return audio;
    }

    filteredAudio = audio;

    float lowHz = (float)layer.frequency.lowHz;
    float highHz = (float)layer.frequency.highHz;
    bool hasHighLimit = highHz > 0.0f;
    float noiseFloor = (float)layer.noiseFloorPercent / 100.0f;

    bool foundBand = false;
    uint16_t includedBands = 0;
    uint16_t dominantBand = 0;
    float minIncludedHz = 0.0f;
    float maxIncludedHz = 0.0f;
    float strongestLevel = 0.0f;
    float strongestDb = VISUALIZER_FILTER_SILENCE_DB;
    float energySum = 0.0f;
    float centroidSum = 0.0f;
    float centroidWeight = 0.0f;

    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      float frequency = audio.bandCenterHz[band];
      bool inRange = (!layer.frequency.enabled || frequency >= lowHz) && (!layer.frequency.enabled || !hasHighLimit || frequency <= highHz);
      if (!inRange) {
        filteredAudio.bands[band] = 0.0f;
        filteredAudio.bandPeak[band] = 0.0f;
        filteredAudio.bandDb[band] = VISUALIZER_FILTER_SILENCE_DB;
        continue;
      }

      float level = applyNoiseFloor(clamp01(audio.bands[band]), noiseFloor);
      float peak = applyNoiseFloor(clamp01(audio.bandPeak[band]), noiseFloor);
      filteredAudio.bands[band] = level;
      filteredAudio.bandPeak[band] = peak;
      filteredAudio.bandDb[band] = level > 0.0f ? audio.bandDb[band] : VISUALIZER_FILTER_SILENCE_DB;

      if (level <= 0.0f) {
        continue;
      }

      if (!foundBand) {
        minIncludedHz = frequency;
        maxIncludedHz = frequency;
        dominantBand = band;
        strongestDb = audio.bandDb[band];
        foundBand = true;
      }

      if (frequency < minIncludedHz) minIncludedHz = frequency;
      if (frequency > maxIncludedHz) maxIncludedHz = frequency;

      if (level > strongestLevel) {
        strongestLevel = level;
        strongestDb = audio.bandDb[band];
        dominantBand = band;
      }

      centroidSum += frequency * level;
      centroidWeight += level;
      energySum += level;
      includedBands++;
    }

    if (!foundBand || includedBands == 0) {
      muteFilteredAudio(filteredAudio);
      return filteredAudio;
    }

    float splitHz = sqrtf(minIncludedHz * maxIncludedHz);
    if (splitHz <= minIncludedHz || splitHz >= maxIncludedHz) {
      splitHz = (minIncludedHz + maxIncludedHz) * 0.5f;
    }

    float lowRangeLevel = 0.0f;
    float highRangeLevel = 0.0f;
    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      float frequency = audio.bandCenterHz[band];
      float level = filteredAudio.bands[band];
      if (level <= 0.0f) {
        continue;
      }

      if (frequency <= splitHz && level > lowRangeLevel) {
        lowRangeLevel = level;
      }
      if (frequency >= splitHz && level > highRangeLevel) {
        highRangeLevel = level;
      }
    }

    if (lowRangeLevel <= 0.0f) lowRangeLevel = strongestLevel;
    if (highRangeLevel <= 0.0f) highRangeLevel = strongestLevel;

    if (layer.lastFilterSequence != audio.sequence) {
      layer.filteredBassTransient = positiveDelta(lowRangeLevel, layer.previousFilteredLowLevel, 0.20f);
      layer.filteredTrebleTransient = positiveDelta(highRangeLevel, layer.previousFilteredHighLevel, 0.20f);
      layer.filteredTransient = positiveDelta(strongestLevel, layer.previousFilteredLevel, 0.20f);
      if (layer.filteredBassTransient > layer.filteredTransient) {
        layer.filteredTransient = layer.filteredBassTransient;
      }
      if (layer.filteredTrebleTransient > layer.filteredTransient) {
        layer.filteredTransient = layer.filteredTrebleTransient;
      }

      layer.previousFilteredLevel = strongestLevel;
      layer.previousFilteredLowLevel = lowRangeLevel;
      layer.previousFilteredHighLevel = highRangeLevel;
      layer.lastFilterSequence = audio.sequence;
    }

    float averageLevel = energySum / (float)includedBands;
    float rangeEnergy = clamp01(strongestLevel * 0.78f + averageLevel * 0.35f);
    float centroid = centroidWeight > 0.000001f ? centroidSum / centroidWeight : audio.bandCenterHz[dominantBand];
    float rangeScale = audio.loudness > 0.001f ? clamp01(rangeEnergy / (audio.loudness + 0.001f)) : rangeEnergy;

    filteredAudio.rms = rangeEnergy;
    filteredAudio.peak = strongestLevel;
    filteredAudio.loudness = rangeEnergy;
    filteredAudio.subBass = lowRangeLevel;
    filteredAudio.kick = strongestLevel;
    filteredAudio.bass = lowRangeLevel;
    filteredAudio.lowMid = rangeEnergy;
    filteredAudio.mid = averageLevel;
    filteredAudio.treble = highRangeLevel;
    filteredAudio.transient = layer.filteredTransient;
    filteredAudio.bassTransient = layer.filteredBassTransient;
    filteredAudio.trebleTransient = layer.filteredTrebleTransient;
    filteredAudio.spectralTilt = highRangeLevel / (lowRangeLevel + highRangeLevel + 0.0001f);
    filteredAudio.spectralCentroidHz = centroid;
    filteredAudio.dominantBand = dominantBand;
    filteredAudio.dominantFrequencyHz = audio.bandCenterHz[dominantBand];
    filteredAudio.audioFlowX = audio.audioFlowX * rangeScale;
    filteredAudio.audioFlowY = audio.audioFlowY * rangeScale;
    filteredAudio.audioCurl = audio.audioCurl * rangeScale;
    filteredAudio.audioChaos = audio.audioChaos * rangeScale;
    filteredAudio.colorBase = colorBaseFromFrequency(centroid);
    filteredAudio.audioSeed = audio.audioSeed ^ ((uint32_t)layer.frequency.lowHz << 16) ^ (uint32_t)layer.frequency.highHz;

    for (uint16_t i = 0; i < AUDIO_ANALYSIS_WAVEFORM_POINTS; i++) {
      filteredAudio.waveformL[i] = audio.waveformL[i] * rangeScale;
      filteredAudio.waveformR[i] = audio.waveformR[i] * rangeScale;
      filteredAudio.waveformM[i] = audio.waveformM[i] * rangeScale;
    }

    (void)strongestDb;
    return filteredAudio;
  }

  float applyNoiseFloor(float level, float noiseFloor) const {
    if (noiseFloor <= 0.0f) {
      return level;
    }

    if (level <= noiseFloor) {
      return 0.0f;
    }

    return clamp01((level - noiseFloor) / (1.0f - noiseFloor));
  }

  void muteFilteredAudio(AudioAnalysisFrame& audio) const {
    audio.ready = false;
    audio.rms = 0.0f;
    audio.peak = 0.0f;
    audio.loudness = 0.0f;
    audio.subBass = 0.0f;
    audio.kick = 0.0f;
    audio.bass = 0.0f;
    audio.lowMid = 0.0f;
    audio.mid = 0.0f;
    audio.treble = 0.0f;
    audio.transient = 0.0f;
    audio.bassTransient = 0.0f;
    audio.trebleTransient = 0.0f;
    audio.spectralTilt = 0.5f;
    audio.spectralCentroidHz = 0.0f;
    audio.dominantFrequencyHz = 0.0f;
    audio.dominantBand = 0;
    audio.audioFlowX = 0.0f;
    audio.audioFlowY = 0.0f;
    audio.audioCurl = 0.0f;
    audio.audioChaos = 0.0f;
    audio.colorBase = 0.5f;

    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      audio.bands[band] = 0.0f;
      audio.bandPeak[band] = 0.0f;
      audio.bandDb[band] = VISUALIZER_FILTER_SILENCE_DB;
    }
  }

  float colorBaseFromFrequency(float frequencyHz) const {
    if (frequencyHz <= 30.0f) {
      return 0.0f;
    }

    return clamp01(logf(frequencyHz / 30.0f) / logf(20000.0f / 30.0f));
  }

  float positiveDelta(float current, float previous, float scale) const {
    if (current <= previous) {
      return 0.0f;
    }
    return clamp01((current - previous) / scale);
  }

  VisualizerFrequencyRange& editableFrequencyRange() {
    if (layerCount == 0) {
      return pendingFrequency;
    }

    return layers[layerCount - 1].frequency;
  }

  void normalizeFrequencyRange(VisualizerFrequencyRange& frequency) const {
    if (frequency.highHz > 0 && frequency.lowHz > frequency.highHz) {
      uint16_t swap = frequency.lowHz;
      frequency.lowHz = frequency.highHz;
      frequency.highHz = swap;
    }
  }

  void resetLayerFilterState(VisualizerLayer& layer) {
    layer.lastFilterSequence = 0;
    layer.previousFilteredLevel = 0.0f;
    layer.previousFilteredLowLevel = 0.0f;
    layer.previousFilteredHighLevel = 0.0f;
    layer.filteredTransient = 0.0f;
    layer.filteredBassTransient = 0.0f;
    layer.filteredTrebleTransient = 0.0f;
  }

  void printFrequencyRange(const VisualizerFrequencyRange& frequency) const {
    if (!frequency.enabled) {
      Serial.print("full-audio");
      return;
    }

    Serial.print("lf");
    Serial.print(frequency.lowHz);
    Serial.print("-hf");
    if (frequency.highHz > 0) {
      Serial.print(frequency.highHz);
    } else {
      Serial.print("*");
    }
  }

  void printNoiseFloor(uint8_t floorPercent) const {
    Serial.print("f");
    Serial.print(floorPercent);
  }

  bool parseNoiseFloorToken(const char* token, uint8_t& value) const {
    const char* cursor = nullptr;

    if (token[0] == 'f' && token[1] >= '0' && token[1] <= '9') {
      cursor = token + 1;
    } else if (strncmp(token, "floor", 5) == 0) {
      cursor = token + 5;
      while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
      }
    } else {
      return false;
    }

    if (*cursor == '\0') {
      return false;
    }

    uint16_t parsed = 0;
    while (*cursor != '\0') {
      if (*cursor < '0' || *cursor > '9') {
        return false;
      }
      parsed = parsed * 10u + (uint16_t)(*cursor - '0');
      if (parsed > 100u) {
        return false;
      }
      cursor++;
    }

    value = (uint8_t)parsed;
    return true;
  }

  bool parseFrequencyToken(const char* token, const char* prefix, uint16_t& value) const {
    if (token[0] != prefix[0] || token[1] != prefix[1] || token[2] == '\0') {
      return false;
    }

    uint32_t parsed = 0;
    for (uint8_t i = 2; token[i] != '\0'; i++) {
      if (token[i] < '0' || token[i] > '9') {
        return false;
      }
      parsed = parsed * 10UL + (uint32_t)(token[i] - '0');
      if (parsed > 24000UL) {
        return false;
      }
    }

    value = (uint16_t)parsed;
    return true;
  }

  bool parsePrefixedNumber(const char* token, char prefix, uint8_t& value) const {
    if (token[0] != prefix || token[1] == '\0') {
      return false;
    }

    uint16_t parsed = 0;
    for (uint8_t i = 1; token[i] != '\0'; i++) {
      if (token[i] < '0' || token[i] > '9') {
        return false;
      }
      parsed = parsed * 10u + (uint16_t)(token[i] - '0');
      if (parsed > 255u) {
        return false;
      }
    }

    value = (uint8_t)parsed;
    return true;
  }

  char* trim(char* value) const {
    while (*value == ' ' || *value == '\t') {
      value++;
    }

    char* end = value + strlen(value);
    while (end > value && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
      *(--end) = '\0';
    }

    return value;
  }
};
