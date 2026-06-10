# VU Load Protocol

VU load mode uses the same zerocrossing Manchester carrier as the existing
video decoder, but the video payload format is not reused or changed. A VU
packet is one complete decoded Manchester payload in VU load mode.

## Mode Behavior

The mode button is wired to `MODE_BUTTON_PIN` and ground. Each press cycles:

1. `video-zerocross`
2. `vu-load`
3. `raw-audio`

Entering `video-zerocross` clears all loaded VU RAM. `vu-load` waits for VU
packets and does not play video. `raw-audio` renders loaded VU layers against
the `AudioAnalysisFrame` from the PCM/I2S analyzer. If no VU layer is loaded,
raw audio falls back to mode 0 spectrum.

## Packet Header

All multi-byte values are big-endian/MSB-first, matching the existing decoder
bit order.

```text
byte 0    'V'
byte 1    'U'
byte 2    version = 1
byte 3    kind
byte 4    payload length high
byte 5    payload length low
bytes 6+  payload
```

Kinds:

- `1`: append one VU layer
- `2`: assign one palette to the latest loaded layer
- `3`: clear all loaded VU layers

There is intentionally no CRC. Length exists only so the ESP can parse RAM
payloads cleanly.

## Layer Payload

```text
byte 0      flags
byte 1..2   frame program length
byte 3..4   pixel program length
bytes 5+    frame program, then pixel program
```

The runtime executes the frame program once per render and the pixel program
once per LED. Each layer has 32 float state slots available to both programs.
Layers append until a clear packet or until entering video mode.

Layer flags:

- `0x01`: persistent trail layer. The layer renders into a retained canvas
  instead of a cleared canvas. State slot `31` controls per-frame trail fade
  as a normalized `0..1` value; invalid or zero values fall back to `0.82`.
  Trail layers share one retained RGB canvas to fit ESP32 DRAM, so the first
  trail layer rendered in a frame supplies the fade value for that frame.

## Palette Payload

Palettes are not indexed LED-frame palettes. They are VU gradients assigned to
the latest loaded layer.

```text
byte 0      interpolation: 0 = linear, 1 = step
byte 1      stop count, 1..16
stops       position, red, green, blue
```

`position` is `0..255` across the palette. Stops are sorted in RAM after load.

## Pixel VM

The ESP contains only a small stack VM plus audio-analysis access. Visualizer
shape should live in the encoded program.

Initial implemented opcodes are defined in `vu/VuProgram.h`. They include:

- constants, X/Y normalized coordinates, time, deterministic random
- per-layer state load/store
- stack helpers: duplicate, drop, swap
- audio features from `AudioAnalysisFrame`
- bands and mono waveform samples
- arithmetic, clamp/wrap, sin/cos, hypotenuse, comparisons, select
- palette sampling and pixel emission

Color values inside the VM are normalized `0..1`; the renderer converts them to
LED output through the existing visualizer brightness correction.
