#include "ManchesterPacketDecoder.h"

#include "Config.h"
#include "LedFrameGrid.h"

static constexpr uint32_t RAW_AUDIO_RGB_TOGGLE_DEBOUNCE_MS = 30;

void ManchesterPacketDecoder::begin() {
#if RAW_AUDIO_RGB_TOGGLE_PIN >= 0
  pinMode(RAW_AUDIO_RGB_TOGGLE_PIN, INPUT_PULLUP);

  rawToggleLastReadingActive = readRawAudioRgbTogglePinActive();
  rawToggleStableActive = rawToggleLastReadingActive;
  rawToggleLastChangeMs = millis();

  if (rawToggleStableActive) {
    setRawAudioRgbMode(!rawAudioRgbModeActive);
  }
#endif
}

bool ManchesterPacketDecoder::rawAudioRgbModeEnabled() const {
  return rawAudioRgbModeActive;
}

bool ManchesterPacketDecoder::readRawAudioRgbTogglePinActive() const {
#if RAW_AUDIO_RGB_TOGGLE_PIN >= 0
  return digitalRead(RAW_AUDIO_RGB_TOGGLE_PIN) == LOW;
#else
  return false;
#endif
}

void ManchesterPacketDecoder::setRawAudioRgbMode(bool enabled) {
  if (rawAudioRgbModeActive == enabled) {
    return;
  }

  rawAudioRgbModeActive = enabled;
  resetForNextPacket();

  if (rawAudioRgbModeActive) {
    rawFramePusher.clear();
    Serial.println("raw-audio-rgb on");
  } else {
    Serial.println("packet-decoder on");
  }
}

void ManchesterPacketDecoder::pollRawAudioRgbModeToggle() {
#if RAW_AUDIO_RGB_TOGGLE_PIN >= 0
  const bool readingActive = readRawAudioRgbTogglePinActive();
  const uint32_t nowMs = millis();

  if (readingActive != rawToggleLastReadingActive) {
    rawToggleLastReadingActive = readingActive;
    rawToggleLastChangeMs = nowMs;
    return;
  }

  if (readingActive == rawToggleStableActive) {
    return;
  }

  if ((uint32_t)(nowMs - rawToggleLastChangeMs) < RAW_AUDIO_RGB_TOGGLE_DEBOUNCE_MS) {
    return;
  }

  rawToggleStableActive = readingActive;

  if (rawToggleStableActive) {
    setRawAudioRgbMode(!rawAudioRgbModeActive);
  }
#endif
}

void ManchesterPacketDecoder::resetPacketDecoderOnly() {
  mode = SEARCH_PREAMBLE_AND_SFD;

  havePrevMidBit = false;
  prevMidBit = 0;
  alternatingRun = 0;

  activeInvert = false;
  sfdBitsLeftToDiscard = 0;
}

void ManchesterPacketDecoder::resetPayloadConsumer() {
  rawFramePusher.reset();
  payloadParser.reset();
}

void ManchesterPacketDecoder::resetForNextPacket() {
  resetPacketDecoderOnly();

  haveAnyEdge = false;
  lastEdgeUs = 0;
  lastLevel = 0;

  haveLastMid = false;
  lastMidUs = 0;
  haveLastAcceptedRawBit = false;
  lastAcceptedRawBit = 0;
  sawBoundarySinceLastMid = false;

  bitPeriodQ8 = 0;
  periodSamples = 0;

  resetPayloadConsumer();
}

void ManchesterPacketDecoder::finishPacketBecauseOfSilence() {
  statSilenceResets++;
  resetForNextPacket();

  if (rawAudioRgbModeActive) {
    rawFramePusher.clear();
  }
}

bool ManchesterPacketDecoder::timingReady() const {
  return periodSamples >= MIN_PERIOD_SAMPLES && bitPeriodQ8 > 0;
}

uint32_t ManchesterPacketDecoder::bitPeriodUs() const {
  if (bitPeriodQ8 == 0) {
    return 0;
  }
  return bitPeriodQ8 >> 8;
}

void ManchesterPacketDecoder::updatePreamblePeriod(uint32_t gapUs) {
  if (gapUs < MIN_PREAMBLE_GAP_US || gapUs > MAX_PREAMBLE_GAP_US) {
    return;
  }

  const uint32_t sampleQ8 = gapUs << 8;

  if (bitPeriodQ8 == 0) {
    bitPeriodQ8 = sampleQ8;
  } else {
    // While acquiring from 0x55, all accepted mid-bit edges should be one bit apart.
    bitPeriodQ8 = ((bitPeriodQ8 * 7UL) + sampleQ8) >> 3;
  }

  if (periodSamples < 255) {
    periodSamples++;
  }
}

