#pragma once

#include <Arduino.h>

#include "Config.h"
#include "EdgeEvent.h"
#include "LedProtocolParser.h"
#include "RawAudioRgbFramePusher.h"

struct ManchesterPacketDecoder {
  enum Mode : uint8_t {
    SEARCH_PREAMBLE_AND_SFD = 0,
    DISCARD_SFD_REMAINDER = 1,
    READ_DATA = 2
  };

  Mode mode = SEARCH_PREAMBLE_AND_SFD;

  bool haveAnyEdge = false;
  uint32_t lastEdgeUs = 0;
  uint8_t lastLevel = 0;

  bool haveLastMid = false;
  uint32_t lastMidUs = 0;

  // Q8 fixed-point bit period estimate.
  uint32_t bitPeriodQ8 = 0;
  uint8_t periodSamples = 0;

  bool havePrevMidBit = false;
  uint8_t prevMidBit = 0;
  uint16_t alternatingRun = 0;

  bool activeInvert = false;
  uint8_t sfdBitsLeftToDiscard = 0;

  LedProtocolParser payloadParser;
  RawAudioRgbFramePusher rawFramePusher;

  bool rawAudioRgbModeActive = RAW_AUDIO_RGB_MODE != 0;
  bool rawToggleLastReadingActive = false;
  bool rawToggleStableActive = false;
  uint32_t rawToggleLastChangeMs = 0;

  void begin();
  bool rawAudioRgbModeEnabled() const;
  bool readRawAudioRgbTogglePinActive() const;
  void pollRawAudioRgbModeToggle();
  void setRawAudioRgbMode(bool enabled);
  void resetPacketDecoderOnly();
  void resetPayloadConsumer();
  void resetForNextPacket();
  void finishPacketBecauseOfSilence();
  bool timingReady() const;
  uint32_t bitPeriodUs() const;
  void updatePreamblePeriod(uint32_t gapUs);
  void updateBoundaryPeriod(uint32_t boundaryGapUs);
  void updateMidPeriod(uint32_t midGapUs);
  void startSfdDiscard(uint8_t repeatedRawBit);
  void feedPreambleSearchBit(uint8_t rawBit);
  void feedSfdDiscardBit(uint8_t rawBit);
  void feedDataBit(uint8_t rawBit);
  void feedMidBit(uint8_t rawBit);
  void acceptMidBitEdge(const EdgeEvent &event);
  void treatEdgeAsFirstMidBit(const EdgeEvent &event);
  void resetTimingAndSearchFromThisEdge(const EdgeEvent &event);
  void processEdge(const EdgeEvent &event);
  void pollForSilence();
};
