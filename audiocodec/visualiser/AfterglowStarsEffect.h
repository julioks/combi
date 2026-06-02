#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t AFTERGLOW_STAR_MAX_GLINTS = 42;
static constexpr float AFTERGLOW_STAR_FRAME_MS = 18.0f;
static constexpr uint16_t AFTERGLOW_STAR_MIN_GLINT_MS = 42;

class AfterglowStarsEffect : public VisualizerLayerEffect {
public:
  const char* name() const override {
    return "effect-3-afterglow-stars";
  }

  void reset() override {
    for (uint8_t i = 0; i < AFTERGLOW_STAR_MAX_GLINTS; i++) {
      glints[i].active = false;
    }

    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    motionEnergy = 0.0f;
    stereoBalance = 0.0f;
    spectralTilt = 0.5f;
    audioFlowX = 0.0f;
    audioFlowY = 0.0f;
    audioCurl = 0.0f;
    audioChaos = 0.0f;
    colorBase = 0.50f;
    paletteDrift = 0.0f;
    pendingTrebleGlint = 0.0f;
    previousKick = 0.0f;
    kickRise = 0.0f;
    lastGlintMs = 0;
    lastRenderMs = 0;
    lastAudioSequence = 0;
    rngState = 0xAF734B11UL;
  }

  void render(VisualizerCanvas& canvas, const AudioAnalysisFrame& audio, VisualizerPaletteId palette) override {
    uint32_t now = millis();
    float frameScale = animationFrameScale(now);

    applyAudio(audio);
    paletteDrift = wrap01(paletteDrift + (0.0013f + trebleEnergy * 0.0030f + audioChaos * 0.0025f) * frameScale);

    spawnGlints(now);
    updateGlints(canvas, frameScale, palette);

    pendingTrebleGlint *= powf(0.72f, frameScale);
  }

private:
  struct Glint {
    bool active;
    float x;
    float y;
    float vx;
    float vy;
    float life;
    float maxLife;
    float brightness;
    float tone;
    float size;
  };

  Glint glints[AFTERGLOW_STAR_MAX_GLINTS];

  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float motionEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float spectralTilt = 0.5f;
  float audioFlowX = 0.0f;
  float audioFlowY = 0.0f;
  float audioCurl = 0.0f;
  float audioChaos = 0.0f;
  float colorBase = 0.50f;
  float paletteDrift = 0.0f;
  float pendingTrebleGlint = 0.0f;
  float previousKick = 0.0f;
  float kickRise = 0.0f;

  uint32_t lastGlintMs = 0;
  uint32_t lastRenderMs = 0;
  uint32_t lastAudioSequence = 0;
  uint32_t rngState = 0xAF734B11UL;

  void applyAudio(const AudioAnalysisFrame& audio) {
    if (!audio.ready) {
      settleTowardSilence();
      return;
    }

    if (audio.sequence == lastAudioSequence) {
      return;
    }

    lastAudioSequence = audio.sequence;
    rngState ^= audio.audioSeed + 0x9E3779B9UL + (rngState << 6) + (rngState >> 2);

    float previousTrebleEnergy = trebleEnergy;
    float kick = clamp01(audio.kick);
    float kickDelta = kick - previousKick;
    previousKick = kick;
    kickRise = smoothAttackRelease(kickRise, kickDelta > 0.0f ? kickDelta : 0.0f, 0.82f, 0.30f);

    trebleEnergy = smoothAttackRelease(trebleEnergy, audio.treble, 0.62f, 0.22f);
    loudEnergy = smoothAttackRelease(loudEnergy, audio.loudness, 0.64f, 0.18f);
    motionEnergy = smoothAttackRelease(
      motionEnergy,
      clamp01(audio.transient * 0.28f + audio.trebleTransient * 0.28f + audio.audioChaos * 0.24f + kickRise * 0.46f + audio.loudness * 0.10f),
      0.72f,
      0.22f
    );
    stereoBalance = smoothToward(stereoBalance, audio.stereoBalance, 0.22f);
    spectralTilt = smoothToward(spectralTilt, audio.spectralTilt, 0.26f);
    audioFlowX = smoothToward(audioFlowX, audio.audioFlowX, 0.34f);
    audioFlowY = smoothToward(audioFlowY, audio.audioFlowY, 0.34f);
    audioCurl = smoothToward(audioCurl, audio.audioCurl, 0.26f);
    audioChaos = smoothToward(audioChaos, audio.audioChaos, 0.32f);

    float brightness = clamp01(trebleEnergy * 0.44f + spectralTilt * 0.34f + audio.spectralCentroidHz / 18000.0f * 0.18f);
    float colorTarget = wrap01(audio.colorBase * 0.18f + brightness * 0.42f + audioCurl * 0.10f + stereoBalance * 0.05f);
    colorBase = smoothTone(colorBase, colorTarget, 0.18f + audio.transient * 0.06f + audio.trebleTransient * 0.04f);

    float trebleRise = audio.treble - previousTrebleEnergy;
    if (trebleRise < 0.0f) {
      trebleRise = 0.0f;
    }

    float glintHit = clamp01(audio.trebleTransient * 0.96f + audio.transient * 0.24f + trebleRise * 0.92f + audio.audioChaos * 0.12f - 0.08f);
    if (glintHit > pendingTrebleGlint) {
      pendingTrebleGlint = glintHit;
    }
  }

