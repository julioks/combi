#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "driver/i2s.h"
#include "soc/gpio_struct.h"
#include <WiFi.h>
#include <stdlib.h>
#include <string.h>

#define I2S_PORT        I2S_NUM_0

#define PIN_I2S_MCLK    0    // Classic ESP32: usually GPIO0/GPIO1/GPIO3 only
#define PIN_I2S_BCLK    26
#define PIN_I2S_LRCK    25
#define PIN_I2S_DIN     32   // input-only GPIO is fine for DOUT -> ESP32

#define SAMPLE_RATE     96000
#define MCLK_HZ         24576000  // 96k * 256

#define ZERO_CROSS_CAPTURE_PIN 21
#define MODE_BUTTON_PIN 14

#define LED_PIN         23   // NeoPixel data output. GPIO23 is output-capable and does not overlap the I2S pins.

static constexpr uint16_t LED_DRIVER_GRID_WIDTH = 20;
static constexpr uint16_t LED_DRIVER_GRID_HEIGHT = 20;
static constexpr uint32_t LED_COUNT_32 = (uint32_t)LED_DRIVER_GRID_WIDTH * (uint32_t)LED_DRIVER_GRID_HEIGHT;
static_assert(LED_COUNT_32 <= 65535UL, "Adafruit_NeoPixel uses 16-bit pixel indexes; use parallel output or another driver for larger panels.");
static constexpr uint16_t LED_COUNT = (uint16_t)LED_COUNT_32;
static constexpr uint8_t LED_BRIGHTNESS = 48;
static constexpr uint32_t LED_TARGET_FRAME_US = 16667UL; // 60 FPS target when WS2812 timing allows it.
static constexpr uint32_t LED_WS2812_US_PER_PIXEL = 30UL;
static constexpr uint32_t LED_SHOW_MARGIN_US = 700UL;
static constexpr uint32_t LED_RENDER_BUDGET_US = 7000UL;
static constexpr uint32_t LED_RENDER_IDLE_MARGIN_US = 400UL;
static constexpr bool DEBUG_AUDIO_PRINTS = false;
static constexpr uint16_t AUDIO_FRAMES_PER_BLOCK = 256;
static constexpr uint32_t MODE_BUTTON_DEBOUNCE_MS = 30;
static constexpr uint32_t ZC_RING_BITS = 9;
static constexpr uint32_t ZC_RING_SIZE = 1UL << ZC_RING_BITS;
static constexpr uint32_t ZC_RING_MASK = ZC_RING_SIZE - 1;

enum LED_DRIVER_LAYOUT : uint8_t {
  LED_DRIVER_LAYOUT_COLUMN_SERPENTINE,
  LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE,
  LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y
};

static constexpr LED_DRIVER_LAYOUT LED_LAYOUT = LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y;
static constexpr bool LED_FIRST_PIXEL_IS_BOTTOM_LEFT = true; // Flip this if the visualizers draw upside down.

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);
uint8_t currentLedBrightness = LED_BRIGHTNESS;

#ifndef I2S_COMM_FORMAT_STAND_I2S
  #define I2S_COMM_FORMAT_STAND_I2S I2S_COMM_FORMAT_I2S
#endif

uint16_t ledIndexXY(uint16_t x, uint16_t yFromTop) {
  if (x >= LED_DRIVER_GRID_WIDTH || yFromTop >= LED_DRIVER_GRID_HEIGHT) {
    return 0;
  }

  uint16_t stripY = LED_FIRST_PIXEL_IS_BOTTOM_LEFT
    ? (LED_DRIVER_GRID_HEIGHT - 1 - yFromTop)
    : yFromTop;

  switch (LED_LAYOUT) {
    case LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE:
    case LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y: {
      static_assert(LED_DRIVER_GRID_WIDTH == 20 && LED_DRIVER_GRID_HEIGHT == 20,
          "LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE is only for the tested 20x20 panel.");

      if (LED_LAYOUT == LED_DRIVER_LAYOUT_SPLIT_10X20_SERPENTINE_FLIPPED_Y) {
        yFromTop = (LED_DRIVER_GRID_HEIGHT - 1) - yFromTop;
      }

      if (x < 10) {
        return ((uint16_t)yFromTop * 10U) + ((yFromTop & 0x01) ? x : (uint8_t)(9 - x));
      }

      const uint16_t localX = x - 10;
      const uint16_t rowFromBottom = (LED_DRIVER_GRID_HEIGHT - 1) - yFromTop;
      return 200U + (rowFromBottom * 10U) +
        ((rowFromBottom & 0x01) ? (uint8_t)(9 - localX) : localX);
    }

    case LED_DRIVER_LAYOUT_COLUMN_SERPENTINE:
    default:
      if (x & 0x01) {
        stripY = LED_DRIVER_GRID_HEIGHT - 1 - stripY;
      }
      return (uint16_t)x * LED_DRIVER_GRID_HEIGHT + stripY;
  }
}

