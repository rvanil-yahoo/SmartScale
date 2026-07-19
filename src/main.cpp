#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <HX711.h>

constexpr uint8_t HX711_DT_PIN = 34;
constexpr uint8_t HX711_SCK_PIN = 21;

constexpr uint8_t TFT_CS_PIN = 14;
constexpr uint8_t TFT_DC_PIN = 4;
constexpr uint8_t TFT_RST_PIN = 5;
constexpr uint8_t TFT_MOSI_PIN = 18;
constexpr uint8_t TFT_SCLK_PIN = 19;

constexpr uint16_t SCREEN_REFRESH_MS = 1000;
constexpr uint16_t HX711_STARTUP_TIMEOUT_MS = 2000;
constexpr long HX711_MAX_RAW = 8388607L;
constexpr long HX711_MIN_RAW = -8388608L;
constexpr float CAL_OFFSET_COUNTS = 1114690.18f;
constexpr float CAL_COUNTS_PER_LB = 9169.7606f;
constexpr uint16_t TFT_BG_COLOR = ST77XX_BLACK;
constexpr uint16_t TFT_VALUE_READY_COLOR = ST77XX_GREEN;
constexpr uint16_t TFT_VALUE_ERROR_COLOR = ST77XX_RED;
constexpr char TITLE_TEXT[] = "Smart Scale";
constexpr uint8_t TFT_ROTATION = 1;
constexpr int8_t TFT_COL_START = 2;
constexpr int8_t TFT_ROW_START = 1;
constexpr int16_t SCREEN_PADDING = 10;

constexpr int16_t SCREEN_WIDTH = 160;
constexpr int16_t SCREEN_HEIGHT = 128;
constexpr int16_t VALUE_BOX_X = SCREEN_PADDING;
constexpr int16_t VALUE_BOX_Y = 32 + SCREEN_PADDING;
constexpr int16_t VALUE_BOX_W = SCREEN_WIDTH - (SCREEN_PADDING * 2);
constexpr int16_t VALUE_BOX_H = 30;

HX711 scale;
class SmartScaleDisplay : public Adafruit_ST7735 {
public:
  using Adafruit_ST7735::Adafruit_ST7735;

  void applyPanelOffset(int8_t colStart, int8_t rowStart, uint8_t rotation) {
    setColRowStart(colStart, rowStart);
    setRotation(rotation);
  }
};

SmartScaleDisplay tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

unsigned long lastRefreshAt = 0;

bool isSaturatedReading(long rawValue) {
  return rawValue == HX711_MAX_RAW || rawValue == HX711_MIN_RAW;
}

float rawToLbs(long rawValue) {
  const float pounds = (static_cast<float>(rawValue) - CAL_OFFSET_COUNTS) / CAL_COUNTS_PER_LB;
  return (pounds < 0.0f) ? 0.0f : pounds;
}

void drawStaticFrame() {
  int16_t titleX = 0;
  int16_t titleY = 0;
  uint16_t titleW = 0;
  uint16_t titleH = 0;

  tft.fillScreen(TFT_BG_COLOR);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.getTextBounds(TITLE_TEXT, 0, 0, &titleX, &titleY, &titleW, &titleH);
  tft.setCursor((SCREEN_WIDTH - titleW) / 2, SCREEN_PADDING);
  tft.println(TITLE_TEXT);
}

void drawReading(long rawValue, bool hx711Ready) {
  tft.fillRect(VALUE_BOX_X, VALUE_BOX_Y, VALUE_BOX_W, VALUE_BOX_H, TFT_BG_COLOR);
  tft.setCursor(VALUE_BOX_X, VALUE_BOX_Y);
  tft.setTextSize(3);
  tft.setTextWrap(false);

  if (hx711Ready && !isSaturatedReading(rawValue)) {
    const float pounds = rawToLbs(rawValue);
    tft.setTextColor(TFT_VALUE_READY_COLOR, TFT_BG_COLOR);
    tft.print(pounds, 0);
    tft.println(F(" lb"));
  } else if (hx711Ready) {
    tft.setTextColor(TFT_VALUE_ERROR_COLOR, TFT_BG_COLOR);
    tft.println(F("saturated"));
  } else {
    tft.setTextColor(TFT_VALUE_ERROR_COLOR, TFT_BG_COLOR);
    tft.println(F("not ready"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println(F("SmartScale booting"));

  SPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);
  tft.initR(INITR_BLACKTAB);
  tft.applyPanelOffset(TFT_COL_START, TFT_ROW_START, TFT_ROTATION);
  drawStaticFrame();

  pinMode(HX711_DT_PIN, INPUT);
  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_gain(128);

  Serial.print(F("HX711 pins DT="));
  Serial.print(HX711_DT_PIN);
  Serial.print(F(" SCK="));
  Serial.println(HX711_SCK_PIN);
  Serial.print(F("Calibration offset="));
  Serial.println(CAL_OFFSET_COUNTS, 2);
  Serial.print(F("Calibration counts/lb="));
  Serial.println(CAL_COUNTS_PER_LB, 4);

  const bool hx711Ready = scale.wait_ready_timeout(HX711_STARTUP_TIMEOUT_MS);
  if (hx711Ready) {
    Serial.println(F("HX711 ready"));
  } else {
    Serial.println(F("HX711 not ready after startup timeout"));
    Serial.println(F("Check HX711 power, DT/SCK wiring, and common ground."));
  }

  drawReading(0, hx711Ready);
}

void loop() {
  const unsigned long now = millis();
  if (now - lastRefreshAt < SCREEN_REFRESH_MS) {
    return;
  }

  lastRefreshAt = now;

  const bool hx711Ready = scale.is_ready();
  long rawValue = 0;

  if (hx711Ready) {
    rawValue = scale.read_average(5);
    if (isSaturatedReading(rawValue)) {
      Serial.print(F("HX711 raw saturated: "));
      Serial.println(rawValue);
      Serial.println(F("Check E+/E-/A+/A- wiring, load-cell combiner wiring, and HX711 supply voltage."));
    } else {
      const float pounds = rawToLbs(rawValue);
      Serial.println(pounds, 0);
    }
  } else {
    Serial.println(F("HX711 not ready"));
  }

  drawReading(rawValue, hx711Ready);
}