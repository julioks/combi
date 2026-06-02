#pragma once

#include <Arduino.h>

#include "../video/VideoFrameReceiver.h"
#include "../vu/VuLoadPacketParser.h"

static constexpr uint32_t ZC_SILENCE_RESET_US = 1000000UL;
static constexpr uint32_t ZC_MIN_PREAMBLE_GAP_US = 8;
static constexpr uint32_t ZC_MAX_PREAMBLE_GAP_US = 10000;
static constexpr uint8_t ZC_MIN_PERIOD_SAMPLES = 8;
static constexpr uint16_t ZC_MIN_ALTERNATING_BITS_BEFORE_SFD = 24;
static constexpr uint32_t ZC_EDGE_TOO_CLOSE_NUM = 1;
static constexpr uint32_t ZC_EDGE_TOO_CLOSE_DEN = 3;
static constexpr uint32_t ZC_BOUNDARY_MIN_NUM = 1;
static constexpr uint32_t ZC_BOUNDARY_MIN_DEN = 3;
static constexpr uint32_t ZC_BOUNDARY_MAX_NUM = 2;
static constexpr uint32_t ZC_BOUNDARY_MAX_DEN = 3;
static constexpr uint32_t ZC_MID_MIN_NUM = 3;
static constexpr uint32_t ZC_MID_MIN_DEN = 4;
static constexpr uint32_t ZC_MID_MAX_NUM = 3;
static constexpr uint32_t ZC_MID_MAX_DEN = 2;

struct ZeroCrossEdgeEvent {
  uint32_t t_us;
  uint8_t level;
};

class ZeroCrossManchesterDecoder {
public:
  enum SinkMode : uint8_t {
    SINK_VIDEO = 0,
    SINK_VU_LOAD = 1
  };

  void begin(VideoFrameReceiver* videoReceiver, VuLoadPacketParser* vuReceiver) {
    video = videoReceiver;
    vu = vuReceiver;
    resetForNextPacket();
  }

  void setSinkMode(SinkMode nextMode) {
    sinkMode = nextMode;
    resetForNextPacket();
  }

  void resetForNextPacket() {
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

  void processEdge(const ZeroCrossEdgeEvent& event) {
    if (!haveAnyEdge) {
      haveAnyEdge = true;
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      treatEdgeAsFirstMidBit(event);
      return;
    }

    const uint32_t gapFromLastEdge = event.t_us - lastEdgeUs;
    if (gapFromLastEdge >= ZC_SILENCE_RESET_US) {
      resetForNextPacket();
      haveAnyEdge = true;
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      treatEdgeAsFirstMidBit(event);
      return;
    }

    if (event.level == lastLevel) {
      resetTimingAndSearchFromThisEdge(event);
      return;
    }

    if (!timingReady()) {
      if (gapFromLastEdge < ZC_MIN_PREAMBLE_GAP_US) {
        lastEdgeUs = event.t_us;
        lastLevel = event.level;
        return;
      }

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
    const uint32_t tooCloseMax = (bitUs * ZC_EDGE_TOO_CLOSE_NUM) / ZC_EDGE_TOO_CLOSE_DEN;
    const uint32_t boundaryMin = (bitUs * ZC_BOUNDARY_MIN_NUM) / ZC_BOUNDARY_MIN_DEN;
    const uint32_t boundaryMax = (bitUs * ZC_BOUNDARY_MAX_NUM) / ZC_BOUNDARY_MAX_DEN;
    const uint32_t midMin = (bitUs * ZC_MID_MIN_NUM) / ZC_MID_MIN_DEN;
    const uint32_t midMax = (bitUs * ZC_MID_MAX_NUM) / ZC_MID_MAX_DEN;

    if (gapFromLastMid < tooCloseMax) {
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      return;
    }

    if (gapFromLastMid >= boundaryMin && gapFromLastMid <= boundaryMax) {
      updateBoundaryPeriod(gapFromLastMid);
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      return;
    }

    if (gapFromLastMid < midMin) {
      lastEdgeUs = event.t_us;
      lastLevel = event.level;
      return;
    }

    if (gapFromLastMid > midMax) {
      resetTimingAndSearchFromThisEdge(event);
      return;
    }

    updateMidPeriod(gapFromLastMid);
    lastMidUs = event.t_us;
    acceptMidBitEdge(event);
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
  }

  void pollForSilence() {
    if (!haveAnyEdge) {
      return;
    }

    const uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastEdgeUs) >= ZC_SILENCE_RESET_US) {
      resetForNextPacket();
    }
  }

private:
  enum Mode : uint8_t {
    SEARCH_PREAMBLE_AND_SFD = 0,
    DISCARD_SFD_REMAINDER = 1,
    READ_DATA = 2
  };

  VideoFrameReceiver* video = nullptr;
  VuLoadPacketParser* vu = nullptr;
  SinkMode sinkMode = SINK_VIDEO;
  Mode mode = SEARCH_PREAMBLE_AND_SFD;

