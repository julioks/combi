#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Upload this sketch by itself to test physical LED order/orientation.
// Edit these values to match the test panel.
static constexpr uint8_t LED_PIN = 23;
static constexpr uint16_t PANEL_WIDTH = 20;
static constexpr uint16_t PANEL_HEIGHT = 20;
static constexpr uint16_t LED_COUNT = PANEL_WIDTH * PANEL_HEIGHT;
static constexpr uint8_t LED_BRIGHTNESS = 32;

// Change to NEO_GRB if the colors look swapped on this test sketch.
static constexpr neoPixelType LED_COLOR_ORDER = NEO_RGB + NEO_KHZ800;

// Set to true for a single pixel walking through raw strip indices.
static constexpr bool WALK_RAW_INDICES = false;
static constexpr uint16_t WALK_STEP_MS = 140;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, LED_COLOR_ORDER);

struct Marker {
  uint16_t index;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Default test: probe row ends and possible 10-column panel halves.
// Report where each color appears, counting physical panel positions however
// is easiest for you.
static const Marker MARKERS[] = {
  {0, 255, 0, 0},        // red
  {9, 0, 255, 0},        // green
  {10, 0, 0, 255},       // blue
  {19, 255, 255, 255},   // white
  {20, 255, 255, 0},     // yellow
  {199, 255, 0, 255},    // magenta
  {200, 0, 255, 255},    // cyan
  {399, 255, 128, 0},    // orange
};

static constexpr uint8_t MARKER_COUNT = sizeof(MARKERS) / sizeof(MARKERS[0]);

uint16_t walkIndex = 0;
uint32_t lastWalkMs = 0;

void drawMarkers() {
  strip.clear();

  for (uint8_t i = 0; i < MARKER_COUNT; i++) {
    const Marker& marker = MARKERS[i];
    if (marker.index < LED_COUNT) {
      strip.setPixelColor(marker.index, strip.Color(marker.r, marker.g, marker.b));
    }
  }

  strip.show();
}

void drawWalkPixel() {
  const uint32_t nowMs = millis();
  if ((uint32_t)(nowMs - lastWalkMs) < WALK_STEP_MS) {
    return;
  }

  lastWalkMs = nowMs;
  strip.clear();
  strip.setPixelColor(walkIndex, strip.Color(255, 255, 255));
  strip.show();

  Serial.print("raw index ");
  Serial.println(walkIndex);

  walkIndex++;
  if (walkIndex >= LED_COUNT) {
    walkIndex = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.clear();
  strip.show();

  Serial.println("LED mapping test");
  Serial.print("LED count: ");
  Serial.println(LED_COUNT);
  Serial.println(WALK_RAW_INDICES ? "Mode: walking raw indices" : "Mode: fixed raw-index markers");

  if (!WALK_RAW_INDICES) {
    drawMarkers();
  }
}

void loop() {
  if (WALK_RAW_INDICES) {
    drawWalkPixel();
  }
}
