#include "DecoderTask.h"

#include <Arduino.h>

#include "EdgeRingBuffer.h"
#include "LedDriver.h"
#include "ManchesterPacketDecoder.h"

void decoderCore1Task(void *parameter) {
  (void)parameter;

  EdgeEvent event;
  ManchesterPacketDecoder decoder;
  decoder.begin();
  uint32_t ringHighWater = 0;
  uint32_t lastDiagnosticMs = 0;

  while (true) {
    bool didWork = false;
    const uint32_t ringFill = getEdgeRingFillLevel();
    if (ringFill > ringHighWater) {
      ringHighWater = ringFill;
    }

    while (popEdge(event)) {
      didWork = true;
      decoder.pollRawAudioRgbModeToggle();
      decoder.processEdge(event);
      serviceLedDriver();
    }

    decoder.pollRawAudioRgbModeToggle();
    decoder.pollForSilence();
    serviceLedDriver();

#if ZC_DIAGNOSTICS
    const uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - lastDiagnosticMs) >= ZC_DIAGNOSTIC_INTERVAL_MS) {
      lastDiagnosticMs = nowMs;
      decoder.printDiagnostics(getIsrDropCount(), getEdgeRingFillLevel(), ringHighWater);
      ringHighWater = getEdgeRingFillLevel();
    }
#endif

    if (!didWork) {
      vTaskDelay(1);
    }
  }
}