  bool haveAnyEdge = false;
  uint32_t lastEdgeUs = 0;
  uint8_t lastLevel = 0;
  bool haveLastMid = false;
  uint32_t lastMidUs = 0;
  uint32_t bitPeriodQ8 = 0;
  uint8_t periodSamples = 0;
  bool havePrevMidBit = false;
  uint8_t prevMidBit = 0;
  uint16_t alternatingRun = 0;
  bool activeInvert = false;
  uint8_t sfdBitsLeftToDiscard = 0;

  void resetPacketDecoderOnly() {
    mode = SEARCH_PREAMBLE_AND_SFD;
    havePrevMidBit = false;
    prevMidBit = 0;
    alternatingRun = 0;
    activeInvert = false;
    sfdBitsLeftToDiscard = 0;
  }

  void resetPayloadConsumer() {
    if (sinkMode == SINK_VIDEO && video != nullptr) {
      video->reset();
    } else if (sinkMode == SINK_VU_LOAD && vu != nullptr) {
      vu->reset();
    }
  }

  bool timingReady() const {
    return periodSamples >= ZC_MIN_PERIOD_SAMPLES && bitPeriodQ8 > 0;
  }

  uint32_t bitPeriodUs() const {
    return bitPeriodQ8 == 0 ? 0 : bitPeriodQ8 >> 8;
  }

  void updatePreamblePeriod(uint32_t gapUs) {
    if (gapUs < ZC_MIN_PREAMBLE_GAP_US || gapUs > ZC_MAX_PREAMBLE_GAP_US) {
      return;
    }

    const uint32_t sampleQ8 = gapUs << 8;
    bitPeriodQ8 = bitPeriodQ8 == 0
      ? sampleQ8
      : ((bitPeriodQ8 * 7UL) + sampleQ8) >> 3;

    if (periodSamples < 255) {
      periodSamples++;
    }
  }

  void updateBoundaryPeriod(uint32_t boundaryGapUs) {
    const uint32_t sampleQ8 = boundaryGapUs << 9;
    if (bitPeriodQ8 == 0) {
      bitPeriodQ8 = sampleQ8;
      periodSamples = 1;
      return;
    }
    bitPeriodQ8 = ((bitPeriodQ8 * 31UL) + sampleQ8) >> 5;
  }

  void updateMidPeriod(uint32_t midGapUs) {
    const uint32_t sampleQ8 = midGapUs << 8;
    if (bitPeriodQ8 == 0) {
      bitPeriodQ8 = sampleQ8;
      periodSamples = 1;
      return;
    }
    bitPeriodQ8 = ((bitPeriodQ8 * 15UL) + sampleQ8) >> 4;
  }

  void startSfdDiscard(uint8_t repeatedRawBit) {
    activeInvert = repeatedRawBit == 0;
    mode = DISCARD_SFD_REMAINDER;
    sfdBitsLeftToDiscard = 7;
  }

  void feedPreambleSearchBit(uint8_t rawBit) {
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

    if (timingReady() && alternatingRun >= ZC_MIN_ALTERNATING_BITS_BEFORE_SFD) {
      startSfdDiscard(rawBit);
      prevMidBit = rawBit;
      alternatingRun = 0;
      return;
    }

    prevMidBit = rawBit;
    alternatingRun = 1;
  }

  void feedSfdDiscardBit(uint8_t rawBit) {
    (void)rawBit;
    if (sfdBitsLeftToDiscard > 0) {
      sfdBitsLeftToDiscard--;
    }
    if (sfdBitsLeftToDiscard == 0) {
      mode = READ_DATA;
    }
  }

  void feedDataBit(uint8_t rawBit) {
    const uint8_t bit = activeInvert ? (rawBit ^ 1) : rawBit;
    if (sinkMode == SINK_VIDEO && video != nullptr) {
      video->feedBit(bit);
    } else if (sinkMode == SINK_VU_LOAD && vu != nullptr) {
      vu->feedBit(bit);
    }
  }

  void feedMidBit(uint8_t rawBit) {
    if (mode == SEARCH_PREAMBLE_AND_SFD) {
      feedPreambleSearchBit(rawBit);
    } else if (mode == DISCARD_SFD_REMAINDER) {
      feedSfdDiscardBit(rawBit);
    } else {
      feedDataBit(rawBit);
    }
  }

  void acceptMidBitEdge(const ZeroCrossEdgeEvent& event) {
    feedMidBit(event.level ? 1 : 0);
  }

  void treatEdgeAsFirstMidBit(const ZeroCrossEdgeEvent& event) {
    haveLastMid = true;
    lastMidUs = event.t_us;
    acceptMidBitEdge(event);
  }

  void resetTimingAndSearchFromThisEdge(const ZeroCrossEdgeEvent& event) {
    resetPacketDecoderOnly();
    resetPayloadConsumer();
    haveLastMid = false;
    bitPeriodQ8 = 0;
    periodSamples = 0;
    lastEdgeUs = event.t_us;
    lastLevel = event.level;
    treatEdgeAsFirstMidBit(event);
  }
};

