#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t SPARK_PARTICLE_MAX_PARTICLES = 72;
static constexpr float SPARK_PARTICLE_TRAIL_FADE = 0.62f;
static constexpr float SPARK_PARTICLE_HIT_THRESHOLD = 0.38f;
static constexpr uint16_t SPARK_PARTICLE_BASS_BURST_MS = 52;

class SparkParticleEffect : public VisualizerLayerEffect {
public:
  const char* name() const override {
    return "effect-2-sparkles";
  }

  void begin() override {
    reset();
  }

  void reset() override {
    trail.clear();

    for (uint8_t i = 0; i < SPARK_PARTICLE_MAX_PARTICLES; i++) {
      particles[i].active = false;
    }

    bassEnergy = 0.0f;
    kickEnergy = 0.0f;
    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    motionEnergy = 0.0f;
    stereoBalance = 0.0f;
    spectralTilt = 0.5f;
    audioFlowX = 0.0f;
    audioFlowY = 0.0f;
    audioCurl = 0.0f;
    audioChaos = 0.0f;
    colorBase = 0.0f;
    emitterXNorm = 0.5f;
    emitterYNorm = 0.50f;
    pendingBassBurst = 0.0f;
    pendingTrebleSpark = 0.0f;
    previousKick = 0.0f;
    kickRise = 0.0f;
    lastBassBurstMs = 0;
    lastAudioSequence = 0;
    rngState = 0xA73C9E2DUL;
  }

  void render(VisualizerCanvas& canvas, const AudioAnalysisFrame& audio, VisualizerPaletteId palette) override {
    applyAudio(audio);
    fadeCanvas();

    uint32_t now = millis();
    spawnFromAudio(now);
    updateParticles(palette);

    canvas.blendAdd(trail);
    pendingBassBurst *= 0.62f;
    pendingTrebleSpark *= 0.70f;
  }

private:
  struct Particle {
    bool active;
    float x;
    float y;
    float vx;
    float vy;
    float life;
    float maxLife;
    float tone;
    float toneVelocity;
    float energy;
    float size;
    float curl;
    float turbulence;
  };

  Particle particles[SPARK_PARTICLE_MAX_PARTICLES];
  VisualizerCanvas trail;
  float bassEnergy = 0.0f;
  float kickEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float motionEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float spectralTilt = 0.5f;
  float audioFlowX = 0.0f;
  float audioFlowY = 0.0f;
  float audioCurl = 0.0f;
  float audioChaos = 0.0f;
  float colorBase = 0.0f;
  float emitterXNorm = 0.5f;
  float emitterYNorm = 0.50f;
  float pendingBassBurst = 0.0f;
  float pendingTrebleSpark = 0.0f;
  float previousKick = 0.0f;
  float kickRise = 0.0f;
  uint32_t lastBassBurstMs = 0;
  uint32_t lastAudioSequence = 0;
  uint32_t rngState = 0xA73C9E2DUL;

