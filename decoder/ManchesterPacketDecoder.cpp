#include "ManchesterPacketDecoder.h"

#include "Config.h"

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

  bitPeriodQ8 = 0;
  periodSamples = 0;

  resetPayloadConsumer();
}

void ManchesterPacketDecoder::finishPacketBecauseOfSilence() {
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

  // Payload is forwarded bit-by-bit so indexed LED frames do not need any
  // sender-side byte padding between frame fields.
  payloadParser.feedBit(bit);
}

void ManchesterPacketDecoder::acceptMidBitEdge(const EdgeEvent &event) {
  const uint8_t rawBit = event.level ? 1 : 0;
  feedMidBit(rawBit);
}

void ManchesterPacketDecoder::treatEdgeAsFirstMidBit(const EdgeEvent &event) {
  haveLastMid = true;
  lastMidUs = event.t_us;
  acceptMidBitEdge(event);
}

void ManchesterPacketDecoder::resetTimingAndSearchFromThisEdge(const EdgeEvent &event) {
  resetPacketDecoderOnly();
  // Packet mode needs a fresh payload parser after an impossible Manchester
  // gap so guarded resync chunks can reacquire cleanly. Raw visualizer mode is
  // intentionally continuous, so keep its byte/pixel cursor moving across
  // timing reacquisition instead of jumping back to pixel 0.
  if (!rawAudioRgbModeActive) {
    resetPayloadConsumer();
  }
  haveLastMid = false;
  bitPeriodQ8 = 0;
  periodSamples = 0;

  lastEdgeUs = event.t_us;
  lastLevel = event.level;
  treatEdgeAsFirstMidBit(event);
}

void ManchesterPacketDecoder::processEdge(const EdgeEvent &event) {
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

  if (event.level == lastLevel) {
    // CHANGE interrupts should alternate levels. If this happens, assume noise/loss and restart search.
    resetTimingAndSearchFromThisEdge(event);
    return;
  }

  if (!timingReady()) {
    if (gapFromLastEdge < MIN_PREAMBLE_GAP_US) {
      // Too fast to be tape drift or a valid Manchester transition.
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      return;
    }

    // During 0x55 preamble, valid mid-bit edges arrive roughly one bit period apart.
    updatePreamblePeriod(gapFromLastEdge);

    lastEdgeUs = event.t_us;
    lastLevel = event.level;

    treatEdgeAsFirstMidBit(event);
    return;
  }

  const uint32_t bitUs = bitPeriodUs();

  if (bitUs == 0) {
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  const uint32_t gapFromLastMid = event.t_us - lastMidUs;
  const uint32_t tooCloseMax = (bitUs * EDGE_TOO_CLOSE_NUM) / EDGE_TOO_CLOSE_DEN;
  const uint32_t boundaryMin = (bitUs * BOUNDARY_MIN_NUM) / BOUNDARY_MIN_DEN;
  const uint32_t boundaryMax = (bitUs * BOUNDARY_MAX_NUM) / BOUNDARY_MAX_DEN;
  const uint32_t midMin = (bitUs * MID_MIN_NUM) / MID_MIN_DEN;
  const uint32_t midMax = (bitUs * MID_MAX_NUM) / MID_MAX_DEN;

  if (gapFromLastMid < tooCloseMax) {
    // Too fast to be tape drift, a bit boundary, or a mid-bit transition.
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  if (gapFromLastMid >= boundaryMin && gapFromLastMid <= boundaryMax) {
    // Manchester boundary transition between two equal data bits.
    // Real edge, but not the bit-value transition, so track timing only.
    updateBoundaryPeriod(gapFromLastMid);
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  if (gapFromLastMid < midMin) {
    // Between the valid boundary and mid-bit windows. Keep physical edge state,
    // but do not let an implausible interval pull the timing estimate around.
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    return;
  }

  if (gapFromLastMid > midMax) {
    // We lost timing, but this is not silence. A guarded resync chunk uses this
    // impossible Manchester gap; valid wow/flutter stays inside the timing
    // windows above and keeps updating the period estimate.
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
