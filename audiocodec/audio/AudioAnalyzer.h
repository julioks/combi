#pragma once

#include "AudioAnalysis.h"
#include <math.h>

static constexpr uint16_t AUDIO_ANALYZER_FFT_MAX_SAMPLES = 1024;
static constexpr uint16_t AUDIO_ANALYZER_FFT_MAX_MASK = AUDIO_ANALYZER_FFT_MAX_SAMPLES - 1;
static constexpr uint16_t AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES = 256;
static constexpr uint16_t AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES = 1024;
static constexpr uint16_t AUDIO_ANALYZER_MID_FFT_SAMPLES = 1024;
static constexpr uint16_t AUDIO_ANALYZER_HIGH_FFT_SAMPLES = 1024;
static constexpr uint16_t AUDIO_ANALYZER_PUBLISH_INTERVAL_MS = 16;
static constexpr float AUDIO_ANALYZER_MIN_FREQ_HZ = 30.0f;
static constexpr float AUDIO_ANALYZER_MAX_FREQ_HZ = 20000.0f;
static constexpr float AUDIO_ANALYZER_DB_FLOOR = -78.0f;
static constexpr float AUDIO_ANALYZER_DB_CEILING = -3.0f;
static constexpr float AUDIO_ANALYZER_SILENCE_DB = -96.0f;
static constexpr float AUDIO_ANALYZER_LOW_HYBRID_MAX_HZ = 220.0f;
static constexpr float AUDIO_ANALYZER_LOW_STABLE_PRESENT_DB = -72.0f;
static constexpr float AUDIO_ANALYZER_LOW_FAST_LIFT_DB = 16.0f;
static constexpr uint8_t AUDIO_ANALYZER_WAVEFORM_CAPTURE_STRIDE = 8;
static constexpr float AUDIO_ANALYZER_TWO_PI_F = 6.28318530718f;
static constexpr float AUDIO_ANALYZER_BLACKMAN_HARRIS_A0 = 0.35875f;
static constexpr float AUDIO_ANALYZER_BLACKMAN_HARRIS_A1 = 0.48829f;
static constexpr float AUDIO_ANALYZER_BLACKMAN_HARRIS_A2 = 0.14128f;
static constexpr float AUDIO_ANALYZER_BLACKMAN_HARRIS_A3 = 0.01168f;

static_assert((AUDIO_ANALYZER_FFT_MAX_SAMPLES & AUDIO_ANALYZER_FFT_MAX_MASK) == 0, "FFT size must be a power of two.");
static_assert(AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES == AUDIO_ANALYZER_FFT_MAX_SAMPLES, "Low stable FFT must fit the shared FFT buffer.");
static_assert(AUDIO_ANALYZER_MID_FFT_SAMPLES == AUDIO_ANALYZER_FFT_MAX_SAMPLES, "Mid FFT must fit the shared FFT buffer.");
static_assert(AUDIO_ANALYZER_HIGH_FFT_SAMPLES == AUDIO_ANALYZER_FFT_MAX_SAMPLES, "High FFT must fit the shared FFT buffer.");

static inline float audioAnalyzerClamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static inline float audioAnalyzerWrap01(float value) {
  while (value < 0.0f) value += 1.0f;
  while (value >= 1.0f) value -= 1.0f;
  return value;
}

class AudioAnalyzer {
public:
  void begin() {
    clearAudioAnalysisFrame(latest);

    rawSampleRate = (float)SAMPLE_RATE;
    midSampleRate = rawSampleRate / 4.0f;
    lowSampleRate = rawSampleRate / 16.0f;

    configureLowPass(decim4A, rawSampleRate, 10000.0f, 0.5411961f);
    configureLowPass(decim4B, rawSampleRate, 10000.0f, 1.3065630f);
    configureLowPass(decim16A, midSampleRate, 2500.0f, 0.5411961f);
    configureLowPass(decim16B, midSampleRate, 2500.0f, 1.3065630f);

    clearRing(rawRing, rawWriteIndex, rawFilled);
    clearRing(midRing, midWriteIndex, midFilled);
    clearRing(lowRing, lowWriteIndex, lowFilled);

    window256Scale = fillBlackmanHarrisWindow(window256, AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES);
    window1024Scale = fillBlackmanHarrisWindow(window1024, AUDIO_ANALYZER_FFT_MAX_SAMPLES);
    fillBitReverse(bitReverse256, AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES);
    fillBitReverse(bitReverse1024, AUDIO_ANALYZER_FFT_MAX_SAMPLES);
    fillTwiddles();

    for (uint16_t i = 0; i < AUDIO_ANALYSIS_WAVEFORM_POINTS; i++) {
      waveformL[i] = 0.0f;
      waveformR[i] = 0.0f;
      waveformM[i] = 0.0f;
    }

    configureBands();
    clearSpectra();
    resetFeatureAccumulators();
    lastPublishMs = millis();
  }

