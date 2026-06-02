#pragma once

#include <Arduino.h>
#include <string.h>

static constexpr uint16_t AUDIO_ANALYSIS_BANDS = 128;
static constexpr uint16_t AUDIO_ANALYSIS_WAVEFORM_POINTS = 128;

struct AudioAnalysisFrame {
  bool ready;
  uint32_t sequence;
  uint32_t sampleCounter;
  uint32_t generatedAtMs;
  uint32_t audioSeed;

  float bands[AUDIO_ANALYSIS_BANDS];
  float bandDb[AUDIO_ANALYSIS_BANDS];
  float bandPeak[AUDIO_ANALYSIS_BANDS];
  float bandCenterHz[AUDIO_ANALYSIS_BANDS];

  float rms;
  float peak;
  float loudness;
  float subBass;
  float kick;
  float bass;
  float lowMid;
  float mid;
  float treble;
  float transient;
  float bassTransient;
  float trebleTransient;
  float stereoBalance;
  float stereoWidth;
  float stereoCorrelation;
  float spectralTilt;
  float spectralCentroidHz;
  float dominantFrequencyHz;
  uint16_t dominantBand;
  float zeroCrossing;
  float audioFlowX;
  float audioFlowY;
  float audioCurl;
  float audioChaos;
  float colorBase;
  float waveformGain;

  float waveformL[AUDIO_ANALYSIS_WAVEFORM_POINTS];
  float waveformR[AUDIO_ANALYSIS_WAVEFORM_POINTS];
  float waveformM[AUDIO_ANALYSIS_WAVEFORM_POINTS];
};

static inline void clearAudioAnalysisFrame(AudioAnalysisFrame& frame) {
  memset(&frame, 0, sizeof(frame));
  frame.stereoCorrelation = 1.0f;
  frame.spectralTilt = 0.5f;
  frame.colorBase = 0.5f;
  frame.waveformGain = 5.0f;
}
