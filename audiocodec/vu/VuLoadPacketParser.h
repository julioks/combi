#pragma once

#include "VuProgramStore.h"

static constexpr uint16_t VU_MAX_PACKET_PAYLOAD_BYTES = VU_MAX_PROGRAM_BYTES + 8;

class VuLoadPacketParser {
public:
  void begin(VuProgramStore* programStore) {
    store = programStore;
    reset();
  }

  void reset() {
    state = READ_MAGIC_0;
    bitBuffer = 0;
    bitCount = 0;
    packetVersion = 0;
    packetKind = 0;
    payloadLength = 0;
    payloadCursor = 0;
  }

  void feedBit(uint8_t bit) {
    bitBuffer = (uint8_t)((bitBuffer << 1) | (bit & 0x01));
    bitCount++;

    if (bitCount < 8) {
      return;
    }

    feedByte(bitBuffer);
    bitBuffer = 0;
    bitCount = 0;
  }

private:
  enum State : uint8_t {
    READ_MAGIC_0,
    READ_MAGIC_1,
    READ_VERSION,
    READ_KIND,
    READ_LENGTH_HI,
    READ_LENGTH_LO,
    READ_PAYLOAD
  };

  VuProgramStore* store = nullptr;
  State state = READ_MAGIC_0;
  uint8_t bitBuffer = 0;
  uint8_t bitCount = 0;
  uint8_t packetVersion = 0;
  uint8_t packetKind = 0;
  uint16_t payloadLength = 0;
  uint16_t payloadCursor = 0;
  uint8_t payload[VU_MAX_PACKET_PAYLOAD_BYTES];

  void feedByte(uint8_t value) {
    switch (state) {
      case READ_MAGIC_0:
        if (value == VU_PACKET_MAGIC_0) {
          state = READ_MAGIC_1;
        }
        return;

      case READ_MAGIC_1:
        if (value == VU_PACKET_MAGIC_1) {
          state = READ_VERSION;
        } else {
          state = value == VU_PACKET_MAGIC_0 ? READ_MAGIC_1 : READ_MAGIC_0;
        }
        return;

      case READ_VERSION:
        packetVersion = value;
        state = READ_KIND;
        return;

      case READ_KIND:
        packetKind = value;
        state = READ_LENGTH_HI;
        return;

      case READ_LENGTH_HI:
        payloadLength = (uint16_t)value << 8;
        state = READ_LENGTH_LO;
        return;

      case READ_LENGTH_LO:
        payloadLength |= value;
        payloadCursor = 0;
        if (payloadLength > VU_MAX_PACKET_PAYLOAD_BYTES || packetVersion != VU_PACKET_VERSION) {
          Serial.println("vu packet error");
          reset();
          return;
        }
        if (payloadLength == 0) {
          finishPacket();
          return;
        }
        state = READ_PAYLOAD;
        return;

      case READ_PAYLOAD:
        if (payloadCursor < VU_MAX_PACKET_PAYLOAD_BYTES) {
          payload[payloadCursor++] = value;
        }
        if (payloadCursor >= payloadLength) {
          finishPacket();
        }
        return;
    }
  }

  void finishPacket() {
    bool ok = false;

    if (store != nullptr) {
      if (packetKind == VU_PACKET_LAYER) {
        ok = store->loadLayerPayload(payload, payloadLength);
      } else if (packetKind == VU_PACKET_PALETTE) {
        ok = store->loadPalettePayload(payload, payloadLength);
      } else if (packetKind == VU_PACKET_CLEAR) {
        store->clear();
        ok = true;
      }
    }

    Serial.println(ok ? "vu packet ok" : "vu packet error");
    reset();
  }
};

