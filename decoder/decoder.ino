/*
  ESP32 Manchester LED frame decoder for Tape Thingy signal ingestion

  Signal format:
  - silence
  - 0x55 preamble bytes
  - 0xD5 start delimiter
  - payload bits
  - silence
  - next packet starts again with 0x55 preamble

  Payload format:
  - width byte, height byte, bpp byte
  - width/height value 0 is treated as 1
  - bpp 0 means direct RGB frames: R,G,B per pixel, no palette fields
  - bpp 1..8 means indexed frames:
    - initial palette has 2^bpp RGB entries
    - each frame starts with one palette-change bit
    - if that bit is 1, an 8-bit update count follows, then repeated
      palette_index,R,G,B entries
    - packed pixel indexes are MSB-first and are not padded between frames

  Output:
  - Decoded frames update the global ledGrid RGB array.
  - Selected LED output driver renders the global ledGrid RGB array.
  - Parser errors print "error" and wait for signal silence/reset.

  Why this version exists:
  - Your repeated output 55 46 26 66 16 56 36 76 0E showed the decoder was starting
    at bit 1 of the 0xD5 delimiter and packing the following MSB-first stream as LSB-first.
  - In the observed stream, 0xD5 behaves as MSB-first: 11010101.
  - 0x55 preamble behaves as MSB-first: 01010101.
  - Therefore the preamble break happens at the boundary: ...01010101 11010101
                                                        repeated bits: ^^
  - The first repeated bit is bit 0 of the SFD, not the final bit.
  - This decoder detects that break, discards the remaining 7 SFD bits, then reads payload MSB-first.

  Hardware:
  - Comparator output -> ESP32 GPIO27
  - Comparator GND and ESP32 GND must be connected
  - Comparator output must be 0 to 3.3 V, not 5 V

  Important:
  - Timing-based Manchester decoder.
  - 0x55 preamble calibrates bit period.
  - Manchester boundary transitions are ignored.
  - 0xD5 is detected structurally as the break in the alternating preamble.
*/

#include <Arduino.h>
#include <WiFi.h>

#include "CaptureTask.h"
#include "Config.h"
#include "DecoderTask.h"
#include "LedDriver.h"

// ============================================================================
// Arduino setup/loop
// ============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  setupLedDriver();

  WiFi.mode(WIFI_OFF);
  btStop();

  xTaskCreatePinnedToCore(
    captureCore0Task,
    "captureCore0",
    2048,
    nullptr,
    5,
    nullptr,
    0
  );

  xTaskCreatePinnedToCore(
    decoderCore1Task,
    "decoderCore1",
    4096,
    nullptr,
    3,
    nullptr,
    1
  );
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
