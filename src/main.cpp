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

constexpr uint16_t SCREEN_REFRESH_MS = 250;

HX711 scale;
Adafruit_ST7735 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

unsigned long lastRefreshAt = 0;

void drawFrame(long rawValue, bool hx711Ready) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println(F("Hello"));
  tft.println(F("World"));

  tft.setTextSize(1);
  tft.println();
  tft.setTextColor(ST77XX_CYAN);
  tft.print(F("HX711: "));

  if (hx711Ready) {
    tft.setTextColor(ST77XX_GREEN);
    tft.println(rawValue);
  } else {
    tft.setTextColor(ST77XX_RED);
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
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(true);

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);

  drawFrame(0, scale.is_ready());
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
    Serial.print(F("HX711 raw: "));
    Serial.println(rawValue);
  } else {
    Serial.println(F("HX711 not ready"));
  }

  drawFrame(rawValue, hx711Ready);
}