  void settleTowardSilence() {
    trebleEnergy = smoothToward(trebleEnergy, 0.0f, 0.06f);
    loudEnergy = smoothToward(loudEnergy, 0.0f, 0.05f);
    motionEnergy = smoothToward(motionEnergy, 0.0f, 0.06f);
    pendingTrebleGlint *= 0.76f;
  }

  float animationFrameScale(uint32_t now) {
    if (lastRenderMs == 0) {
      lastRenderMs = now;
      return 1.0f;
    }

    uint32_t elapsedMs = now - lastRenderMs;
    lastRenderMs = now;

    float scale = (float)elapsedMs / AFTERGLOW_STAR_FRAME_MS;
    if (scale < 0.62f) scale = 0.62f;
    if (scale > 1.35f) scale = 1.35f;
    return scale;
  }

  void spawnGlints(uint32_t now) {
    if (pendingTrebleGlint < 0.18f || now - lastGlintMs < AFTERGLOW_STAR_MIN_GLINT_MS) {
      return;
    }

    uint8_t count = 1;
    if (pendingTrebleGlint > 0.42f) count++;
    if (pendingTrebleGlint > 0.68f && audioChaos > 0.18f) count++;
    if (pendingTrebleGlint > 0.90f && nextRandomFloat() < pendingTrebleGlint) count++;

    for (uint8_t i = 0; i < count; i++) {
      spawnGlint(pendingTrebleGlint);
    }

    lastGlintMs = now;
    pendingTrebleGlint *= 0.42f;
  }

  void spawnGlint(float strength) {
    Glint& glint = glints[chooseGlintSlot()];
    float xNorm = nextRandomFloat();
    float yNorm = nextRandomFloat();
    float roll = nextRandomFloat();

    if (roll < 0.34f) {
      xNorm = clamp01(0.5f + stereoBalance * 0.42f + audioFlowX * 0.12f + (nextRandomFloat() - 0.5f) * (0.45f + trebleEnergy * 0.25f));
      yNorm = clamp01(0.52f + spectralTilt * 0.28f + audioFlowY * 0.10f + (nextRandomFloat() - 0.5f) * 0.34f);
    } else if (roll > 0.74f) {
      uint8_t edge = nextRandomU32() & 0x03;
      if (edge == 0) {
        xNorm = nextRandomFloat();
        yNorm = 0.02f + nextRandomFloat() * 0.10f;
      } else if (edge == 1) {
        xNorm = nextRandomFloat();
        yNorm = 0.88f + nextRandomFloat() * 0.10f;
      } else if (edge == 2) {
        xNorm = 0.02f + nextRandomFloat() * 0.10f;
        yNorm = nextRandomFloat();
      } else {
        xNorm = 0.88f + nextRandomFloat() * 0.10f;
        yNorm = nextRandomFloat();
      }
    }

    glint.x = xNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    glint.y = yNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    glint.vx = (nextRandomFloat() - 0.5f) * (0.10f + audioChaos * 0.18f) + audioFlowX * 0.05f + stereoBalance * 0.025f;
    glint.vy = (nextRandomFloat() - 0.5f) * (0.08f + audioChaos * 0.15f) + audioFlowY * 0.05f + (spectralTilt - 0.5f) * 0.030f;
    glint.life = 0.0f;
    glint.maxLife = 4.0f + strength * 6.5f + trebleEnergy * 3.8f;
    glint.brightness = 0.34f + strength * 0.48f + trebleEnergy * 0.16f;
    glint.tone = wrap01(colorBase + 0.62f + nextRandomFloat() * 0.22f + trebleEnergy * 0.08f);
    glint.size = 0.50f + strength * 0.62f + audioChaos * 0.35f;
    glint.active = true;
  }

