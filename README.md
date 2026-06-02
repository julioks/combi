# combi

ESP32 sketches for audio-carried LED data and audio-reactive VU visualizers.

## Projects

- `decoder/`: Manchester/zerocrossing LED frame decoder with browser frame and
  media encoders. The existing video payload format is kept intact.
- `audiocodec/`: PCM1861/I2S audio analyzer plus combined firmware scaffolding
  for video zerocrossing mode, VU-load mode, and raw-audio VU rendering.

## Modes

The combined `audiocodec` firmware cycles modes with a button to ground:

1. `video-zerocross`: decode and render the existing Manchester video frame
   stream.
2. `vu-load`: receive RAM-only visualizer layers and palettes over a separate
   VU packet format.
3. `raw-audio`: render loaded VU layers from the codec audio analyzer output,
   falling back to mode 0 spectrum if nothing is loaded.

See `audiocodec/VU_PROTOCOL.md` for the VU-load packet format.

## Notes

Large local media samples and bundled tooling are intentionally ignored by Git.
Open the HTML encoder files directly in a browser when generating WAV payloads.
