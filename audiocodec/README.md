# audiocodec

ESP32 audio-reactive LED matrix firmware for a PCM1861-style I2S audio ADC and
a 32 x 16 WS2812/NeoPixel panel.

It samples stereo audio at 96 kHz, builds a 128-band analysis frame on the
ESP32, and renders the result to a LED matrix through either the existing
zerocrossing video decoder or a RAM-loaded VU bytecode runtime.

## Features

- 96 kHz I2S receive path with 24-bit PCM in 32-bit stereo slots.
- On-device audio analysis with FFT bands, peaks, transients, stereo width,
  waveform capture, spectral centroid, and dominant frequency.
- Existing zerocrossing Manchester video frame format is preserved.
- Separate VU-load packet format for loading visualizer layers and palettes
  into RAM.
- Raw-audio mode renders loaded VU layers from the analyzer output.
- Mode 0 spectrum remains built in as the fallback when no VU layer is loaded.

## Tested Toolchain

This sketch currently builds with:

- Arduino ESP32 core `3.0.4`
- Adafruit NeoPixel `1.15.4`
- Board FQBN: `esp32:esp32:esp32`

Other ESP32 board definitions may work, but the pin choices and I2S MCLK support
should be checked before uploading.

## Hardware

Default wiring is configured near the top of `audiocodec.ino`.

| Signal | ESP32 pin | Notes |
| --- | ---: | --- |
| I2S MCLK | GPIO0 | Classic ESP32 MCLK-capable pin. |
| I2S BCLK | GPIO26 | PCM1861 bit clock. |
| I2S LRCK | GPIO25 | PCM1861 word select. |
| I2S DIN | GPIO35 | PCM1861 data output into ESP32. |
| Zerocross/comparator | GPIO27 | Manchester data input for video or VU load. |
| Mode button | GPIO14 | Momentary button to GND cycles modes. |
| LED data | GPIO23 | WS2812/NeoPixel data output. |

The LED layout defaults to a 32 x 16 column-serpentine matrix with the first
pixel at the bottom left. Adjust `LED_DRIVER_GRID_WIDTH`,
`LED_DRIVER_GRID_HEIGHT`, `LED_LAYOUT`, and `LED_FIRST_PIXEL_IS_BOTTOM_LEFT`
if your panel is wired differently.

Power the LED panel from a supply sized for the number of pixels, tie the LED
and ESP32 grounds together, and use a proper data level shifter for reliable
WS2812 signalling.

## Dependencies

- Arduino IDE or Arduino CLI.
- ESP32 Arduino board package (`esp32:esp32`).
- Adafruit NeoPixel library.

With Arduino CLI installed and available on `PATH`:

```powershell
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"
```

## Build And Upload

Replace `COM3` with the serial port for your ESP32:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32 .
arduino-cli monitor -p COM3 --config baudrate=115200
```

The compile command was last verified successfully with the tested toolchain
listed above.

## Modes

The mode button cycles:

- `video-zerocross`: existing Manchester video frames are decoded and shown.
- `vu-load`: Manchester VU packets are decoded into RAM as layers/palettes.
- `raw-audio`: PCM audio drives the loaded VU layers, or mode 0 spectrum if
  nothing is loaded.

Entering `video-zerocross` clears loaded VU RAM. VU packets are documented in
`VU_PROTOCOL.md`.

Open `vu-encoder/index.html` directly in a browser to generate VU-load WAVs.

## Project Layout

- `audiocodec.ino` contains ESP32 setup, I2S input, zerocrossing mode handling,
  and LED rendering.
- `audio/` contains the analysis frame and FFT/audio feature extraction.
- `zerocross/` contains the shared Manchester decoder used by video and VU load.
- `video/` contains the preserved LED-frame receiver for the existing video
  payload format.
- `vu/` contains the VU packet parser, RAM store, and bytecode runtime.
- `vu-encoder/` contains the browser tool for compiling VU bytecode and
  palettes into Manchester WAV packets.
- `visualiser/` contains shared canvas/palette helpers and the built-in mode 0
  spectrum fallback. The older C++ effect files are no longer the active VU
  mechanism.