void ManchesterPacketDecoder::updateBoundaryPeriod(uint32_t boundaryGapUs) {
  // A boundary edge between equal data bits lands about half a bit after the
  // previous mid-bit edge. Use it as a conservative tape-speed tracking sample.
  const uint32_t sampleQ8 = boundaryGapUs << 9;

  if (bitPeriodQ8 == 0) {
    bitPeriodQ8 = sampleQ8;
    periodSamples = 1;
    return;
  }

  bitPeriodQ8 = ((bitPeriodQ8 * 31UL) + sampleQ8) >> 5;
}

void ManchesterPacketDecoder::updateMidPeriod(uint32_t midGapUs) {
  const uint32_t sampleQ8 = midGapUs << 8;

  if (bitPeriodQ8 == 0) {
    bitPeriodQ8 = sampleQ8;
    periodSamples = 1;
    return;
  }

  // Slower smoothing once timing is established.
  bitPeriodQ8 = ((bitPeriodQ8 * 15UL) + sampleQ8) >> 4;
}

void ManchesterPacketDecoder::startSfdDiscard(uint8_t repeatedRawBit) {
  // Observed byte stream is MSB-first:
  // 0x55 = 01010101
  // 0xD5 = 11010101
  // The preamble/SFD boundary creates the first repeated bit pair: ...1 1...
  // If the repeated SFD bit is 0, polarity is inverted.
  activeInvert = (repeatedRawBit == 0);

  mode = DISCARD_SFD_REMAINDER;

  // The current repeated bit is already bit 0 of the 8-bit SFD.
  // Discard the remaining 7 SFD bits. The next bit after that is payload bit 0.
  sfdBitsLeftToDiscard = 7;
  statSfdLocks++;

}

void ManchesterPacketDecoder::feedPreambleSearchBit(uint8_t rawBit) {
  if (!havePrevMidBit) {
    havePrevMidBit = true;
    prevMidBit = rawBit;
    alternatingRun = 1;
    return;
  }

  if (rawBit != prevMidBit) {
    if (alternatingRun < 65535) {
      alternatingRun++;
    }
    prevMidBit = rawBit;
    return;
  }

  // First repeated bit after a long alternating preamble is the first bit of 0xD5 in this stream.
  if (timingReady() && alternatingRun >= MIN_ALTERNATING_BITS_BEFORE_SFD) {
    startSfdDiscard(rawBit);
    prevMidBit = rawBit;
    alternatingRun = 0;
    return;
  }

  // Too early to trust as SFD. Treat it as a new possible preamble start.
  prevMidBit = rawBit;
  alternatingRun = 1;
}

void ManchesterPacketDecoder::feedSfdDiscardBit(uint8_t rawBit) {
  (void)rawBit;

  if (sfdBitsLeftToDiscard > 0) {
    sfdBitsLeftToDiscard--;
  }

  if (sfdBitsLeftToDiscard == 0) {
    mode = READ_DATA;
  }
}

void ManchesterPacketDecoder::feedMidBit(uint8_t rawBit) {
  if (rawAudioRgbModeActive) {
    rawFramePusher.pushBit(rawBit);
    return;
  }

  if (mode == SEARCH_PREAMBLE_AND_SFD) {
    feedPreambleSearchBit(rawBit);
  } else if (mode == DISCARD_SFD_REMAINDER) {
    feedSfdDiscardBit(rawBit);
  } else {
    feedDataBit(rawBit);
  }
}

void ManchesterPacketDecoder::feedDataBit(uint8_t rawBit) {
  const uint8_t bit = activeInvert ? (rawBit ^ 1) : rawBit;
  statPayloadBits++;

  // Payload is forwarded bit-by-bit so indexed LED frames do not need any
  // sender-side byte padding between frame fields.
  payloadParser.feedBit(bit);
}

void ManchesterPacketDecoder::feedInferredMidBit() {
  if (!haveLastAcceptedRawBit) {
    return;
  }

  const uint8_t rawBit = sawBoundarySinceLastMid
    ? lastAcceptedRawBit
    : (uint8_t)(lastAcceptedRawBit ^ 1U);

  feedMidBit(rawBit);
  haveLastAcceptedRawBit = true;
  lastAcceptedRawBit = rawBit;
  sawBoundarySinceLastMid = false;
  statMidBits++;
  statInferredMidBits++;
}

