#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t DRIFT_RIPPLE_MAX_RIPPLES = 12;
static constexpr float DRIFT_RIPPLE_TRAIL_FADE = 0.80f;
static constexpr float DRIFT_RIPPLE_FRAME_MS = 18.0f;
static constexpr float DRIFT_RIPPLE_HIT_THRESHOLD = 0.22f;
static constexpr float DRIFT_RIPPLE_RISE_THRESHOLD = 0.065f;
static constexpr uint16_t DRIFT_RIPPLE_MIN_SPAWN_MS = 76;
static constexpr uint16_t DRIFT_RIPPLE_EXTRA_QUIET_SPAWN_MS = 90;

class DriftRippleEffect : public VisualizerLayerEffect {
public:
  const char* name() const override {
    return "effect-1-ripples";
  }

  void begin() override {
    reset();
  }

  void reset() override {
    trail.clear();

    for (uint8_t i = 0; i < DRIFT_RIPPLE_MAX_RIPPLES; i++) {
      ripples[i].active = false;
    }

    bassEnergy = 0.0f;
    subBassEnergy = 0.0f;
    kickEnergy = 0.0f;
    lowMidEnergy = 0.0f;
    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    stereoBalance = 0.0f;
    bassTransientEnergy = 0.0f;
    spectralTilt = 0.5f;
    spawnXNorm = 0.5f;
    spawnYNorm = 0.35f;
    motionEnergy = 0.0f;
    pendingBassHit = 0.0f;
    previousKick = 0.0f;
    kickRise = 0.0f;
    dominantTone = 0.5f;
    treblePhase = 0.0f;
    lastSpawnMs = 0;
    lastRenderMs = 0;
    lastAudioSequence = 0;
    rngState = 0x5A17C0DEUL;
  }

  void render(VisualizerCanvas& canvas, const AudioAnalysisFrame& audio, VisualizerPaletteId palette) override {
    uint32_t now = millis();
    float frameScale = animationFrameScale(now);

    applyAudio(audio);
    fadeCanvas(frameScale);

    maybeSpawnRipples(now);

    treblePhase += (0.16f + trebleEnergy * 0.34f) * frameScale;

    for (uint8_t i = 0; i < DRIFT_RIPPLE_MAX_RIPPLES; i++) {
      if (ripples[i].active) {
        drawRipple(ripples[i], frameScale, palette);
      }
    }

    canvas.blendAdd(trail);
    pendingBassHit *= powf(0.76f, frameScale);
  }

private:
  struct Ripple {
    bool active;
    float x;
    float y;
    float radius;
    float startRadius;
    float speed;
    float strength;
    float ringWidth;
    float distortion;
    float maxRadius;
    float maxAge;
    float age;
    float seed;
    float tone;
    float toneDrift;
    float ovalness;
    float angle;
  };

  Ripple ripples[DRIFT_RIPPLE_MAX_RIPPLES];
  VisualizerCanvas trail;
  float bassEnergy = 0.0f;
  float subBassEnergy = 0.0f;
  float kickEnergy = 0.0f;
  float lowMidEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float bassTransientEnergy = 0.0f;
  float spectralTilt = 0.5f;
  float spawnXNorm = 0.5f;
  float spawnYNorm = 0.35f;
  float motionEnergy = 0.0f;
  float pendingBassHit = 0.0f;
  float previousKick = 0.0f;
  float kickRise = 0.0f;
  float dominantTone = 0.5f;
  float treblePhase = 0.0f;
  uint32_t lastSpawnMs = 0;
  uint32_t lastRenderMs = 0;
  uint32_t lastAudioSequence = 0;
  uint32_t rngState = 0x5A17C0DEUL;

