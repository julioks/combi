#pragma once

#include <Arduino.h>

static constexpr uint8_t VU_PACKET_MAGIC_0 = 'V';
static constexpr uint8_t VU_PACKET_MAGIC_1 = 'U';
static constexpr uint8_t VU_PACKET_VERSION = 1;

static constexpr uint16_t VU_MAX_LAYERS = 4;
static constexpr uint16_t VU_MAX_PROGRAM_BYTES = 768;
static constexpr uint8_t VU_MAX_PALETTE_STOPS = 16;
static constexpr uint8_t VU_LAYER_STATE_SLOTS = 32;
static constexpr uint8_t VU_LAYER_FLAG_TRAIL = 0x01;
static constexpr uint8_t VU_TRAIL_FADE_STATE_SLOT = VU_LAYER_STATE_SLOTS - 1;

enum VuPacketKind : uint8_t {
  VU_PACKET_LAYER = 1,
  VU_PACKET_PALETTE = 2,
  VU_PACKET_CLEAR = 3
};

enum VuPaletteInterpolation : uint8_t {
  VU_PALETTE_LINEAR = 0,
  VU_PALETTE_STEP = 1
};

struct VuPaletteStop {
  uint8_t position;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct VuPaletteProgram {
  bool loaded = false;
  VuPaletteInterpolation interpolation = VU_PALETTE_LINEAR;
  uint8_t stopCount = 0;
  VuPaletteStop stops[VU_MAX_PALETTE_STOPS];
};

struct VuLayerProgram {
  bool loaded = false;
  uint8_t flags = 0;
  uint16_t frameProgramOffset = 0;
  uint16_t frameProgramLength = 0;
  uint16_t pixelProgramOffset = 0;
  uint16_t pixelProgramLength = 0;
  uint16_t byteCount = 0;
  uint8_t bytes[VU_MAX_PROGRAM_BYTES];
  VuPaletteProgram palette;
};

enum VuAudioFeatureId : uint8_t {
  VU_FEATURE_RMS = 0,
  VU_FEATURE_PEAK = 1,
  VU_FEATURE_LOUDNESS = 2,
  VU_FEATURE_SUB_BASS = 3,
  VU_FEATURE_KICK = 4,
  VU_FEATURE_BASS = 5,
  VU_FEATURE_LOW_MID = 6,
  VU_FEATURE_MID = 7,
  VU_FEATURE_TREBLE = 8,
  VU_FEATURE_TRANSIENT = 9,
  VU_FEATURE_BASS_TRANSIENT = 10,
  VU_FEATURE_TREBLE_TRANSIENT = 11,
  VU_FEATURE_STEREO_BALANCE = 12,
  VU_FEATURE_STEREO_WIDTH = 13,
  VU_FEATURE_STEREO_CORRELATION = 14,
  VU_FEATURE_SPECTRAL_TILT = 15,
  VU_FEATURE_DOMINANT_FREQUENCY = 16,
  VU_FEATURE_ZERO_CROSSING = 17,
  VU_FEATURE_AUDIO_FLOW_X = 18,
  VU_FEATURE_AUDIO_FLOW_Y = 19,
  VU_FEATURE_AUDIO_CURL = 20,
  VU_FEATURE_AUDIO_CHAOS = 21,
  VU_FEATURE_COLOR_BASE = 22,
  VU_FEATURE_WAVEFORM_GAIN = 23
};

enum VuOpcode : uint8_t {
  VU_OP_END = 0x00,
  VU_OP_PUSH_U8 = 0x01,
  VU_OP_PUSH_S8 = 0x02,
  VU_OP_PUSH_FEATURE = 0x03,
  VU_OP_PUSH_BAND = 0x04,
  VU_OP_PUSH_WAVEFORM_M = 0x05,
  VU_OP_PUSH_X = 0x06,
  VU_OP_PUSH_Y = 0x07,
  VU_OP_PUSH_XN = 0x08,
  VU_OP_PUSH_YN = 0x09,
  VU_OP_PUSH_TIME = 0x0A,
  VU_OP_PUSH_RANDOM = 0x0B,
  VU_OP_PUSH_STATE = 0x0C,
  VU_OP_STORE_STATE = 0x0D,

  VU_OP_DUP = 0x10,
  VU_OP_DROP = 0x11,
  VU_OP_SWAP = 0x12,

  VU_OP_ADD = 0x20,
  VU_OP_SUB = 0x21,
  VU_OP_MUL = 0x22,
  VU_OP_DIV = 0x23,
  VU_OP_MIN = 0x24,
  VU_OP_MAX = 0x25,
  VU_OP_ABS = 0x26,
  VU_OP_NEG = 0x27,
  VU_OP_CLAMP01 = 0x28,
  VU_OP_WRAP01 = 0x29,
  VU_OP_SIN01 = 0x2A,
  VU_OP_COS01 = 0x2B,
  VU_OP_LESS = 0x2C,
  VU_OP_GREATER = 0x2D,
  VU_OP_SELECT = 0x2E,
  VU_OP_SMOOTHSTEP = 0x2F,
  VU_OP_HYPOT = 0x30,

  VU_OP_SAMPLE_PALETTE = 0x40,
  VU_OP_EMIT_RGB = 0x41,
  VU_OP_EMIT_PALETTE = 0x42,
  VU_OP_SCALE_RGB = 0x43
};

static inline uint16_t vuReadU16(const uint8_t* bytes) {
  return ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
}