void ManchesterPacketDecoder::acceptMidBitEdge(const EdgeEvent &event) {
  const uint8_t rawBit = event.level ? 1 : 0;
  feedMidBit(rawBit);
  haveLastAcceptedRawBit = true;
  lastAcceptedRawBit = rawBit;
  sawBoundarySinceLastMid = false;
  statMidBits++;
}

void ManchesterPacketDecoder::treatEdgeAsFirstMidBit(const EdgeEvent &event) {
  haveLastMid = true;
  lastMidUs = event.t_us;
  acceptMidBitEdge(event);
}

void ManchesterPacketDecoder::resetTimingAndSearchFromThisEdge(const EdgeEvent &event) {
  statTimingResets++;
  resetPacketDecoderOnly();
  // Packet mode needs a fresh payload parser after an impossible Manchester
  // gap so guarded resync chunks can reacquire cleanly. Raw visualizer mode is
  // intentionally continuous, so keep its byte/pixel cursor moving across
  // timing reacquisition instead of jumping back to pixel 0.
  if (!rawAudioRgbModeActive) {
    resetPayloadConsumer();
  }
  haveLastMid = false;
  haveLastAcceptedRawBit = false;
  lastAcceptedRawBit = 0;
  sawBoundarySinceLastMid = false;
  bitPeriodQ8 = 0;
  periodSamples = 0;

  lastEdgeUs = event.t_us;
  lastLevel = event.level;
  treatEdgeAsFirstMidBit(event);
}

bool ManchesterPacketDecoder::recoverOneMissedMidBitBefore(const EdgeEvent &event, uint32_t bitUs) {
#if ZC_MISSED_MID_RECOVERY
  if (!haveLastMid || !haveLastAcceptedRawBit || bitUs == 0) {
    return false;
  }

  const uint32_t gapFromLastMid = event.t_us - lastMidUs;
  const uint32_t boundaryMin = (bitUs * BOUNDARY_MIN_NUM) / BOUNDARY_MIN_DEN;
  const uint32_t midMax = (bitUs * MID_MAX_NUM) / MID_MAX_DEN;
  const uint32_t recoverMin = bitUs + boundaryMin;
  const uint32_t recoverMax = bitUs + midMax;

  if (gapFromLastMid <= midMax || gapFromLastMid < recoverMin || gapFromLastMid > recoverMax) {
    return false;
  }

  lastMidUs += bitUs;
  feedInferredMidBit();
  return true;
#else
  (void)event;
  (void)bitUs;
  return false;
#endif
}