  void applyAudio(const AudioAnalysisFrame& audio) {
    if (!audio.ready || audio.sequence == lastAudioSequence) {
      return;
    }

    lastAudioSequence = audio.sequence;
    rngState ^= audio.audioSeed + 0x9E3779B9UL + (rngState << 6) + (rngState >> 2);

    float previousTrebleEnergy = trebleEnergy;
    float kick = clamp01(audio.kick);
    float kickDelta = kick - previousKick;
    previousKick = kick;
    kickRise = smoothToward(kickRise, kickDelta > 0.0f ? kickDelta : 0.0f, 0.58f);

    bassEnergy = smoothToward(bassEnergy, audio.bass, 0.34f);
    kickEnergy = smoothToward(kickEnergy, kick, 0.42f);
    trebleEnergy = smoothToward(trebleEnergy, audio.treble, 0.36f);
    loudEnergy = smoothToward(loudEnergy, audio.loudness, 0.30f);
    motionEnergy = smoothToward(
      motionEnergy,
      clamp01(audio.bassTransient * 0.30f + audio.trebleTransient * 0.24f + audio.loudness * 0.16f + audio.audioChaos * 0.24f + kickRise * 0.80f),
      0.34f
    );
    stereoBalance = smoothToward(stereoBalance, audio.stereoBalance, 0.24f);
    spectralTilt = smoothToward(spectralTilt, audio.spectralTilt, 0.28f);
    audioFlowX = smoothToward(audioFlowX, audio.audioFlowX, 0.36f);
    audioFlowY = smoothToward(audioFlowY, audio.audioFlowY, 0.36f);
    audioCurl = smoothToward(audioCurl, audio.audioCurl, 0.30f);
    audioChaos = smoothToward(audioChaos, audio.audioChaos, 0.34f);
    float colorTarget = wrap01(spectralTilt * 0.52f + stereoBalance * 0.10f + audioCurl * 0.12f + audioChaos * 0.05f);
    colorBase = wrap01(colorBase * 0.88f + colorTarget * 0.12f + audio.trebleTransient * 0.010f + audio.bassTransient * 0.004f);

    float bassHit = clamp01(kickRise * 1.72f + audio.bassTransient * (0.52f + kick * 0.34f) + audio.subBass * audio.bassTransient * 0.22f + audio.transient * 0.10f);
    if (bassHit > pendingBassBurst) {
      pendingBassBurst = bassHit;
    }

    float trebleRise = audio.treble - previousTrebleEnergy;
    if (trebleRise < 0.0f) {
      trebleRise = 0.0f;
    }
    float trebleSpark = clamp01(audio.trebleTransient * 0.92f + trebleRise * 0.70f + audio.transient * 0.18f + audio.audioChaos * 0.10f - 0.10f);
    if (trebleSpark > pendingTrebleSpark) {
      pendingTrebleSpark = trebleSpark;
    }

    float xTarget = clamp01(0.5f + stereoBalance * 0.46f + audioFlowX * 0.11f + (trebleEnergy - bassEnergy) * 0.05f);
    float yTarget = clamp01(0.22f + spectralTilt * 0.56f + loudEnergy * 0.10f + audioFlowY * 0.08f);
    emitterXNorm = smoothToward(emitterXNorm, xTarget, 0.30f);
    emitterYNorm = smoothToward(emitterYNorm, yTarget, 0.30f);
  }

  float smoothToward(float current, float target, float amount) const {
    return current * (1.0f - amount) + target * amount;
  }

  void fadeCanvas() {
    trail.fade(SPARK_PARTICLE_TRAIL_FADE, 0.06f);
  }

  void spawnFromAudio(uint32_t now) {
    if (pendingBassBurst >= SPARK_PARTICLE_HIT_THRESHOLD && now - lastBassBurstMs >= SPARK_PARTICLE_BASS_BURST_MS) {
      uint8_t count = 2 + (uint8_t)(pendingBassBurst * 5.0f) + (uint8_t)(kickEnergy * 2.0f) + (uint8_t)(audioChaos * 2.0f);
      if (count > 10) {
        count = 10;
      }

      for (uint8_t i = 0; i < count; i++) {
        spawnParticle(pendingBassBurst, true);
      }

      lastBassBurstMs = now;
      pendingBassBurst = 0.0f;
    }

    float trebleChance = pendingTrebleSpark * (0.42f + trebleEnergy * 0.58f + audioChaos * 0.42f);
    uint8_t sparkleCount = 0;
    if (trebleChance > 0.18f && nextRandomFloat() < trebleChance) {
      sparkleCount = 1;
      if (trebleChance > 0.78f && nextRandomFloat() < trebleChance - 0.38f) {
        sparkleCount = 2;
      }
      if (audioChaos > 0.74f && nextRandomFloat() < audioChaos - 0.42f) {
        sparkleCount++;
      }
    }

    for (uint8_t i = 0; i < sparkleCount; i++) {
      spawnParticle(clamp01(0.35f + pendingTrebleSpark * 0.65f), false);
    }
  }