  bool processBlock(const int32_t* interleavedStereo32, uint16_t frames) {
    if (frames == 0) {
      return false;
    }

    uint32_t blockSeed = 0;

    for (uint16_t frame = 0; frame < frames; frame++) {
      int32_t rawL = interleavedStereo32[frame * 2];
      int32_t rawR = interleavedStereo32[frame * 2 + 1];
      int32_t s24L = rawL >> 8;
      int32_t s24R = rawR >> 8;
      float left = (float)s24L / 8388608.0f;
      float right = (float)s24R / 8388608.0f;

      dcL = dcL * 0.9992f + left * 0.0008f;
      dcR = dcR * 0.9992f + right * 0.0008f;
      left -= dcL;
      right -= dcR;

      float mono = (left + right) * 0.5f;
      float side = (right - left) * 0.5f;
      float monoDelta = mono - previousMono;
      float sideDelta = side - previousSide;
      float absMono = fabsf(mono);

      pushRing(rawRing, rawWriteIndex, rawFilled, mono);
      pushDecimatedSamples(mono);

      if (absMono > featurePeak) {
        featurePeak = absMono;
      }
      featureRmsSum += mono * mono;
      featureMonoSum += absMono;
      featureSideSum += fabsf(side);
      featureLeftAbsSum += fabsf(left);
      featureRightAbsSum += fabsf(right);
      featureLeftPower += left * left;
      featureRightPower += right * right;
      featureCrossPower += left * right;
      featureFlowXSum += side * 0.34f + sideDelta * 3.20f;
      featureFlowYSum += mono * 0.22f + monoDelta * 3.60f;
      featureSlopeSum += fabsf(monoDelta) + fabsf(sideDelta) * 0.70f;
      featureCurlSum += mono * sideDelta - side * monoDelta;
      featureSignedWaveSum += mono;

      if ((mono >= 0.0f && previousMono < 0.0f) || (mono < 0.0f && previousMono >= 0.0f)) {
        featureZeroCrossings++;
      }

      previousMono = mono;
      previousSide = side;
      featureSampleCount++;
      totalSamples++;

      waveformCaptureCounter++;
      if (waveformCaptureCounter >= AUDIO_ANALYZER_WAVEFORM_CAPTURE_STRIDE) {
        waveformCaptureCounter = 0;
        waveformL[waveformWriteIndex] = clampSigned(left);
        waveformR[waveformWriteIndex] = clampSigned(right);
        waveformM[waveformWriteIndex] = clampSigned(mono);
        waveformWriteIndex = (waveformWriteIndex + 1) & (AUDIO_ANALYSIS_WAVEFORM_POINTS - 1);
      }

      if ((frame & 0x0F) == 0) {
        blockSeed ^= (uint32_t)s24L + ((uint32_t)s24R << 7) + ((uint32_t)frame << 17) + totalSamples;
      }
    }

    rollingSeed ^= blockSeed + 0x9E3779B9UL + (rollingSeed << 6) + (rollingSeed >> 2);

    uint32_t now = millis();
    if ((uint32_t)(now - lastPublishMs) < AUDIO_ANALYZER_PUBLISH_INTERVAL_MS) {
      return false;
    }

    lastPublishMs = now;
    publishFrame(now);
    return true;
  }