uint16_t visualizerLedIndexXY(uint16_t x, uint16_t yFromBottom) {
  if (yFromBottom >= LED_DRIVER_GRID_HEIGHT) {
    return 0;
  }

  return ledIndexXY(x, yFromBottom);
}

#include "audio/AudioAnalyzer.h"
#include "video/VideoFrameReceiver.h"
#include "vu/VuLoadPacketParser.h"
#include "vu/VuRuntime.h"
#include "zerocross/ZeroCrossManchesterDecoder.h"

enum CombiMode : uint8_t {
  COMBI_MODE_VIDEO_ZERO_CROSS = 0,
  COMBI_MODE_VU_LOAD = 1,
  COMBI_MODE_RAW_AUDIO = 2
};

VuProgramStore vuStore;
VuLoadPacketParser vuPacketParser;
VuRuntime vuRuntime;
VideoFrameReceiver videoReceiver;
ZeroCrossManchesterDecoder zeroCrossDecoder;

CombiMode activeMode = COMBI_MODE_VIDEO_ZERO_CROSS;
bool modeButtonLastReadingActive = false;
bool modeButtonStableActive = false;
uint32_t modeButtonLastChangeMs = 0;

uint32_t lastLedRefreshUs = 0;
uint32_t lastRenderDurationUs = 0;
uint32_t lastShowDurationUs = 0;

AudioAnalyzer audioAnalyzer;
AudioAnalysisFrame sharedAudioFrame;
portMUX_TYPE audioFrameMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t audioTaskHandle = nullptr;

static int32_t audioTaskSamples[AUDIO_FRAMES_PER_BLOCK * 2];
volatile uint32_t audioReadFailures = 0;
volatile uint32_t lastAudioBytesRead = 0;
volatile uint16_t lastAudioFramesRead = 0;
volatile int32_t lastRawL = 0;
volatile int32_t lastRawR = 0;

static ZeroCrossEdgeEvent zcRing[ZC_RING_SIZE];
static volatile uint32_t zcRingHead = 0;
static volatile uint32_t zcRingTail = 0;
static volatile uint32_t zcRingDropCount = 0;

bool popZeroCrossEdge(ZeroCrossEdgeEvent& out);
const char* combiModeName(CombiMode mode);
void setCombiMode(CombiMode nextMode);

void clearStrip() {
  strip.clear();
  strip.show();
}

uint32_t ledRefreshIntervalUs() {
  uint32_t physicalLimitUs = (uint32_t)LED_COUNT * LED_WS2812_US_PER_PIXEL + LED_SHOW_MARGIN_US;
  uint32_t measuredLimitUs = lastShowDurationUs + LED_SHOW_MARGIN_US;
  if (lastRenderDurationUs > LED_RENDER_BUDGET_US) {
    measuredLimitUs += lastRenderDurationUs - LED_RENDER_BUDGET_US + LED_RENDER_IDLE_MARGIN_US;
  }
  if (measuredLimitUs > physicalLimitUs) {
    physicalLimitUs = measuredLimitUs;
  }
  return physicalLimitUs > LED_TARGET_FRAME_US ? physicalLimitUs : LED_TARGET_FRAME_US;
}

float estimatedLedFps() {
  return 1000000.0f / (float)ledRefreshIntervalUs();
}

void publishLatestAudioFrame() {
  portENTER_CRITICAL(&audioFrameMux);
  sharedAudioFrame = audioAnalyzer.latestFrame();
  portEXIT_CRITICAL(&audioFrameMux);
}

void IRAM_ATTR zeroCrossCaptureISR() {
  const uint32_t now = micros();
  const uint8_t level = (GPIO.in >> ZERO_CROSS_CAPTURE_PIN) & 0x01;
  const uint32_t head = zcRingHead;
  const uint32_t nextHead = (head + 1) & ZC_RING_MASK;

  if (nextHead == zcRingTail) {
    zcRingDropCount++;
    return;
  }

  zcRing[head].t_us = now;
  zcRing[head].level = level;
  __asm__ __volatile__("" ::: "memory");
  zcRingHead = nextHead;
}