  void applyAudio(const AudioAnalysisFrame& audio) {
    if (!audio.ready || audio.sequence == lastAudioSequence) {
      return;
    }

    lastAudioSequence = audio.sequence;
    rngState ^= audio.audioSeed + 0x9E3779B9UL + (rngState << 6) + (rngState >> 2);

    float kick = clamp01(audio.kick);
    float kickDelta = kick - previousKick;
    previousKick = kick;
    kickRise = smoothToward(kickRise, kickDelta > 0.0f ? kickDelta : 0.0f, 0.62f);

    subBassEnergy = smoothToward(subBassEnergy, audio.subBass, 0.36f);
    kickEnergy = smoothToward(kickEnergy, kick, 0.46f);
    bassEnergy = smoothToward(bassEnergy, clamp01(audio.bass * 0.52f + audio.kick * 0.48f), 0.38f);
    lowMidEnergy = smoothToward(lowMidEnergy, audio.lowMid, 0.32f);
    trebleEnergy = smoothToward(trebleEnergy, audio.treble, 0.34f);
    loudEnergy = smoothToward(loudEnergy, audio.loudness, 0.30f);
    bassTransientEnergy = smoothToward(bassTransientEnergy, clamp01(audio.bassTransient * 0.62f + kickRise * 1.10f + kick * 0.16f), 0.52f);
    spectralTilt = smoothToward(spectralTilt, audio.spectralTilt, 0.28f);
    dominantTone = smoothToward(dominantTone, dominantToneFromFrequency(audio.dominantFrequencyHz), 0.24f);
    motionEnergy = smoothToward(
      motionEnergy,
      clamp01(audio.transient * 0.28f + audio.trebleTransient * 0.22f + audio.loudness * 0.14f + audio.audioChaos * 0.22f + kickRise * 0.92f),
      0.32f
    );
    stereoBalance = smoothToward(stereoBalance, audio.stereoBalance, 0.22f);

    float hit = clamp01(kickRise * 1.85f + audio.bassTransient * (0.62f + kick * 0.34f) + audio.subBass * audio.bassTransient * 0.22f + audio.transient * 0.14f);
    if (hit > pendingBassHit) {
      pendingBassHit = hit;
    }

    float xTarget = clamp01(0.5f + stereoBalance * 0.38f + audio.audioFlowX * 0.13f + (audio.treble - audio.bass) * 0.06f);
    float yTarget = clamp01(0.08f + audio.spectralTilt * 0.58f + audio.loudness * 0.12f + audio.audioFlowY * 0.10f + audio.kick * 0.10f);
    spawnXNorm = smoothToward(spawnXNorm, xTarget, 0.30f);
    spawnYNorm = smoothToward(spawnYNorm, yTarget, 0.30f);
  }

  float dominantToneFromFrequency(float frequencyHz) const {
    if (frequencyHz <= 0.0f) {
      return 0.5f;
    }
    float normalized = logf(frequencyHz / 35.0f) / logf(20000.0f / 35.0f);
    return clamp01(normalized);
  }

  float smoothToward(float current, float target, float amount) const {
    return current * (1.0f - amount) + target * amount;
  }

  float animationFrameScale(uint32_t now) {
    if (lastRenderMs == 0) {
      lastRenderMs = now;
      return 1.0f;
    }

    uint32_t elapsedMs = now - lastRenderMs;
    lastRenderMs = now;

    float scale = (float)elapsedMs / DRIFT_RIPPLE_FRAME_MS;
    if (scale < 0.65f) scale = 0.65f;
    if (scale > 1.28f) scale = 1.28f;
    return scale;
  }

  void fadeCanvas(float frameScale) {
    float fade = powf(DRIFT_RIPPLE_TRAIL_FADE, frameScale);
    trail.fade(fade, 0.12f);
  }

  void maybeSpawnRipples(uint32_t now) {
    uint16_t minSpawnMs = DRIFT_RIPPLE_MIN_SPAWN_MS
      + (uint16_t)((1.0f - clamp01(kickEnergy + bassTransientEnergy * 0.45f)) * (float)DRIFT_RIPPLE_EXTRA_QUIET_SPAWN_MS);

    bool bassHit = pendingBassHit >= DRIFT_RIPPLE_HIT_THRESHOLD || kickRise >= DRIFT_RIPPLE_RISE_THRESHOLD;
    if (!bassHit || now - lastSpawnMs < minSpawnMs) {
      return;
    }

    float strength = pendingBassHit;
    float riseStrength = clamp01(kickRise * 1.45f);
    if (riseStrength > strength) {
      strength = riseStrength;
    }
    uint8_t spawnCount = 1;
    if (strength > 0.72f && kickEnergy > 0.48f) {
      spawnCount = 2;
    }

    for (uint8_t i = 0; i < spawnCount; i++) {
      float rippleStrength = strength * (1.0f - (float)i * 0.18f);
      spawnRipple(clamp01(rippleStrength));
    }

    lastSpawnMs = now;
    pendingBassHit = 0.0f;
  }

