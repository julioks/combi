#pragma once

#include <Arduino.h>

// ============================================================================
// Settings
// ============================================================================

// Hardware pin layout follows audiocodec/audiocodec.ino.
static constexpr int CAPTURE_PIN = 21;

// Keep the default conservative for cheap USB-serial bridges and long cables.
// The compact diagnostics are sparse enough for 115200 baud.
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

// LED output driver selection.
#define LED_OUTPUT_DRIVER_SERIAL 1
#define LED_OUTPUT_DRIVER_NEOPIXEL 2

// Serial LED debug output mode.
#define LED_SERIAL_OUTPUT_FULL_FRAME 1
#define LED_SERIAL_OUTPUT_FRAME_NUMBER_ONLY 2

// Change this to LED_OUTPUT_DRIVER_NEOPIXEL when the Adafruit_NeoPixel
// library is installed and the physical LED output is connected.
#ifndef LED_OUTPUT_DRIVER
#define LED_OUTPUT_DRIVER LED_OUTPUT_DRIVER_NEOPIXEL
#endif

// Full-frame text dumps are slow at 115200 baud and can make the decoder fall
// behind. Frame-number-only mode is a lightweight decode progress indicator.
#ifndef LED_SERIAL_OUTPUT_MODE
#define LED_SERIAL_OUTPUT_MODE LED_SERIAL_OUTPUT_FRAME_NUMBER_ONLY
#endif

static constexpr uint8_t LED_PIN = 23;

#define LED_PANEL_PROFILE_16X16_TEST 1
#define LED_PANEL_PROFILE_WEIRD_20X20 2

// Demo panel: weird 20x20 split 10x20 layout.
#ifndef LED_PANEL_PROFILE
#define LED_PANEL_PROFILE LED_PANEL_PROFILE_WEIRD_20X20
#endif

#define LED_DRIVER_LAYOUT_ROW_MAJOR 1
#define LED_DRIVER_LAYOUT_ROW_SERPENTINE 2
#define LED_DRIVER_LAYOUT_COLUMN_MAJOR 3
#define LED_DRIVER_LAYOUT_COLUMN_SERPENTINE 4
#define LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE 5
#define LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y 6

#if LED_PANEL_PROFILE == LED_PANEL_PROFILE_16X16_TEST
static constexpr uint8_t LED_DRIVER_GRID_WIDTH = 16;
static constexpr uint8_t LED_DRIVER_GRID_HEIGHT = 16;
#ifndef LED_DRIVER_LAYOUT
#define LED_DRIVER_LAYOUT LED_DRIVER_LAYOUT_COLUMN_SERPENTINE
#endif
#ifndef LED_DRIVER_MIRROR_X
#define LED_DRIVER_MIRROR_X 0
#endif
#elif LED_PANEL_PROFILE == LED_PANEL_PROFILE_WEIRD_20X20
static constexpr uint8_t LED_DRIVER_GRID_WIDTH = 20;
static constexpr uint8_t LED_DRIVER_GRID_HEIGHT = 20;
#ifndef LED_DRIVER_LAYOUT
#define LED_DRIVER_LAYOUT LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y
#endif
#ifndef LED_DRIVER_MIRROR_X
#define LED_DRIVER_MIRROR_X 1
#endif
#else
#error Unknown LED_PANEL_PROFILE selected.
#endif

#if LED_PANEL_PROFILE != LED_PANEL_PROFILE_WEIRD_20X20 && \
    (LED_DRIVER_LAYOUT == LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE || \
     LED_DRIVER_LAYOUT == LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y)
#error Split 10x20 LED layouts require LED_PANEL_PROFILE_WEIRD_20X20.
#endif

static constexpr uint16_t LED_COUNT = LED_DRIVER_GRID_WIDTH * LED_DRIVER_GRID_HEIGHT;

// Raw audio RGB visualizer.
// Leave this 0 for normal boot. Set to 1 only if you want raw mode at startup.
#ifndef RAW_AUDIO_RGB_MODE
#define RAW_AUDIO_RGB_MODE 0
#endif

// Set this to a GPIO number to enable both modes in one firmware.
// Wire a button from that GPIO to GND, or briefly short the pin to GND.
// Each press/short toggles normal decoder <-> raw audio RGB.
// Leave -1 to disable the button and use RAW_AUDIO_RGB_MODE as a fixed mode.
#ifndef RAW_AUDIO_RGB_TOGGLE_PIN
#define RAW_AUDIO_RGB_TOGGLE_PIN 14
#endif

// Used whenever raw visualizer mode is active. Without an SFD, polarity cannot
// be inferred from the stream, so this flips the raw bit stream when needed.
#ifndef RAW_AUDIO_RGB_INVERT_BITS
#define RAW_AUDIO_RGB_INVERT_BITS 0
#endif

#ifndef RAW_AUDIO_RGB_PUBLISH_FPS
#define RAW_AUDIO_RGB_PUBLISH_FPS 20
#endif

#ifndef RAW_AUDIO_RGB_FADE_STEP
#define RAW_AUDIO_RGB_FADE_STEP 8
#endif

#if RAW_AUDIO_RGB_PUBLISH_FPS <= 0
#error RAW_AUDIO_RGB_PUBLISH_FPS must be greater than zero
#endif

