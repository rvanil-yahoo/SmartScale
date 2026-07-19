#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <HX711.h>
#include <esp_bt.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

// HX711 wiring.
constexpr uint8_t HX711_DT_PIN = 34;
constexpr uint8_t HX711_SCK_PIN = 21;

// ST7735 wiring.
constexpr uint8_t TFT_CS_PIN = 14;
constexpr uint8_t TFT_DC_PIN = 4;
constexpr uint8_t TFT_RST_PIN = 5;
constexpr uint8_t TFT_MOSI_PIN = 18;
constexpr uint8_t TFT_SCLK_PIN = 19;
constexpr uint8_t TFT_BL_PIN = 15;

// Timing and stability control.
constexpr uint16_t SCREEN_REFRESH_MS = 1000;
constexpr uint16_t HX711_STARTUP_TIMEOUT_MS = 2000;
constexpr uint16_t HX711_WAKE_SETTLE_MS = 5;
constexpr uint8_t STABLE_SAMPLES_FOR_SLEEP = 10;

// HX711 special values indicating ADC rail saturation.
constexpr long HX711_MAX_RAW = 8388607L;
constexpr long HX711_MIN_RAW = -8388608L;

// Linear calibration model: pounds = (raw - offset) / counts_per_lb.
constexpr float CAL_OFFSET_COUNTS = 1114690.18f;
constexpr float CAL_COUNTS_PER_LB = 9169.7606f;

// UI colors and title.
constexpr uint16_t TFT_BG_COLOR = ST77XX_BLACK;
constexpr uint16_t TFT_VALUE_READY_COLOR = ST77XX_GREEN;
constexpr uint16_t TFT_VALUE_ERROR_COLOR = ST77XX_RED;
constexpr char TITLE_TEXT[] = "Smart Scale";

// Panel-specific orientation/offset correction.
constexpr uint8_t TFT_ROTATION = 1;
constexpr int8_t TFT_COL_START = 2;
constexpr int8_t TFT_ROW_START = 1;
constexpr int16_t SCREEN_PADDING = 10;

// 160x128 panel layout constants.
constexpr int16_t SCREEN_WIDTH = 160;
constexpr int16_t SCREEN_HEIGHT = 128;
constexpr int16_t VALUE_BOX_X = SCREEN_PADDING;
constexpr int16_t VALUE_BOX_Y = 32 + SCREEN_PADDING;
constexpr int16_t VALUE_BOX_W = SCREEN_WIDTH - (SCREEN_PADDING * 2);
constexpr int16_t VALUE_BOX_H = 30;

HX711 scale;

// Small wrapper to expose protected ST77xx offset controls.
class SmartScaleDisplay : public Adafruit_ST7735 {
public:
  using Adafruit_ST7735::Adafruit_ST7735;

  /*
   * Applies panel start offsets and display rotation.
   * This exposes protected ST77xx helpers through a tiny wrapper.
   */
  void applyPanelOffset(int8_t colStart, int8_t rowStart, uint8_t rotation) {
    setColRowStart(colStart, rowStart);
    setRotation(rotation);
  }
};

SmartScaleDisplay tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

// Stability tracking for sleep decision logic.
int32_t lastStableWeightLbs = 0;
uint8_t stableSampleCount = 0;
bool hasStableReference = false;

// Tracks what value was last rendered to the TFT.
int32_t lastDisplayedWeightLbs = 0;
bool hasDisplayedWeight = false;

/*
 * Controls TFT backlight state through a dedicated GPIO.
 * Active HIGH is assumed for the configured backlight pin.
 */
void setBacklight(bool enabled) {
  digitalWrite(TFT_BL_PIN, enabled ? HIGH : LOW);
}

/*
 * Controls TFT panel and backlight together.
 * Keeps display power state consistent in one place.
 */
void setDisplayActive(bool enabled) {
  tft.enableDisplay(enabled);
  setBacklight(enabled);
}

/*
 * Powers down Wi-Fi and Bluetooth radios.
 * Called once at startup to reduce baseline power draw.
 */
void disableRadios() {
  esp_wifi_stop();
  esp_wifi_deinit();
  btStop();
}

/*
 * Enters light sleep until the next sample interval.
 * Uses timer wake and blanks the TFT during sleep to save power.
 */
void sleepUntilNextSample() {
  // Reset wake sources before configuring this sleep cycle.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // Timer wake keeps sleep behavior deterministic even if HX711 DOUT stays HIGH.
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(SCREEN_REFRESH_MS) * 300000ULL);

  // Disable panel to save additional power while CPU sleeps.
  scale.power_down();
  setDisplayActive(false);
  esp_light_sleep_start();

  // Wake sensor path, but keep TFT off until value actually changes.
  scale.power_up();
  delay(HX711_WAKE_SETTLE_MS);
  stableSampleCount = 0;
  hasStableReference = false;
}

/*
 * Returns true when HX711 output is at ADC rail limits.
 * These values indicate saturation, not a valid weight.
 */
bool isSaturatedReading(long rawValue) {
  return rawValue == HX711_MAX_RAW || rawValue == HX711_MIN_RAW;
}