  void spawnRipple(float strength) {
    uint8_t slot = chooseRippleSlot();
    Ripple& ripple = ripples[slot];

    float audioWeight = 0.42f + strength * 0.24f;
    float jitterX = 0.18f + trebleEnergy * 0.24f + motionEnergy * 0.18f + fabsf(stereoBalance) * 0.12f;
    float jitterY = 0.18f + lowMidEnergy * 0.18f + motionEnergy * 0.16f + spectralTilt * 0.08f;
    float roll = nextRandomFloat();
    float xNorm = spawnXNorm + (nextRandomFloat() - 0.5f) * jitterX;
    float yNorm = spawnYNorm + (nextRandomFloat() - 0.5f) * jitterY;

    if (roll < 0.22f) {
      xNorm = nextRandomFloat();
      yNorm = nextRandomFloat();
    } else if (roll < 0.42f) {
      uint8_t edge = nextRandomU32() & 0x03;
      float inset = 0.05f + nextRandomFloat() * 0.18f;
      if (edge == 0) {
        xNorm = inset;
        yNorm = nextRandomFloat();
      } else if (edge == 1) {
        xNorm = 1.0f - inset;
        yNorm = nextRandomFloat();
      } else if (edge == 2) {
        xNorm = nextRandomFloat();
        yNorm = inset;
      } else {
        xNorm = nextRandomFloat();
        yNorm = 1.0f - inset;
      }
    } else {
      xNorm = xNorm * audioWeight + nextRandomFloat() * (1.0f - audioWeight);
      yNorm = yNorm * audioWeight + nextRandomFloat() * (1.0f - audioWeight);
    }
    xNorm = clamp01(xNorm);
    yNorm = clamp01(yNorm);

    if (xNorm > 0.38f && xNorm < 0.62f && yNorm > 0.38f && yNorm < 0.62f) {
      xNorm += (xNorm < 0.5f) ? -0.23f : 0.23f;
      yNorm += (yNorm < 0.5f) ? -0.17f : 0.17f;
      xNorm = clamp01(xNorm);
      yNorm = clamp01(yNorm);
    }

    ripple.x = xNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    ripple.y = yNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    ripple.radius = 0.03f + strength * 0.14f + kickEnergy * 0.10f;
    ripple.startRadius = ripple.radius;
    ripple.speed = 0.30f + strength * 0.24f + kickEnergy * 0.18f + trebleEnergy * 0.12f + motionEnergy * 0.10f;
    ripple.strength = 0.70f + strength * 0.82f + kickEnergy * 0.22f;
    ripple.ringWidth = 0.46f + subBassEnergy * 0.18f + lowMidEnergy * 0.14f + loudEnergy * 0.08f;
    ripple.distortion = 0.02f + trebleEnergy * 0.13f + motionEnergy * 0.10f + fabsf(stereoBalance) * 0.05f;
    ripple.maxRadius = 4.2f + strength * 4.2f + subBassEnergy * 2.4f + bassEnergy * 1.8f + loudEnergy * 1.0f - spectralTilt * 0.5f;
    if (ripple.maxRadius < 4.2f) ripple.maxRadius = 4.2f;
    if (ripple.maxRadius > 13.5f) ripple.maxRadius = 13.5f;
    ripple.maxAge = (ripple.maxRadius - ripple.radius) / (ripple.speed + 0.001f) + 1.8f + subBassEnergy * 2.4f - trebleEnergy * 1.0f;
    if (ripple.maxAge < 6.0f) ripple.maxAge = 6.0f;
    ripple.age = 0.0f;
    ripple.seed = nextRandomFloat() * VISUALIZER_TWO_PI_F;
    ripple.toneDrift = (nextRandomFloat() - 0.5f) * (0.02f + trebleEnergy * 0.08f + motionEnergy * 0.06f);
    ripple.ovalness = (nextRandomFloat() - 0.5f) * (0.18f + lowMidEnergy * 0.34f + audioShapeEnergy() * 0.16f);
    ripple.angle = nextRandomFloat() * VISUALIZER_TWO_PI_F + stereoBalance * 0.9f;
    ripple.tone = chooseRippleTone(strength);
    ripple.active = true;
  }

  float audioShapeEnergy() const {
    return clamp01(trebleEnergy * 0.44f + motionEnergy * 0.36f + fabsf(stereoBalance) * 0.20f);
  }