  void updateGlints(VisualizerCanvas& canvas, float frameScale, VisualizerPaletteId palette) {
    for (uint8_t i = 0; i < AFTERGLOW_STAR_MAX_GLINTS; i++) {
      Glint& glint = glints[i];
      if (!glint.active) {
        continue;
      }

      glint.x += glint.vx * frameScale;
      glint.y += glint.vy * frameScale;
      glint.vx += (-glint.vy * audioCurl * 0.014f + audioFlowX * 0.006f) * frameScale;
      glint.vy += (glint.vx * audioCurl * 0.014f + audioFlowY * 0.006f) * frameScale;
      glint.life += frameScale;

      if (glint.life >= glint.maxLife || glint.x < -1.0f || glint.x > (float)LED_DRIVER_GRID_WIDTH || glint.y < -1.0f || glint.y > (float)LED_DRIVER_GRID_HEIGHT) {
        glint.active = false;
        continue;
      }

      float life = 1.0f - glint.life / glint.maxLife;
      float pop = life * life * (3.0f - 2.0f * life);
      float level = glint.brightness * pop * (0.46f + trebleEnergy * 0.30f + pendingTrebleGlint * 0.12f);

      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      visualizerSamplePalette(palette, wrap01(glint.tone + paletteDrift + glint.life * 0.010f), r, g, b);
      float white = clamp01(level * (0.16f + trebleEnergy * 0.16f)) * 58.0f;
      drawGlint(canvas, glint.x, glint.y, glint.size, r * level + white, g * level + white, b * level + white);
    }
  }

  void drawGlint(VisualizerCanvas& canvas, float x, float y, float size, float r, float g, float b) {
    int16_t centerX = (int16_t)roundf(x);
    int16_t centerY = (int16_t)roundf(y);

    canvas.addPixelSafe(centerX, centerY, r, g, b);

    float side = 0.22f + size * 0.12f;
    canvas.addPixelSafe(centerX - 1, centerY, r * side, g * side, b * side);
    canvas.addPixelSafe(centerX + 1, centerY, r * side, g * side, b * side);
    canvas.addPixelSafe(centerX, centerY - 1, r * side, g * side, b * side);
    canvas.addPixelSafe(centerX, centerY + 1, r * side, g * side, b * side);

    if (size > 0.90f) {
      float far = side * 0.32f;
      canvas.addPixelSafe(centerX - 2, centerY, r * far, g * far, b * far);
      canvas.addPixelSafe(centerX + 2, centerY, r * far, g * far, b * far);
      canvas.addPixelSafe(centerX, centerY - 2, r * far, g * far, b * far);
      canvas.addPixelSafe(centerX, centerY + 2, r * far, g * far, b * far);
    }
  }

  uint8_t chooseGlintSlot() const {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < AFTERGLOW_STAR_MAX_GLINTS; i++) {
      if (!glints[i].active) {
        return i;
      }

      float score = glints[i].life / glints[i].maxLife;
      if (score > oldestScore) {
        oldestScore = score;
        slot = i;
      }
    }

    return slot;
  }

  float smoothToward(float current, float target, float amount) const {
    return current * (1.0f - amount) + target * amount;
  }

  float smoothAttackRelease(float current, float target, float attack, float release) const {
    return smoothToward(current, target, target > current ? attack : release);
  }

  float smoothTone(float current, float target, float amount) const {
    float delta = target - current;
    if (delta > 0.5f) {
      delta -= 1.0f;
    } else if (delta < -0.5f) {
      delta += 1.0f;
    }

    return wrap01(current + delta * amount);
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