  void updateParticles(VisualizerPaletteId palette) {
    float driftX = audioFlowX * (0.014f + motionEnergy * 0.032f) + stereoBalance * 0.010f;
    float driftY = audioFlowY * (0.014f + motionEnergy * 0.032f) + (spectralTilt - 0.5f) * 0.010f;
    float drag = 0.986f - motionEnergy * 0.014f - audioChaos * 0.010f;
    if (drag < 0.950f) {
      drag = 0.950f;
    }

    for (uint8_t i = 0; i < SPARK_PARTICLE_MAX_PARTICLES; i++) {
      Particle& p = particles[i];
      if (!p.active) {
        continue;
      }

      p.x += p.vx;
      p.y += p.vy;
      float oldVx = p.vx;
      float oldVy = p.vy;
      float swirl = p.curl * (0.010f + audioChaos * 0.026f + trebleEnergy * 0.012f);
      float wiggle = p.turbulence * (0.006f + trebleEnergy * 0.022f + audioChaos * 0.018f);
      p.vx += driftX - oldVy * swirl + sinf(p.life * 0.31f + p.tone * VISUALIZER_TWO_PI_F) * wiggle;
      p.vy += driftY + oldVx * swirl + cosf(p.life * 0.27f + p.tone * VISUALIZER_TWO_PI_F) * wiggle;
      p.vx *= drag;
      p.vy *= drag;
      p.life += 1.0f;
      p.tone = wrap01(p.tone + p.toneVelocity + audioCurl * 0.0025f + trebleEnergy * 0.0015f);

      if (p.x < -1.0f || p.x > (float)LED_DRIVER_GRID_WIDTH || p.y < -1.0f || p.y > (float)LED_DRIVER_GRID_HEIGHT || p.life >= p.maxLife) {
        p.active = false;
        continue;
      }

      float life = 1.0f - p.life / p.maxLife;
      float level = p.energy * life * (0.72f + loudEnergy * 0.45f);
      if (level <= 0.012f) {
        continue;
      }

      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      float tone = wrap01(p.tone + colorBase * 0.10f + trebleEnergy * 0.06f + life * p.turbulence * 0.05f);
      visualizerSamplePalette(palette, tone, r, g, b);
      float glow = 0.18f + clamp01(level) * 0.92f;
      float whiteBoost = clamp01(trebleEnergy * life * (0.12f + audioChaos * 0.12f)) * 255.0f;
      r = r * glow + whiteBoost;
      g = g * glow + whiteBoost;
      b = b * glow + whiteBoost;
      drawParticle(p.x, p.y, p.size, r, g, b);
    }
  }