  void drawRipple(Ripple& ripple, float frameScale, VisualizerPaletteId palette) {
    ripple.radius += ripple.speed * (0.90f + loudEnergy * 0.16f) * frameScale;
    ripple.age += frameScale;

    if (ripple.age >= ripple.maxAge || ripple.radius > ripple.maxRadius) {
      ripple.active = false;
      return;
    }

    float radiusProgress = clamp01((ripple.radius - ripple.startRadius) / (ripple.maxRadius - ripple.startRadius + 0.001f));
    float growthFade = 1.0f - radiusProgress;
    float waterFade = growthFade * growthFade * (3.0f - 2.0f * growthFade);
    float life = waterFade * (0.52f + growthFade * 0.34f);
    float startupBoost = 0.72f + growthFade * 0.20f;
    float ringWidth = ripple.ringWidth + trebleEnergy * 0.08f;
    float distortionAmount = ripple.distortion + trebleEnergy * 0.08f;
    float ca = cosf(ripple.angle);
    float sa = sinf(ripple.angle);

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float dx = (float)x - ripple.x;
        float dy = (float)y - ripple.y;
        float rx = dx * ca - dy * sa;
        float ry = dx * sa + dy * ca;
        float stretchX = 1.0f + ripple.ovalness;
        float stretchY = 1.0f - ripple.ovalness;
        if (stretchX < 0.72f) stretchX = 0.72f;
        if (stretchY < 0.72f) stretchY = 0.72f;
        float distance = sqrtf((rx / stretchX) * (rx / stretchX) + (ry / stretchY) * (ry / stretchY));
        float maxReach = ringWidth + distortionAmount * 2.0f;

        if (fabsf(distance - ripple.radius) > maxReach) {
          continue;
        }

        float distortion =
          sinf(dx * 1.35f + dy * 0.74f + treblePhase + ripple.seed) +
          sinf((dx - dy) * 1.08f - treblePhase * 1.31f + ripple.seed * 0.63f);
        float warpedDistance = distance + distortion * distortionAmount;
        float edge = 1.0f - fabsf(warpedDistance - ripple.radius) / ringWidth;

        if (edge <= 0.0f) {
          continue;
        }

        edge = edge * edge * (3.0f - 2.0f * edge);
        float level = edge * life * startupBoost * ripple.strength * (0.72f + bassEnergy * 0.22f + kickEnergy * 0.18f + motionEnergy * 0.10f);
        if (level <= 0.004f) {
          continue;
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float tone = wrap01(ripple.tone + ripple.toneDrift * ripple.age + trebleEnergy * 0.08f + edge * 0.045f + sinf(treblePhase + ripple.seed) * 0.030f);
        visualizerSamplePalette(palette, tone, r, g, b);
        float glow = 0.10f + clamp01(level) * 1.10f;
        trail.addPixel(visualizerLedIndexXY(x, y), r * glow, g * glow, b * glow);
      }
    }
  }

  float chooseRippleTone(float strength) {
    float roll = nextRandomFloat();
    float baseTone = 0.0f;

    if (roll < 0.22f) {
      baseTone = 0.02f + nextRandomFloat() * 0.14f;
    } else if (roll < 0.45f) {
      baseTone = 0.20f + nextRandomFloat() * 0.14f;
    } else if (roll < 0.68f) {
      baseTone = 0.40f + nextRandomFloat() * 0.16f;
    } else if (roll < 0.88f) {
      baseTone = 0.58f + nextRandomFloat() * 0.18f;
    } else {
      baseTone = 0.78f + nextRandomFloat() * 0.12f;
    }

    return wrap01(baseTone * 0.62f + dominantTone * 0.22f + spectralTilt * 0.16f + trebleEnergy * 0.10f + strength * 0.05f + stereoBalance * 0.04f);
  }

  uint8_t chooseRippleSlot() {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < DRIFT_RIPPLE_MAX_RIPPLES; i++) {
      if (!ripples[i].active) {
        return i;
      }

      float score = ripples[i].age / ripples[i].maxAge;
      if (score > oldestScore) {
        oldestScore = score;
        slot = i;
      }
    }

    return slot;
  }

  uint32_t nextRandomU32() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
  }

  float nextRandomFloat() {
    return (float)(nextRandomU32() & 0x00FFFFFFUL) / 16777215.0f;
  }
};
