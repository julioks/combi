#pragma once

#include "VuProgram.h"

class VuProgramStore {
public:
  void begin() {
    clear();
  }

  void clear() {
    layerCount = 0;
    for (uint8_t i = 0; i < VU_MAX_LAYERS; i++) {
      layers[i].loaded = false;
      layers[i].byteCount = 0;
      layers[i].frameProgramLength = 0;
      layers[i].pixelProgramLength = 0;
      layers[i].palette.loaded = false;
    }
  }

  bool hasLayers() const {
    return layerCount > 0;
  }

  uint8_t count() const {
    return layerCount;
  }

  const VuLayerProgram& layer(uint8_t index) const {
    return layers[index];
  }

  bool loadLayerPayload(const uint8_t* payload, uint16_t length) {
    if (layerCount >= VU_MAX_LAYERS || payload == nullptr || length < 5) {
      return false;
    }

    const uint8_t flags = payload[0];
    const uint16_t frameLength = vuReadU16(payload + 1);
    const uint16_t pixelLength = vuReadU16(payload + 3);
    const uint32_t totalProgramBytes = (uint32_t)frameLength + (uint32_t)pixelLength;

    if (totalProgramBytes > VU_MAX_PROGRAM_BYTES || (uint32_t)length < 5UL + totalProgramBytes) {
      return false;
    }

    VuLayerProgram& target = layers[layerCount];
    target.loaded = true;
    target.flags = flags;
    target.frameProgramOffset = 0;
    target.frameProgramLength = frameLength;
    target.pixelProgramOffset = frameLength;
    target.pixelProgramLength = pixelLength;
    target.byteCount = (uint16_t)totalProgramBytes;
    target.palette.loaded = false;

    for (uint16_t i = 0; i < target.byteCount; i++) {
      target.bytes[i] = payload[5 + i];
    }

    layerCount++;
    return true;
  }

  bool loadPalettePayload(const uint8_t* payload, uint16_t length) {
    if (layerCount == 0 || payload == nullptr || length < 2) {
      return false;
    }

    VuPaletteInterpolation interpolation = payload[0] == VU_PALETTE_STEP
      ? VU_PALETTE_STEP
      : VU_PALETTE_LINEAR;
    uint8_t stopCount = payload[1];
    if (stopCount == 0 || stopCount > VU_MAX_PALETTE_STOPS) {
      return false;
    }

    const uint16_t needed = 2U + (uint16_t)stopCount * 4U;
    if (length < needed) {
      return false;
    }

    VuPaletteProgram& target = layers[layerCount - 1].palette;
    target.loaded = true;
    target.interpolation = interpolation;
    target.stopCount = stopCount;

    for (uint8_t i = 0; i < stopCount; i++) {
      const uint16_t offset = 2U + (uint16_t)i * 4U;
      target.stops[i].position = payload[offset];
      target.stops[i].r = payload[offset + 1];
      target.stops[i].g = payload[offset + 2];
      target.stops[i].b = payload[offset + 3];
    }

    sortPaletteStops(target);
    return true;
  }

private:
  VuLayerProgram layers[VU_MAX_LAYERS];
  uint8_t layerCount = 0;

  void sortPaletteStops(VuPaletteProgram& palette) {
    for (uint8_t i = 1; i < palette.stopCount; i++) {
      VuPaletteStop current = palette.stops[i];
      int8_t j = (int8_t)i - 1;
      while (j >= 0 && palette.stops[j].position > current.position) {
        palette.stops[j + 1] = palette.stops[j];
        j--;
      }
      palette.stops[j + 1] = current;
    }
  }
};

