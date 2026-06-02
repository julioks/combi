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

  while (true) {
    bool didWork = false;

    while (popEdge(event)) {
      didWork = true;
      decoder.pollRawAudioRgbModeToggle();
      decoder.processEdge(event);
      serviceLedDriver();
    }

    decoder.pollRawAudioRgbModeToggle();
    decoder.pollForSilence();
    serviceLedDriver();

    if (!didWork) {
      vTaskDelay(1);
    }
  }
}