  const AudioAnalysisFrame& latestFrame() const {
    return latest;
  }

private:
  struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    float process(float input) {
      float output = b0 * input + z1;
      z1 = b1 * input - a1 * output + z2;
      z2 = b2 * input - a2 * output;
      return output;
    }

    void reset() {
      z1 = 0.0f;
      z2 = 0.0f;
    }
  };

  struct BandConfig {
    float lowHz;
    float centerHz;
    float highHz;
  };

  AudioAnalysisFrame latest;
  BandConfig bandConfig[AUDIO_ANALYSIS_BANDS];

  float rawSampleRate = 0.0f;
  float midSampleRate = 0.0f;
  float lowSampleRate = 0.0f;

  float rawRing[AUDIO_ANALYZER_FFT_MAX_SAMPLES];
  float midRing[AUDIO_ANALYZER_FFT_MAX_SAMPLES];
  float lowRing[AUDIO_ANALYZER_FFT_MAX_SAMPLES];
  uint16_t rawWriteIndex = 0;
  uint16_t midWriteIndex = 0;
  uint16_t lowWriteIndex = 0;
  uint16_t rawFilled = 0;
  uint16_t midFilled = 0;
  uint16_t lowFilled = 0;

  float window256[AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES];
  float window1024[AUDIO_ANALYZER_FFT_MAX_SAMPLES];
  float window256Scale = 1.0f;
  float window1024Scale = 1.0f;
  uint16_t bitReverse256[AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES];
  uint16_t bitReverse1024[AUDIO_ANALYZER_FFT_MAX_SAMPLES];
  float twiddleCos[AUDIO_ANALYZER_FFT_MAX_SAMPLES / 2];
  float twiddleSin[AUDIO_ANALYZER_FFT_MAX_SAMPLES / 2];
  float fftReal[AUDIO_ANALYZER_FFT_MAX_SAMPLES];
  float fftImag[AUDIO_ANALYZER_FFT_MAX_SAMPLES];

  float lowFastBinDb[AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES / 2 + 1];
  float lowStableBinDb[AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES / 2 + 1];
  float midBinDb[AUDIO_ANALYZER_MID_FFT_SAMPLES / 2 + 1];
  float highBinDb[AUDIO_ANALYZER_HIGH_FFT_SAMPLES / 2 + 1];
  float bandDb[AUDIO_ANALYSIS_BANDS];
  float rawTarget[AUDIO_ANALYSIS_BANDS];

  float waveformL[AUDIO_ANALYSIS_WAVEFORM_POINTS];
  float waveformR[AUDIO_ANALYSIS_WAVEFORM_POINTS];
  float waveformM[AUDIO_ANALYSIS_WAVEFORM_POINTS];
  uint16_t waveformWriteIndex = 0;
  uint8_t waveformCaptureCounter = 0;

  Biquad decim4A;
  Biquad decim4B;
  Biquad decim16A;
  Biquad decim16B;
  uint8_t decim4Count = 0;
  uint8_t decim16Count = 0;

  float dcL = 0.0f;
  float dcR = 0.0f;
  float previousMono = 0.0f;
  float previousSide = 0.0f;
  float previousBassLevel = 0.0f;
  float previousTrebleLevel = 0.0f;
  float colorBase = 0.5f;
  float waveformGain = 5.0f;
  uint32_t lastPublishMs = 0;
  uint32_t totalSamples = 0;
  uint32_t sequence = 0;
  uint32_t rollingSeed = 0xC001CAFEUL;

  float featureRmsSum = 0.0f;
  float featurePeak = 0.0f;
  float featureMonoSum = 0.0f;
  float featureSideSum = 0.0f;
  float featureLeftAbsSum = 0.0f;
  float featureRightAbsSum = 0.0f;
  float featureLeftPower = 0.0f;
  float featureRightPower = 0.0f;
  float featureCrossPower = 0.0f;
  float featureFlowXSum = 0.0f;
  float featureFlowYSum = 0.0f;
  float featureSlopeSum = 0.0f;
  float featureCurlSum = 0.0f;
  float featureSignedWaveSum = 0.0f;
  uint16_t featureZeroCrossings = 0;
  uint16_t featureSampleCount = 0;

  void configureBands() {
    float maxFrequency = AUDIO_ANALYZER_MAX_FREQ_HZ;
    float nyquistLimit = rawSampleRate * 0.45f;
    if (maxFrequency > nyquistLimit) {
      maxFrequency = nyquistLimit;
    }

    float ratio = maxFrequency / AUDIO_ANALYZER_MIN_FREQ_HZ;
    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      float lowFraction = (float)band / (float)AUDIO_ANALYSIS_BANDS;
      float centerFraction = ((float)band + 0.5f) / (float)AUDIO_ANALYSIS_BANDS;
      float highFraction = (float)(band + 1) / (float)AUDIO_ANALYSIS_BANDS;
      bandConfig[band].lowHz = AUDIO_ANALYZER_MIN_FREQ_HZ * powf(ratio, lowFraction);
      bandConfig[band].centerHz = AUDIO_ANALYZER_MIN_FREQ_HZ * powf(ratio, centerFraction);
      bandConfig[band].highHz = AUDIO_ANALYZER_MIN_FREQ_HZ * powf(ratio, highFraction);
      bandDb[band] = AUDIO_ANALYZER_SILENCE_DB;
      rawTarget[band] = 0.0f;
      latest.bandCenterHz[band] = bandConfig[band].centerHz;
    }
  }

  void configureLowPass(Biquad& biquad, float sampleRate, float cutoffHz, float q) {
    float w0 = AUDIO_ANALYZER_TWO_PI_F * cutoffHz / sampleRate;
    float cosW0 = cosf(w0);
    float sinW0 = sinf(w0);
    float alpha = sinW0 / (2.0f * q);
    float invA0 = 1.0f / (1.0f + alpha);

    biquad.b0 = ((1.0f - cosW0) * 0.5f) * invA0;
    biquad.b1 = (1.0f - cosW0) * invA0;
    biquad.b2 = biquad.b0;
    biquad.a1 = (-2.0f * cosW0) * invA0;
    biquad.a2 = (1.0f - alpha) * invA0;
    biquad.reset();
  }

  void clearRing(float* ring, uint16_t& writeIndex, uint16_t& filled) {
    writeIndex = 0;
    filled = 0;
    for (uint16_t i = 0; i < AUDIO_ANALYZER_FFT_MAX_SAMPLES; i++) {
      ring[i] = 0.0f;
    }
  }

  void pushRing(float* ring, uint16_t& writeIndex, uint16_t& filled, float sample) {
    ring[writeIndex] = sample;
    writeIndex = (writeIndex + 1) & AUDIO_ANALYZER_FFT_MAX_MASK;
    if (filled < AUDIO_ANALYZER_FFT_MAX_SAMPLES) {
      filled++;
    }
  }

  void pushDecimatedSamples(float sample) {
    float filtered4 = decim4B.process(decim4A.process(sample));
    decim4Count++;
    if (decim4Count < 4) {
      return;
    }

    decim4Count = 0;
    pushRing(midRing, midWriteIndex, midFilled, filtered4);

    float filtered16 = decim16B.process(decim16A.process(filtered4));
    decim16Count++;
    if (decim16Count < 4) {
      return;
    }

    decim16Count = 0;
    pushRing(lowRing, lowWriteIndex, lowFilled, filtered16);
  }

  float fillBlackmanHarrisWindow(float* target, uint16_t samples) {
    float sum = 0.0f;
    for (uint16_t i = 0; i < samples; i++) {
      float phase = AUDIO_ANALYZER_TWO_PI_F * (float)i / (float)(samples - 1);
      float value =
        AUDIO_ANALYZER_BLACKMAN_HARRIS_A0 -
        AUDIO_ANALYZER_BLACKMAN_HARRIS_A1 * cosf(phase) +
        AUDIO_ANALYZER_BLACKMAN_HARRIS_A2 * cosf(phase * 2.0f) -
        AUDIO_ANALYZER_BLACKMAN_HARRIS_A3 * cosf(phase * 3.0f);
      target[i] = value;
      sum += value;
    }

    return sum * 0.5f;
  }

  void fillBitReverse(uint16_t* table, uint16_t samples) {
    uint8_t bits = 0;
    while (((uint16_t)1 << bits) < samples) {
      bits++;
    }

    for (uint16_t i = 0; i < samples; i++) {
      uint16_t reversed = 0;
      for (uint8_t bit = 0; bit < bits; bit++) {
        if (i & ((uint16_t)1 << bit)) {
          reversed |= (uint16_t)1 << (bits - 1 - bit);
        }
      }
      table[i] = reversed;
    }
  }

  void fillTwiddles() {
    for (uint16_t i = 0; i < AUDIO_ANALYZER_FFT_MAX_SAMPLES / 2; i++) {
      float phase = AUDIO_ANALYZER_TWO_PI_F * (float)i / (float)AUDIO_ANALYZER_FFT_MAX_SAMPLES;
      twiddleCos[i] = cosf(phase);
      twiddleSin[i] = -sinf(phase);
    }
  }

  void clearSpectra() {
    clearBinDb(lowFastBinDb, AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES / 2 + 1);
    clearBinDb(lowStableBinDb, AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES / 2 + 1);
    clearBinDb(midBinDb, AUDIO_ANALYZER_MID_FFT_SAMPLES / 2 + 1);
    clearBinDb(highBinDb, AUDIO_ANALYZER_HIGH_FFT_SAMPLES / 2 + 1);
  }

  void clearBinDb(float* bins, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
      bins[i] = AUDIO_ANALYZER_SILENCE_DB;
    }
  }

  void computeAllSpectra() {
    computeSpectrumFromRing(
      lowRing,
      lowWriteIndex,
      lowFilled,
      AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES,
      window256,
      window256Scale,
      lowFastBinDb
    );
    computeSpectrumFromRing(
      lowRing,
      lowWriteIndex,
      lowFilled,
      AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES,
      window1024,
      window1024Scale,
      lowStableBinDb
    );
    computeSpectrumFromRing(
      midRing,
      midWriteIndex,
      midFilled,
      AUDIO_ANALYZER_MID_FFT_SAMPLES,
      window1024,
      window1024Scale,
      midBinDb
    );
    computeSpectrumFromRing(
      rawRing,
      rawWriteIndex,
      rawFilled,
      AUDIO_ANALYZER_HIGH_FFT_SAMPLES,
      window1024,
      window1024Scale,
      highBinDb
    );
  }

  void computeSpectrumFromRing(
    const float* ring,
    uint16_t writeIndex,
    uint16_t filled,
    uint16_t fftSamples,
    const float* window,
    float magnitudeScale,
    float* targetDb
  ) {
    uint16_t binCount = fftSamples / 2 + 1;
    if (filled < fftSamples) {
      clearBinDb(targetDb, binCount);
      return;
    }

    uint16_t oldestIndex = (writeIndex + AUDIO_ANALYZER_FFT_MAX_SAMPLES - fftSamples) & AUDIO_ANALYZER_FFT_MAX_MASK;
    for (uint16_t i = 0; i < fftSamples; i++) {
      uint16_t ringIndex = (oldestIndex + i) & AUDIO_ANALYZER_FFT_MAX_MASK;
      fftReal[i] = ring[ringIndex] * window[i];
      fftImag[i] = 0.0f;
    }
    for (uint16_t i = fftSamples; i < AUDIO_ANALYZER_FFT_MAX_SAMPLES; i++) {
      fftReal[i] = 0.0f;
      fftImag[i] = 0.0f;
    }

    runFft(fftSamples, fftSamples == AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES ? bitReverse256 : bitReverse1024);

    targetDb[0] = AUDIO_ANALYZER_SILENCE_DB;
    for (uint16_t bin = 1; bin < binCount; bin++) {
      float magnitude = sqrtf(fftReal[bin] * fftReal[bin] + fftImag[bin] * fftImag[bin]) / magnitudeScale;
      float db = 20.0f * log10f(magnitude + 0.0000001f);
      if (db < AUDIO_ANALYZER_SILENCE_DB) {
        db = AUDIO_ANALYZER_SILENCE_DB;
      }
      targetDb[bin] = db;
    }
  }

  void runFft(uint16_t fftSamples, const uint16_t* bitReverseTable) {
    for (uint16_t i = 0; i < fftSamples; i++) {
      uint16_t j = bitReverseTable[i];
      if (j > i) {
        float realTemp = fftReal[i];
        float imagTemp = fftImag[i];
        fftReal[i] = fftReal[j];
        fftImag[i] = fftImag[j];
        fftReal[j] = realTemp;
        fftImag[j] = imagTemp;
      }
    }

    for (uint16_t length = 2; length <= fftSamples; length <<= 1) {
      uint16_t halfLength = length >> 1;
      uint16_t twiddleStep = AUDIO_ANALYZER_FFT_MAX_SAMPLES / length;

      for (uint16_t start = 0; start < fftSamples; start += length) {
        for (uint16_t offset = 0; offset < halfLength; offset++) {
          uint16_t twiddleIndex = offset * twiddleStep;
          float wr = twiddleCos[twiddleIndex];
          float wi = twiddleSin[twiddleIndex];
          uint16_t evenIndex = start + offset;
          uint16_t oddIndex = evenIndex + halfLength;

          float oddReal = fftReal[oddIndex] * wr - fftImag[oddIndex] * wi;
          float oddImag = fftReal[oddIndex] * wi + fftImag[oddIndex] * wr;
          float evenReal = fftReal[evenIndex];
          float evenImag = fftImag[evenIndex];

          fftReal[evenIndex] = evenReal + oddReal;
          fftImag[evenIndex] = evenImag + oddImag;
          fftReal[oddIndex] = evenReal - oddReal;
          fftImag[oddIndex] = evenImag - oddImag;
        }
      }
    }
  }

  void publishFrame(uint32_t now) {
    computeAllSpectra();

    float lowStablePeakDb = spectrumRangeDb(
      lowStableBinDb,
      AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES,
      lowSampleRate,
      30.0f,
      AUDIO_ANALYZER_LOW_HYBRID_MAX_HZ,
      85.0f
    );
    bool lowStablePresent = lowFilled >= AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES &&
      lowStablePeakDb > AUDIO_ANALYZER_LOW_STABLE_PRESENT_DB;

    float centroidSum = 0.0f;
    float centroidWeight = 0.0f;
    float dominantLevel = 0.0f;
    uint16_t dominantBand = 0;

    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      float db = computeBandDb(band, lowStablePresent);
      bandDb[band] = db;
      rawTarget[band] = dbToBandLevel(db);

      float linearMagnitude = powf(10.0f, db * 0.05f);
      centroidSum += bandConfig[band].centerHz * linearMagnitude;
      centroidWeight += linearMagnitude;

      if (rawTarget[band] > dominantLevel) {
        dominantLevel = rawTarget[band];
        dominantBand = band;
      }
    }

    float invSamples = featureSampleCount > 0 ? 1.0f / (float)featureSampleCount : 0.0f;
    float rms = sqrtf(featureRmsSum * invSamples);
    float peak = featurePeak;
    float subBassBand = rangeLevel(30.0f, 60.0f);
    float kickBand = rangeLevel(45.0f, 140.0f);
    float bassBand = rangeLevel(30.0f, 220.0f);
    float lowMidBand = rangeLevel(220.0f, 600.0f);
    float midBand = rangeLevel(600.0f, 2500.0f);
    float trebleBand = rangeLevel(2500.0f, 16000.0f);
    float monoBlock = featureMonoSum * invSamples;
    float sideBlock = featureSideSum * invSamples;

    float loudness = audioAnalyzerClamp01(peak * 0.72f + rms * 1.20f);
    float bassTransient = positiveDelta(bassBand, previousBassLevel, 0.20f);
    float trebleTransient = positiveDelta(trebleBand, previousTrebleLevel, 0.20f);
    float transient = bassTransient;
    if (trebleTransient > transient) {
      transient = trebleTransient;
    }
    previousBassLevel = bassBand;
    previousTrebleLevel = trebleBand;

    float lrTotal = featureLeftAbsSum + featureRightAbsSum + 0.000001f;
    float balance = (featureRightAbsSum - featureLeftAbsSum) / lrTotal;
    float width = audioAnalyzerClamp01(sideBlock / (monoBlock + sideBlock + 0.000001f) * 2.0f);
    float powerDenom = sqrtf(featureLeftPower * featureRightPower) + 0.000001f;
    float correlation = featureCrossPower / powerDenom;
    if (correlation < -1.0f) correlation = -1.0f;
    if (correlation > 1.0f) correlation = 1.0f;

    float flowDenom = monoBlock + sideBlock + 0.00002f;
    float flowX = clampSigned((featureFlowXSum * invSamples) / flowDenom * 2.2f);
    float flowY = clampSigned((featureFlowYSum * invSamples) / flowDenom * 2.2f);
    float chaos = audioAnalyzerClamp01((featureSlopeSum * invSamples) / flowDenom * 4.4f);
    float curl = clampSigned((featureCurlSum * invSamples) / (flowDenom * flowDenom + 0.000001f) * 0.30f);
    float waveBias = clampSigned((featureSignedWaveSum * invSamples) / (monoBlock + 0.000001f));
    float zeroCrossing = audioAnalyzerClamp01(((float)featureZeroCrossings * invSamples) * 32.0f);
    float spectralTilt = trebleBand / (bassBand + trebleBand + 0.0001f);
    float centroid = centroidWeight > 0.000001f ? centroidSum / centroidWeight : 0.0f;

    colorBase = audioAnalyzerWrap01(colorBase * 0.86f + audioAnalyzerWrap01(spectralTilt * 0.52f + balance * 0.16f + curl * 0.10f + waveBias * 0.08f) * 0.14f);

    float gainTarget = 0.72f / (peak + 0.00001f);
    if (gainTarget < 1.2f) gainTarget = 1.2f;
    if (gainTarget > 18.0f) gainTarget = 18.0f;
    waveformGain = waveformGain * 0.92f + gainTarget * 0.08f;

    latest.ready = true;
    latest.sequence = ++sequence;
    latest.sampleCounter = totalSamples;
    latest.generatedAtMs = now;
    latest.audioSeed = rollingSeed;
    latest.rms = rms;
    latest.peak = peak;
    latest.loudness = loudness;
    latest.subBass = subBassBand;
    latest.kick = kickBand;
    latest.bass = bassBand;
    latest.lowMid = lowMidBand;
    latest.mid = midBand;
    latest.treble = trebleBand;
    latest.transient = transient;
    latest.bassTransient = bassTransient;
    latest.trebleTransient = trebleTransient;
    latest.stereoBalance = balance;
    latest.stereoWidth = width;
    latest.stereoCorrelation = correlation;
    latest.spectralTilt = spectralTilt;
    latest.spectralCentroidHz = centroid;
    latest.dominantBand = dominantBand;
    latest.dominantFrequencyHz = bandConfig[dominantBand].centerHz;
    latest.zeroCrossing = zeroCrossing;
    latest.audioFlowX = flowX;
    latest.audioFlowY = flowY;
    latest.audioCurl = curl;
    latest.audioChaos = chaos;
    latest.colorBase = colorBase;
    latest.waveformGain = waveformGain;

    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      latest.bands[band] = rawTarget[band];
      latest.bandDb[band] = bandDb[band];
      latest.bandPeak[band] = rawTarget[band];
      latest.bandCenterHz[band] = bandConfig[band].centerHz;
    }

    for (uint16_t i = 0; i < AUDIO_ANALYSIS_WAVEFORM_POINTS; i++) {
      uint16_t index = (waveformWriteIndex + i) & (AUDIO_ANALYSIS_WAVEFORM_POINTS - 1);
      latest.waveformL[i] = waveformL[index];
      latest.waveformR[i] = waveformR[index];
      latest.waveformM[i] = waveformM[index];
    }

    resetFeatureAccumulators();
  }

  float computeBandDb(uint16_t band, bool lowStablePresent) const {
    const BandConfig& config = bandConfig[band];

    if (config.centerHz < AUDIO_ANALYZER_LOW_HYBRID_MAX_HZ) {
      float fastDb = spectrumRangeDb(
        lowFastBinDb,
        AUDIO_ANALYZER_LOW_FAST_FFT_SAMPLES,
        lowSampleRate,
        config.lowHz,
        config.highHz,
        config.centerHz
      );
      float stableDb = spectrumRangeDb(
        lowStableBinDb,
        AUDIO_ANALYZER_LOW_STABLE_FFT_SAMPLES,
        lowSampleRate,
        config.lowHz,
        config.highHz,
        config.centerHz
      );

      if (!lowStablePresent) {
        return fastDb;
      }

      float fastCap = stableDb + AUDIO_ANALYZER_LOW_FAST_LIFT_DB;
      if (fastDb > fastCap) {
        fastDb = fastCap;
      }
      return stableDb > fastDb ? stableDb : fastDb;
    }

    if (config.centerHz < 4800.0f) {
      return spectrumRangeDb(
        midBinDb,
        AUDIO_ANALYZER_MID_FFT_SAMPLES,
        midSampleRate,
        config.lowHz,
        config.highHz,
        config.centerHz
      );
    }

    return spectrumRangeDb(
      highBinDb,
      AUDIO_ANALYZER_HIGH_FFT_SAMPLES,
      rawSampleRate,
      config.lowHz,
      config.highHz,
      config.centerHz
    );
  }

  float spectrumRangeDb(
    const float* binDb,
    uint16_t fftSamples,
    float sampleRate,
    float lowHz,
    float highHz,
    float centerHz
  ) const {
    float binHz = sampleRate / (float)fftSamples;
    uint16_t maxBin = fftSamples / 2;
    int firstBin = (int)ceilf(lowHz / binHz);
    int lastBin = (int)floorf(highHz / binHz);

    if (firstBin < 1) {
      firstBin = 1;
    }
    if (lastBin > (int)maxBin) {
      lastBin = maxBin;
    }

    if (firstBin > lastBin) {
      int nearestBin = (int)roundf(centerHz / binHz);
      if (nearestBin < 1) {
        nearestBin = 1;
      }
      if (nearestBin > (int)maxBin) {
        nearestBin = maxBin;
      }
      return binDb[nearestBin];
    }

    float strongestDb = AUDIO_ANALYZER_SILENCE_DB;
    for (int bin = firstBin; bin <= lastBin; bin++) {
      if (binDb[bin] > strongestDb) {
        strongestDb = binDb[bin];
      }
    }
    return strongestDb;
  }

  float dbToBandLevel(float db) const {
    return audioAnalyzerClamp01((db - AUDIO_ANALYZER_DB_FLOOR) / (AUDIO_ANALYZER_DB_CEILING - AUDIO_ANALYZER_DB_FLOOR));
  }

  float rangeLevel(float lowHz, float highHz) const {
    float strongest = 0.0f;
    bool foundBand = false;

    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      float frequency = bandConfig[band].centerHz;
      if (frequency < lowHz || frequency >= highHz) {
        continue;
      }

      if (!foundBand || rawTarget[band] > strongest) {
        strongest = rawTarget[band];
        foundBand = true;
      }
    }

    return foundBand ? strongest : 0.0f;
  }

  float positiveDelta(float current, float previous, float scale) const {
    if (current <= previous) {
      return 0.0f;
    }
    return audioAnalyzerClamp01((current - previous) / scale);
  }

  void resetFeatureAccumulators() {
    featureRmsSum = 0.0f;
    featurePeak = 0.0f;
    featureMonoSum = 0.0f;
    featureSideSum = 0.0f;
    featureLeftAbsSum = 0.0f;
    featureRightAbsSum = 0.0f;
    featureLeftPower = 0.0f;
    featureRightPower = 0.0f;
    featureCrossPower = 0.0f;
    featureFlowXSum = 0.0f;
    featureFlowYSum = 0.0f;
    featureSlopeSum = 0.0f;
    featureCurlSum = 0.0f;
    featureSignedWaveSum = 0.0f;
    featureZeroCrossings = 0;
    featureSampleCount = 0;
  }

  float clampSigned(float value) const {
    if (value < -1.0f) return -1.0f;
    if (value > 1.0f) return 1.0f;
    return value;
  }
};