void ManchesterPacketDecoder::processEdge(const EdgeEvent &event) {
  statEdgesSeen++;

  if (!haveAnyEdge) {
    haveAnyEdge = true;
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    treatEdgeAsFirstMidBit(event);
    return;
  }

  const uint32_t gapFromLastEdge = event.t_us - lastEdgeUs;

  if (gapFromLastEdge >= SILENCE_RESET_US) {
    finishPacketBecauseOfSilence();

    haveAnyEdge = true;
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    treatEdgeAsFirstMidBit(event);
    return;
  }

  const bool sameLevelAsLastEdge = (event.level == lastLevel);

  if (!timingReady()) {
    if (sameLevelAsLastEdge) {
      statSameLevelEdges++;
      resetTimingAndSearchFromThisEdge(event);
      return;
    }

    if (gapFromLastEdge < MIN_PREAMBLE_GAP_US) {
      // Too fast to be tape drift or a valid Manchester transition.
      statTooCloseEdges++;
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      return;
    }

    // During 0x55 preamble, valid mid-bit edges arrive roughly one bit period apart.
    updatePreamblePeriod(gapFromLastEdge);
    statPreambleEdges++;

    lastEdgeUs = event.t_us;
    lastLevel = event.level;

    treatEdgeAsFirstMidBit(event);
    return;
  }

  if (sameLevelAsLastEdge) {
    statSameLevelEdges++;
  }

  const uint32_t bitUs = bitPeriodUs();

  if (bitUs == 0) {
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  const uint32_t tooCloseMax = (bitUs * EDGE_TOO_CLOSE_NUM) / EDGE_TOO_CLOSE_DEN;
  const uint32_t boundaryMin = (bitUs * BOUNDARY_MIN_NUM) / BOUNDARY_MIN_DEN;
  const uint32_t boundaryMax = (bitUs * BOUNDARY_MAX_NUM) / BOUNDARY_MAX_DEN;
  const uint32_t midMinNoBoundary = (bitUs * MID_MIN_NO_BOUNDARY_NUM) / MID_MIN_NO_BOUNDARY_DEN;
  const uint32_t midMinAfterBoundary = (bitUs * MID_MIN_AFTER_BOUNDARY_NUM) / MID_MIN_AFTER_BOUNDARY_DEN;
  const uint32_t midMin = sawBoundarySinceLastMid ? midMinAfterBoundary : midMinNoBoundary;
  const uint32_t midMax = (bitUs * MID_MAX_NUM) / MID_MAX_DEN;

  recoverOneMissedMidBitBefore(event, bitUs);

  const uint32_t gapFromLastMid = event.t_us - lastMidUs;

  if (gapFromLastMid < tooCloseMax) {
    // Too fast to be tape drift, a bit boundary, or a mid-bit transition.
    statTooCloseEdges++;
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  if (gapFromLastMid >= boundaryMin && gapFromLastMid <= boundaryMax) {
    // Manchester boundary transition between two equal data bits.
    // Real edge, but not the bit-value transition. Use it as a phase marker;
    // optionally let it tune timing for experiments.
#if ZC_TRACK_BOUNDARY_PERIOD
    updateBoundaryPeriod(gapFromLastMid);
#endif
    sawBoundarySinceLastMid = true;
    statBoundaryEdges++;
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  if (gapFromLastMid < midMin) {
    // Between the valid boundary and mid-bit windows. Keep physical edge state,
    // but do not let an implausible interval pull the timing estimate around.
    statEarlyEdges++;
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  if (gapFromLastMid > midMax) {
    // We lost timing, but this is not silence. A guarded resync chunk uses this
    // impossible Manchester gap; valid wow/flutter stays inside the timing
    // windows above and keeps updating the period estimate.
    statLongGapResets++;
    resetTimingAndSearchFromThisEdge(event);
    return;
  }

  // This is a mid-bit Manchester transition.
  updateMidPeriod(gapFromLastMid);
  lastMidUs = event.t_us;

  acceptMidBitEdge(event);

  lastEdgeUs = event.t_us;
  lastLevel = event.level;
}

void ManchesterPacketDecoder::pollForSilence() {
  if (!haveAnyEdge) {
    return;
  }

  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastEdgeUs) >= SILENCE_RESET_US) {
    finishPacketBecauseOfSilence();
  }
}

void ManchesterPacketDecoder::printDiagnostics(
  uint32_t isrDrops,
  uint32_t ringFill,
  uint32_t ringHighWater
) const {
#if ZC_DIAGNOSTICS
  Serial.print("zc ms=");
  Serial.print(millis());
  Serial.print(" state=");
  Serial.print((uint8_t)mode);
  Serial.print(" raw=");
  Serial.print(rawAudioRgbModeActive ? 1 : 0);
  Serial.print(" bit_us=");
  Serial.print(bitPeriodUs());
  Serial.print(" samples=");
  Serial.print(periodSamples);
  Serial.print(" edges=");
  Serial.print(statEdgesSeen);
  Serial.print(" mid=");
  Serial.print(statMidBits);
  Serial.print(" payload=");
  Serial.print(statPayloadBits);
  Serial.print(" inferred=");
  Serial.print(statInferredMidBits);
  Serial.print(" boundary=");
  Serial.print(statBoundaryEdges);
  Serial.print(" close=");
  Serial.print(statTooCloseEdges);
  Serial.print(" early=");
  Serial.print(statEarlyEdges);
  Serial.print(" same=");
  Serial.print(statSameLevelEdges);
  Serial.print(" long_reset=");
  Serial.print(statLongGapResets);
  Serial.print(" silence_reset=");
  Serial.print(statSilenceResets);
  Serial.print(" timing_reset=");
  Serial.print(statTimingResets);
  Serial.print(" sfd=");
  Serial.print(statSfdLocks);
  Serial.print(" parser_err=");
  Serial.print(payloadParser.errorCount);
  Serial.print(" frames=");
  Serial.print((uint32_t)ledGridFrameCounter);
  Serial.print(" ring=");
  Serial.print(ringFill);
  Serial.print(" ring_hi=");
  Serial.print(ringHighWater);
  Serial.print(" isr_drop=");
  Serial.println(isrDrops);
#else
  (void)isrDrops;
  (void)ringFill;
  (void)ringHighWater;
#endif
}