bool popZeroCrossEdge(ZeroCrossEdgeEvent& out) {
  const uint32_t tail = zcRingTail;
  if (tail == zcRingHead) {
    return false;
  }

  out = zcRing[tail];
  zcRingTail = (tail + 1) & ZC_RING_MASK;
  return true;
}

const char* combiModeName(CombiMode mode) {
  switch (mode) {
    case COMBI_MODE_VIDEO_ZERO_CROSS:
      return "video-zerocross";
    case COMBI_MODE_VU_LOAD:
      return "vu-load";
    case COMBI_MODE_RAW_AUDIO:
      return "raw-audio";
    default:
      return "unknown";
  }
}

void setCombiMode(CombiMode nextMode) {
  if (activeMode == nextMode) {
    return;
  }

  activeMode = nextMode;
  clearStrip();

  if (activeMode == COMBI_MODE_VIDEO_ZERO_CROSS) {
    vuStore.clear();
    vuRuntime.reset();
    videoReceiver.reset();
    zeroCrossDecoder.setSinkMode(ZeroCrossManchesterDecoder::SINK_VIDEO);
  } else if (activeMode == COMBI_MODE_VU_LOAD) {
    vuPacketParser.reset();
    zeroCrossDecoder.setSinkMode(ZeroCrossManchesterDecoder::SINK_VU_LOAD);
  } else {
    zeroCrossDecoder.resetForNextPacket();
  }

  Serial.print("mode ");
  Serial.println(combiModeName(activeMode));
}

void cycleCombiMode() {
  if (activeMode == COMBI_MODE_VIDEO_ZERO_CROSS) {
    setCombiMode(COMBI_MODE_VU_LOAD);
  } else if (activeMode == COMBI_MODE_VU_LOAD) {
    setCombiMode(COMBI_MODE_RAW_AUDIO);
  } else {
    setCombiMode(COMBI_MODE_VIDEO_ZERO_CROSS);
  }
}

void pollModeButton() {
  const bool readingActive = digitalRead(MODE_BUTTON_PIN) == LOW;
  const uint32_t nowMs = millis();

  if (readingActive != modeButtonLastReadingActive) {
    modeButtonLastReadingActive = readingActive;
    modeButtonLastChangeMs = nowMs;
    return;
  }

  if (readingActive == modeButtonStableActive) {
    return;
  }

  if ((uint32_t)(nowMs - modeButtonLastChangeMs) < MODE_BUTTON_DEBOUNCE_MS) {
    return;
  }

  modeButtonStableActive = readingActive;
  if (modeButtonStableActive) {
    cycleCombiMode();
  }
}

void serviceZeroCrossInput() {
  ZeroCrossEdgeEvent event;
  while (popZeroCrossEdge(event)) {
    if (activeMode == COMBI_MODE_VIDEO_ZERO_CROSS || activeMode == COMBI_MODE_VU_LOAD) {
      zeroCrossDecoder.processEdge(event);
    }
  }

  if (activeMode == COMBI_MODE_VIDEO_ZERO_CROSS || activeMode == COMBI_MODE_VU_LOAD) {
    zeroCrossDecoder.pollForSilence();
  }
}

void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,

    // PCM1861 outputs 24-bit audio, but use 32-bit slots:
    // stereo * 32 bits = 64 BCK per LRCK, exactly what the PCM1861 likes.
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,

    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,

    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,

    // Use ESP32 audio PLL for a less-janky audio clock.
    .use_apll = true,

    .tx_desc_auto_clear = false,

    // Force correct MCLK if your core supports this field.
    .fixed_mclk = MCLK_HZ,

    // These exist in newer Arduino-ESP32 / ESP-IDF 4.x builds.
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    .bits_per_chan = I2S_BITS_PER_CHAN_32BIT
  };

  i2s_pin_config_t pins = {
    .mck_io_num = PIN_I2S_MCLK,
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_I2S_DIN
  };

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));
}