  void spawnParticle(float strength, bool bassBurst) {
    Particle& p = particles[chooseParticleSlot()];
    float width = (float)(LED_DRIVER_GRID_WIDTH - 1);
    float height = (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float xNorm = 0.5f;
    float yNorm = 0.5f;
    pickSpawnPosition(bassBurst, xNorm, yNorm);

    p.x = xNorm * width;
    p.y = yNorm * height;

    float randomDirection = nextRandomFloat() * VISUALIZER_TWO_PI_F;
    float randomX = cosf(randomDirection);
    float randomY = sinf(randomDirection);
    float flowMag = sqrtf(audioFlowX * audioFlowX + audioFlowY * audioFlowY);
    float flowX = 0.0f;
    float flowY = 0.0f;
    if (flowMag > 0.001f) {
      flowX = audioFlowX / flowMag;
      flowY = audioFlowY / flowMag;
    }
    float flowMix = clamp01(0.18f + flowMag * 0.55f + audioChaos * 0.26f + (bassBurst ? bassEnergy * 0.12f : trebleEnergy * 0.16f));
    float tangentX = -flowY * audioCurl;
    float tangentY = flowX * audioCurl;

    float speed = bassBurst
      ? (0.46f + strength * 0.70f + kickEnergy * 0.22f + loudEnergy * 0.20f + audioChaos * 0.28f)
      : (0.62f + strength * 0.70f + trebleEnergy * 0.35f + audioChaos * 0.40f);

    float outwardX = xNorm - 0.5f;
    float outwardY = yNorm - 0.5f;
    float outwardLength = sqrtf(outwardX * outwardX + outwardY * outwardY);
    if (outwardLength > 0.001f) {
      outwardX /= outwardLength;
      outwardY /= outwardLength;
    }

    float vx = randomX * (1.0f - flowMix) + flowX * flowMix + tangentX * 0.38f + outwardX * strength * 0.20f;
    float vy = randomY * (1.0f - flowMix) + flowY * flowMix + tangentY * 0.38f + outwardY * strength * 0.16f;
    float velocityLength = sqrtf(vx * vx + vy * vy);
    if (velocityLength < 0.001f) {
      vx = randomX;
      vy = randomY;
    } else {
      vx /= velocityLength;
      vy /= velocityLength;
    }

    p.vx = vx * speed + stereoBalance * 0.16f;
    p.vy = vy * speed + (spectralTilt - 0.5f) * 0.14f;
    p.life = 0.0f;
    p.maxLife = bassBurst
      ? (24.0f + strength * 36.0f + bassEnergy * 12.0f)
      : (13.0f + strength * 18.0f + trebleEnergy * 10.0f);
    p.energy = bassBurst
      ? (0.50f + strength * 0.78f)
      : (0.36f + strength * 0.68f);
    p.size = bassBurst
      ? (0.18f + bassEnergy * 0.24f)
      : (0.06f + trebleEnergy * 0.18f);
    p.tone = chooseParticleTone(strength, bassBurst);
    p.toneVelocity = (nextRandomFloat() - 0.5f) * (0.006f + trebleEnergy * 0.020f + audioChaos * 0.024f) + audioCurl * 0.006f;
    p.curl = audioCurl * 0.75f + (nextRandomFloat() - 0.5f) * (0.55f + audioChaos * 0.90f);
    p.turbulence = clamp01(0.18f + nextRandomFloat() * 0.70f + audioChaos * 0.42f + trebleEnergy * 0.18f);
    p.active = true;
  }

  void pickSpawnPosition(bool bassBurst, float& xNorm, float& yNorm) {
    float roll = nextRandomFloat();

    if (bassBurst && roll < 0.24f) {
      float jitterX = 0.22f + motionEnergy * 0.20f;
      float jitterY = 0.22f + trebleEnergy * 0.18f;
      xNorm = emitterXNorm + (nextRandomFloat() - 0.5f) * jitterX;
      yNorm = emitterYNorm + (nextRandomFloat() - 0.5f) * jitterY;
    } else if (!bassBurst && roll < 0.30f) {
      float jitterX = 0.34f + trebleEnergy * 0.22f;
      float jitterY = 0.30f + trebleEnergy * 0.18f;
      xNorm = emitterXNorm + (nextRandomFloat() - 0.5f) * jitterX;
      yNorm = emitterYNorm + (nextRandomFloat() - 0.5f) * jitterY;
    } else if (roll > 0.70f) {
      uint8_t edge = nextRandomU32() & 0x03;
      float inset = 0.03f + nextRandomFloat() * 0.10f;

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
      xNorm = nextRandomFloat();
      yNorm = nextRandomFloat();
    }

    xNorm = clamp01(xNorm);
    yNorm = clamp01(yNorm);
  }

  float chooseParticleTone(float strength, bool bassBurst) {
    float spread = bassBurst
      ? (0.34f + audioChaos * 0.34f + bassEnergy * 0.10f)
      : (0.46f + audioChaos * 0.44f + trebleEnergy * 0.16f);
    float audioPush = spectralTilt * 0.18f + stereoBalance * 0.06f + audioCurl * 0.12f + strength * 0.05f;
    return wrap01(colorBase + audioPush + (nextRandomFloat() - 0.5f) * spread);
  }

  void drawParticle(float x, float y, float size, float r, float g, float b) {
    int16_t centerX = (int16_t)roundf(x);
    int16_t centerY = (int16_t)roundf(y);

    addPixelSafe(centerX, centerY, r, g, b);

    float sideR = r * size * 0.13f;
    float sideG = g * size * 0.13f;
    float sideB = b * size * 0.13f;

    if (size > 0.30f) {
      addPixelSafe(centerX - 1, centerY, sideR, sideG, sideB);
      addPixelSafe(centerX + 1, centerY, sideR, sideG, sideB);
    }
    if (size > 0.38f) {
      addPixelSafe(centerX, centerY - 1, sideR, sideG, sideB);
      addPixelSafe(centerX, centerY + 1, sideR, sideG, sideB);
    }
  }

  void addPixelSafe(int16_t x, int16_t y, float r, float g, float b) {
    if (x < 0 || x >= LED_DRIVER_GRID_WIDTH || y < 0 || y >= LED_DRIVER_GRID_HEIGHT) {
      return;
    }

    trail.addPixel(ledIndexXY((uint16_t)x, (uint16_t)y), r, g, b);
  }

  uint8_t chooseParticleSlot() {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < SPARK_PARTICLE_MAX_PARTICLES; i++) {
      if (!particles[i].active) {
        return i;
      }

      float score = particles[i].life / particles[i].maxLife;
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