#if RAW_AUDIO_RGB_FADE_STEP < 0 || RAW_AUDIO_RGB_FADE_STEP > 255
#error RAW_AUDIO_RGB_FADE_STEP must be between 0 and 255
#endif

#if RAW_AUDIO_RGB_TOGGLE_PIN < -1
#error RAW_AUDIO_RGB_TOGGLE_PIN must be -1 or a valid GPIO number
#endif

static constexpr uint8_t RAW_AUDIO_RGB_WIDTH = LED_DRIVER_GRID_WIDTH;
static constexpr uint8_t RAW_AUDIO_RGB_HEIGHT = LED_DRIVER_GRID_HEIGHT;
static constexpr uint16_t RAW_AUDIO_RGB_PIXEL_COUNT =
    RAW_AUDIO_RGB_WIDTH * RAW_AUDIO_RGB_HEIGHT;
static constexpr uint32_t RAW_AUDIO_RGB_PUBLISH_INTERVAL_US =
    1000000UL / RAW_AUDIO_RGB_PUBLISH_FPS;

// Decoded RGB grid storage.
// 16384 pixels supports up to 128x128 and uses 49152 bytes for RGB data.
// 256x256 would need 196608 bytes for RGB data alone, which is too close to
// the available ESP32 RAM once task stacks and runtime allocations are included.
static constexpr uint32_t MAX_GRID_PIXELS = 2048;

// Edges separated by this much silence end the current packet.
static constexpr uint32_t SILENCE_RESET_US = 1000000; // 1 s

// Used before calibration is ready.
// For a 22 kHz preamble edge rate, the preamble edge gap is about 45 us.
static constexpr uint32_t MIN_PREAMBLE_GAP_US = 8;
// Allow slower tape-friendly rates too; 10000 us supports bit rates down to
// about 100 bps while still requiring a long alternating run and SFD to lock.
static constexpr uint32_t MAX_PREAMBLE_GAP_US = 10000;

// How much preamble timing is needed before trusting the Manchester phase.
static constexpr uint8_t MIN_PERIOD_SAMPLES = 8;

// Require a decent alternating run before accepting the repeated SFD bit.
// 24 bits = 3 bytes of 0x55 preamble minimum.
static constexpr uint16_t MIN_ALTERNATING_BITS_BEFORE_SFD = 24;

// Manchester edge classification.
// Too-close transition: shorter than any plausible tape drift or Manchester edge.
// Boundary transition: about 0.5 bit from last mid-bit transition.
// Mid-bit transition: about 1.0 bit from last mid-bit transition. Tape
// playback can pull real mid-bit edges early, especially at higher bit rates.
// Only relax that lower bound when no boundary edge was already seen in the
// same bit cell; after a boundary, an early transition is more likely chatter.
static constexpr uint32_t EDGE_TOO_CLOSE_NUM = 1; // 1/3 bit period
static constexpr uint32_t EDGE_TOO_CLOSE_DEN = 3;
static constexpr uint32_t BOUNDARY_MIN_NUM = 1; // 1/3 bit period
static constexpr uint32_t BOUNDARY_MIN_DEN = 3;
static constexpr uint32_t BOUNDARY_MAX_NUM = 2; // 2/3 bit period
static constexpr uint32_t BOUNDARY_MAX_DEN = 3;
static constexpr uint32_t MID_MIN_NO_BOUNDARY_NUM = 2; // 2/3 bit period
static constexpr uint32_t MID_MIN_NO_BOUNDARY_DEN = 3;
static constexpr uint32_t MID_MIN_AFTER_BOUNDARY_NUM = 3; // 3/4 bit period
static constexpr uint32_t MID_MIN_AFTER_BOUNDARY_DEN = 4;
// Tolerate late mid-bit edges from tape timing skew. Bad chunks are expected
// to be dropped and reacquired by the guarded resyncs.
static constexpr uint32_t MID_MAX_NUM = 3; // 3/2 bit period
static constexpr uint32_t MID_MAX_DEN = 2;

// Boundary edges are useful phase markers, but on tape they are more distorted
// than mid-bit data edges. Leave them out of the period PLL by default so a
// squeezed boundary cannot drag the bit estimate toward the unstable side.
#ifndef ZC_TRACK_BOUNDARY_PERIOD
#define ZC_TRACK_BOUNDARY_PERIOD 0
#endif

// Zero-cross diagnostics are aggregated and printed from the decoder task,
// never from the ISR.
#ifndef ZC_DIAGNOSTICS
#define ZC_DIAGNOSTICS 1
#endif

#ifndef ZC_DIAGNOSTIC_INTERVAL_MS
#define ZC_DIAGNOSTIC_INTERVAL_MS 500
#endif

// Optional experiment: if exactly one mid-bit edge appears to be missing, the
// decoder can insert an inferred raw bit before classifying the current edge.
// Leave this off by default; guarded resyncs are safer than guessed bits until
// logs show true isolated misses rather than tape timing skew.
#ifndef ZC_MISSED_MID_RECOVERY
#define ZC_MISSED_MID_RECOVERY 0
#endif

// Capture buffer.
// 8192 events gives about 372 ms of buffer at 22,000 edges/sec.
static constexpr uint32_t RING_BITS = 13;
static constexpr uint32_t RING_SIZE = 1UL << RING_BITS;
static constexpr uint32_t RING_MASK = RING_SIZE - 1;