void audioTask(void* parameter) {
  (void)parameter;

  while (true) {
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(
      I2S_PORT,
      audioTaskSamples,
      sizeof(audioTaskSamples),
      &bytesRead,
      portMAX_DELAY
    );

    uint16_t frames = bytesRead / 8; // 2 channels * 32-bit
    if (err != ESP_OK || frames == 0) {
      audioReadFailures++;
      continue;
    }

    lastAudioBytesRead = bytesRead;
    lastAudioFramesRead = frames;
    lastRawL = audioTaskSamples[0];
    lastRawR = audioTaskSamples[1];

    if (audioAnalyzer.processBlock(audioTaskSamples, frames)) {
      publishLatestAudioFrame();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void startAudioTask() {
  xTaskCreatePinnedToCore(
    audioTask,
    "audio-analyzer",
    8192,
    nullptr,
    3,
    &audioTaskHandle,
    0
  );
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  strip.begin();
  strip.setBrightness(currentLedBrightness);
  clearStrip();

  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  modeButtonLastReadingActive = digitalRead(MODE_BUTTON_PIN) == LOW;
  modeButtonStableActive = modeButtonLastReadingActive;
  modeButtonLastChangeMs = millis();

  vuStore.begin();
  vuPacketParser.begin(&vuStore);
  vuRuntime.begin(&vuStore);
  videoReceiver.reset();
  zeroCrossDecoder.begin(&videoReceiver, &vuPacketParser);
  zeroCrossDecoder.setSinkMode(ZeroCrossManchesterDecoder::SINK_VIDEO);

  pinMode(ZERO_CROSS_CAPTURE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ZERO_CROSS_CAPTURE_PIN), zeroCrossCaptureISR, CHANGE);

  clearAudioAnalysisFrame(sharedAudioFrame);
  audioAnalyzer.begin();
  setupI2S();
  startAudioTask();

  WiFi.mode(WIFI_OFF);
  btStop();

  Serial.println("combi firmware started");
  Serial.println("mode button cycles video-zerocross -> vu-load -> raw-audio");
  Serial.println("PCM1861 I2S RX started at 96 kHz.");
  Serial.println("Audio analyzer task pinned to core 0.");
  Serial.print("mode ");
  Serial.println(combiModeName(activeMode));
}

void loop() {
  pollModeButton();
  serviceZeroCrossInput();

  if (activeMode == COMBI_MODE_VIDEO_ZERO_CROSS) {
    videoReceiver.service(strip);
    return;
  }

  if (activeMode == COMBI_MODE_VU_LOAD) {
    delay(1);
    return;
  }

  uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastLedRefreshUs) >= ledRefreshIntervalUs()) {
    lastLedRefreshUs = nowUs;

    AudioAnalysisFrame audioFrame;
    portENTER_CRITICAL(&audioFrameMux);
    audioFrame = sharedAudioFrame;
    portEXIT_CRITICAL(&audioFrameMux);

    uint32_t renderStartUs = micros();
    vuRuntime.render(strip, audioFrame);
    lastRenderDurationUs = micros() - renderStartUs;

    uint32_t showStartUs = micros();
    strip.show();
    lastShowDurationUs = micros() - showStartUs;
  }

  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if (DEBUG_AUDIO_PRINTS && now - lastPrint > 500) {
    lastPrint = now;

    AudioAnalysisFrame audioFrame;
    portENTER_CRITICAL(&audioFrameMux);
    audioFrame = sharedAudioFrame;
    portEXIT_CRITICAL(&audioFrameMux);

    int32_t rawL = lastRawL;
    int32_t rawR = lastRawR;

    int32_t s24L = rawL >> 8;
    int32_t s24R = rawR >> 8;

    Serial.print("bytes=");
    Serial.print(lastAudioBytesRead);
    Serial.print(" frames=");
    Serial.print(lastAudioFramesRead);
    Serial.print(" rawL=0x");
    Serial.print((uint32_t)rawL, HEX);
    Serial.print(" rawR=0x");
    Serial.print((uint32_t)rawR, HEX);
    Serial.print(" s24L=");
    Serial.print(s24L);
    Serial.print(" s24R=");
    Serial.print(s24R);
    Serial.print(" seq=");
    Serial.print(audioFrame.sequence);
    Serial.print(" bass=");
    Serial.print(audioFrame.bass, 2);
    Serial.print(" low45-140=");
    Serial.print(audioFrame.kick, 2);
    Serial.print(" mid=");
    Serial.print(audioFrame.mid, 2);
    Serial.print(" treble=");
    Serial.print(audioFrame.treble, 2);
    Serial.print(" domHz=");
    Serial.print(audioFrame.dominantFrequencyHz, 0);
    Serial.print(" domDb=");
    Serial.print(audioFrame.bandDb[audioFrame.dominantBand], 1);
    Serial.print(" fps=");
    Serial.print(estimatedLedFps(), 1);
    Serial.print(" i2sFailures=");
    Serial.println(audioReadFailures);
  }
}