/*
 * Converts raw HX711 counts into pounds using linear calibration.
 * Negative values are clamped so the UI never shows below zero.
 */
float rawToLbs(long rawValue) {
  const float pounds = (static_cast<float>(rawValue) - CAL_OFFSET_COUNTS) / CAL_COUNTS_PER_LB;
  return (pounds < 0.0f) ? 0.0f : pounds;
}

/*
 * Draws static screen elements.
 * This is called during setup and not during every measurement cycle.
 */
void drawStaticFrame() {
  int16_t titleX = 0;
  int16_t titleY = 0;
  uint16_t titleW = 0;
  uint16_t titleH = 0;

  // Clear full screen and center the header text.
  tft.fillScreen(TFT_BG_COLOR);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.getTextBounds(TITLE_TEXT, 0, 0, &titleX, &titleY, &titleW, &titleH);
  tft.setCursor((SCREEN_WIDTH - titleW) / 2, SCREEN_PADDING);
  tft.println(TITLE_TEXT);
}

/*
 * Draws the dynamic reading area only.
 * Avoids full-screen redraws to reduce flicker and update cost.
 */
void drawReading(long rawValue, bool hx711Ready) {
  // Erase previous value in the reading box.
  tft.fillRect(VALUE_BOX_X, VALUE_BOX_Y, VALUE_BOX_W, VALUE_BOX_H, TFT_BG_COLOR);
  tft.setCursor(VALUE_BOX_X, VALUE_BOX_Y);
  tft.setTextSize(3);
  tft.setTextWrap(false);

  // Show calculated weight when data is valid.
  if (hx711Ready && !isSaturatedReading(rawValue)) {
    const float pounds = rawToLbs(rawValue);
    tft.setTextColor(TFT_VALUE_READY_COLOR, TFT_BG_COLOR);
    tft.print(pounds, 0);
    tft.println(F(" lb"));

  // Show sensor fault states when reading is not usable.
  } else if (hx711Ready) {
    tft.setTextColor(TFT_VALUE_ERROR_COLOR, TFT_BG_COLOR);
    tft.println(F("saturated"));
  } else {
    tft.setTextColor(TFT_VALUE_ERROR_COLOR, TFT_BG_COLOR);
    tft.println(F("not ready"));
  }
}

/*
 * One-time hardware initialization.
 * Brings up radios-off state, display, and HX711.
 */
void setup() {
  delay(250);

  // Disable Wi-Fi/Bluetooth to reduce power in production mode.
  disableRadios();

  // Initialize TFT SPI bus and panel orientation/offset.
  pinMode(TFT_BL_PIN, OUTPUT);

  SPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);
  tft.initR(INITR_BLACKTAB);
  tft.applyPanelOffset(TFT_COL_START, TFT_ROW_START, TFT_ROTATION);
  setDisplayActive(false);

  // Initialize HX711 input and gain.
  pinMode(HX711_DT_PIN, INPUT);
  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_gain(128);

  // Wait briefly for HX711 to indicate first data-ready.
  scale.wait_ready_timeout(HX711_STARTUP_TIMEOUT_MS);
}

/*
 * Main measurement and power-management loop.
 * Reads sensor, updates display, and decides when to sleep.
 */
void loop() {
  // Poll sensor readiness and fetch averaged raw count.
  const bool hx711Ready = scale.is_ready();
  long rawValue = 0;
  bool hasComparableSample = false;
  int32_t roundedPounds = 0;
  bool displayValueChanged = false;

  if (hx711Ready) {
    rawValue = scale.read_average(5);

    // Detect fault values before converting to pounds.
    if (isSaturatedReading(rawValue)) {
    } else {
      // Convert to pounds and quantize to integer for stability tracking.
      const float pounds = rawToLbs(rawValue);
      roundedPounds = static_cast<int32_t>(pounds + 0.5f);
      hasComparableSample = true;
    }
  }

  // Track how many consecutive samples produce the same rounded value.
  if (hasComparableSample) {
    if (!hasDisplayedWeight || roundedPounds != lastDisplayedWeightLbs) {
      displayValueChanged = true;
    }

    if (!hasStableReference || roundedPounds != lastStableWeightLbs) {
      lastStableWeightLbs = roundedPounds;
      stableSampleCount = 1;
      hasStableReference = true;
    } else if (stableSampleCount < 255) {
      stableSampleCount++;
    }
  } else {
    hasStableReference = false;
    stableSampleCount = 0;
  }

  // Power the TFT only when a new value should be shown.
  if (displayValueChanged) {
    setDisplayActive(true);
    drawStaticFrame();
    drawReading(rawValue, hx711Ready);
    lastDisplayedWeightLbs = roundedPounds;
    hasDisplayedWeight = true;
  }

  // Enter sleep only after enough stable samples; otherwise continue active updates.
  if (stableSampleCount >= STABLE_SAMPLES_FOR_SLEEP) {
    sleepUntilNextSample();
  } else {
    delay(SCREEN_REFRESH_MS);
  }
}