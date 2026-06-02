# ESP32 Manchester LED Frame Decoder

This project decodes a Manchester-encoded LED frame stream on an ESP32 and
renders the decoded RGB pixels to either a NeoPixel matrix or Serial. It was
built around a "Tape Thingy" style audio/data signal: an alternating preamble,
a start delimiter, a packed LED payload, and silence before the next packet.

The repository also includes browser tools for drawing/importing frames and
generating matching Manchester WAV files.

## Current Capabilities

- Captures input transitions from a comparator on ESP32 GPIO27.
- Buffers edge events in an interrupt-safe ring buffer.
- Runs capture on core 0 and Manchester decoding/LED servicing on core 1.
- Learns bit timing from a `0x55` preamble.
- Detects the `0xD5` start delimiter from the structural break in the
  alternating preamble.
- Decodes payload bits MSB-first.
- Supports direct RGB frames and indexed palette frames.
- Streams repeated frames from one packet until silence resets the decoder.
- Supports guarded in-band resync chunks that resend sync, header, and palette
  data without requiring a long silence reset.
- Can render to a 16x16 NeoPixel matrix or print decoded frames over Serial.
- Includes an optional raw visualizer mode that treats decoded bits as a
  continuous 16x16 RGB byte stream.

## Signal Format

Each normal packet is expected to look like this:

1. Silence
2. Repeated `0x55` preamble bytes
3. `0xD5` start delimiter
4. Payload bits
5. Silence/reset before the next packet

The observed stream is handled as MSB-first:

- `0x55` preamble: `01010101`
- `0xD5` delimiter: `11010101`

The decoder locks when the alternating preamble produces the repeated bit at
the start of `0xD5`. It then discards the remaining delimiter bits and forwards
payload bits to the LED protocol parser.

## Payload Format

Every payload starts with a three-byte header:

- width byte
- height byte
- bits-per-pixel byte

Width or height value `0` is treated as `1`.

`bpp = 0` means direct RGB:

- Each pixel is three bytes: red, green, blue.
- Frames repeat immediately after the previous frame's final blue byte.

`bpp = 1..8` means indexed color:

- The initial palette has `2^bpp` RGB entries.
- Each frame starts with one palette-change bit.
- If that bit is `1`, an 8-bit update count follows.
- Each palette update is `palette_index, red, green, blue`.
- Pixel indexes are packed MSB-first with no padding between frames.

The browser encoders can split the stream into guarded resync chunks every N
frames. Each chunk starts with a short invalid Manchester hold, then a fresh
`0x55` preamble, `0xD5` delimiter, header, full palette when indexed, and that
chunk's frames. The decoder treats the invalid Manchester gap as timing loss,
resets the payload parser, and reacquires timing from the following preamble.
Normal tape drift correction still runs on valid boundary and mid-bit edges.

The ESP32 decoder currently caps decoded grid storage at `MAX_GRID_PIXELS`
(`2048` pixels in `Config.h`). The NeoPixel output driver displays onto a
16x16 physical grid, cropping oversized decoded frames and leaving missing
pixels off.

## Hardware Defaults

- Comparator output: ESP32 GPIO27
- NeoPixel data output: ESP32 GPIO12
- Physical LED matrix: 16x16
- Default LED layout: column-major serpentine
- Serial baud: 115200

The comparator output must be 0 to 3.3 V. Connect comparator ground and ESP32
ground together.

## Configuration

Most tuning lives in `Config.h`.

Important options:

- `LED_OUTPUT_DRIVER`: selects `LED_OUTPUT_DRIVER_NEOPIXEL` or
  `LED_OUTPUT_DRIVER_SERIAL`.
- `LED_SERIAL_OUTPUT_MODE`: selects frame-number-only Serial output or full
  frame dumps.
- `LED_DRIVER_LAYOUT`: selects row-major, row-serpentine, column-major, or
  column-serpentine physical LED mapping.
- `RAW_AUDIO_RGB_TOGGLE_PIN`: set this to a GPIO to enable both normal packet
  decoding and raw audio RGB in one firmware. Wire a button from that pin to
  GND, or briefly short it to GND; each press toggles modes.
- `RAW_AUDIO_RGB_MODE`: startup default only. Leave it `0` for normal boot
  when using `RAW_AUDIO_RGB_TOGGLE_PIN`.
- `RAW_AUDIO_RGB_INVERT_BITS`: flips raw visualizer bits when needed.
- `RAW_AUDIO_RGB_PUBLISH_FPS` and `RAW_AUDIO_RGB_FADE_STEP`: control raw
  visualizer refresh and fading.

The default output driver is NeoPixel. If the Adafruit NeoPixel library is not
installed or no panel is connected, switch `LED_OUTPUT_DRIVER` to
`LED_OUTPUT_DRIVER_SERIAL` for debugging.

## Browser Encoders

Open either HTML file directly in a browser:

- `encoder/index.html`: frame editor and Manchester WAV generator. Supports
  direct RGB, indexed palettes, per-frame palette updates, JSON import/export,
  waveform preview, playback, WAV download, and WS2812-aware palette matching.
- `encoder-video/index.html`: media-to-frame encoder. Imports images, GIFs,
  and videos, converts them to grid frames, supports fitting, palette strategy,
  color mood, LED-output-space palette matching/dithering, and generates
  Manchester WAV output.

Both tools can generate the normal protocol the ESP32 decoder expects:
silence, `0x55` preamble, `0xD5` sync byte, then packed payload bits. Keep the
sync option enabled for normal decoder mode. Use `Resync every N frames` to
repeat the sync/header/palette in guarded chunks; `0` keeps the old single
continuous packet behavior.

## Project Structure

- `decoder.ino`: Arduino setup, Serial setup, LED setup, Wi-Fi/Bluetooth
  shutdown, and FreeRTOS task creation.
- `Config.h`: pins, output mode, LED layout, timing windows, buffer sizes, and
  raw visualizer settings.
- `CaptureTask.*`: GPIO interrupt setup and edge capture.
- `EdgeRingBuffer.*`: ring buffer shared by capture and decode code.
- `ManchesterPacketDecoder.*`: timing recovery, Manchester edge
  classification, preamble/SFD detection, polarity handling, and silence reset.
- `LedProtocolParser.*`: normal packet payload parser for RGB and indexed
  frames.
- `RawAudioRgbFramePusher.*`: optional raw 16x16 RGB visualizer path.
- `LedFrameGrid.*`: decoded RGB grid storage and frame-ready signaling.
- `LedDriver.*`: common output-driver dispatch.
- `LedNeoPixelDriver.*`: Adafruit NeoPixel matrix output.
- `LedSerialDriver.*`: Serial debug output.
- `encoder/`: browser frame editor and WAV generator.
- `encoder-video/`: browser media/video importer and WAV generator.

## Build Notes

This is an Arduino ESP32 sketch. Build/upload it with the Arduino IDE, Arduino
CLI, or another ESP32 Arduino-compatible workflow.

For NeoPixel output, install the `Adafruit_NeoPixel` library. For Serial-only
debugging, set:

```cpp
#define LED_OUTPUT_DRIVER LED_OUTPUT_DRIVER_SERIAL
```

before the default in `Config.h`, or pass the define through your build system.

## Current Status

This is still an experimental decoder. The current code is focused on robust
edge ingestion, Manchester timing recovery, packet alignment, and practical LED
rendering on ESP32. The active implementation is tuned for the observed
MSB-first `0x55` preamble and `0xD5` delimiter behavior.
