#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include "wifi_config.h"

#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Copy wifi_config.example.h to wifi_config.h and fill in your credentials."
#endif

// 1. Define the Data Bus (SPI)
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  15 /* DC */,
  14 /* CS */,
  7  /* SCK */,
  6  /* MOSI */,
  -1 /* MISO */
);

// 2. Define the Display Driver (ST7789)
Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  21 /* RST */,
  0  /* rotation */,
  true /* IPS display */,
  172 /* width */,
  320 /* height */,
  34 /* col offset1 */,
  0  /* row offset1 */,
  34 /* col offset2: needed for landscape rotation 1 */,
  0  /* row offset2 */
);

#define TFT_BL_PIN 22

// Colors (RGB565)
#define BG_COLOR       0x0000
#define TEXT_COLOR     0xFFFF
#define BAR_BG_COLOR   0x18E3  // dark grey
#define BAR_HIGH_COLOR 0x07E0  // green
#define BAR_MED_COLOR  0xFFE0  // yellow
#define BAR_LOW_COLOR  0xF800  // red
#define WIFI_CONNECTED_COLOR 0x07E0 // green
#define WIFI_DISCONNECTED_COLOR 0xF800 // red

// Set to 1 to rotate the display 180 degrees (landscape flipped)
#define FLIP_DISPLAY 1

struct QuotaWindow {
  const char* label;
  uint32_t used;
  uint32_t limit;
};

QuotaWindow quotaWindows[] = {
  { "5h", 45,   200  },
  { "7d", 1230, 5000 }
};
const int WINDOW_COUNT = sizeof(quotaWindows) / sizeof(quotaWindows[0]);

unsigned long lastUpdate = 0;
unsigned long lastWiFiCheck = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Kimi quota display starting");

  setupWiFi();

  gfx->begin();
  gfx->setRotation(FLIP_DISPLAY ? 3 : 1); // Landscape: 1 = normal, 3 = 180° flipped
  gfx->fillScreen(BG_COLOR);

  // Backlight via LEDC (ESP32-C6 uses the new unified LEDC API)
  ledcAttach(TFT_BL_PIN, 5000, 8);
  ledcWrite(TFT_BL_PIN, 80);

  randomSeed(micros());

  drawQuota();
}

void loop() {
  unsigned long now = millis();

  if (now - lastUpdate >= 1000) {
    lastUpdate = now;
    updateStubQuota();
    drawQuota();
  }

  if (now - lastWiFiCheck >= 5000) {
    lastWiFiCheck = now;
    maintainWiFi();
  }
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected; reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void updateStubQuota() {
  for (int i = 0; i < WINDOW_COUNT; i++) {
    uint32_t increment = random(1, 5) + (i == 1 ? 3 : 0);
    quotaWindows[i].used += increment;
    if (quotaWindows[i].used > quotaWindows[i].limit) {
      quotaWindows[i].used = 0;
    }
  }
}

void drawQuota() {
  static bool firstDraw = true;

  int16_t screenW = gfx->width();
  int16_t screenH = gfx->height();
  int16_t bandH = screenH / WINDOW_COUNT;
  const int16_t margin = 8;

  if (firstDraw) {
    gfx->fillScreen(BG_COLOR);
    firstDraw = false;
  }

  for (int i = 0; i < WINDOW_COUNT; i++) {
    QuotaWindow &win = quotaWindows[i];
    int pctRemaining = (win.limit > 0)
      ? ((win.limit - win.used) * 100 / win.limit)
      : 0;

    int16_t y0 = i * bandH;
    int16_t textY = y0 + margin;
    int16_t barY = textY + 22;
    int16_t barW = screenW - 2 * margin;
    int16_t barH = 26;
    int16_t filledW = (int32_t)barW * pctRemaining / 100;

    // Window label (static, safe to redraw every time)
    gfx->setTextSize(2);
    gfx->setTextColor(TEXT_COLOR);
    gfx->setCursor(margin, textY);
    gfx->print(win.label);

    // Clear the percentage text area before writing the new value
    gfx->fillRect(screenW - margin - 60, textY, 60, 16, BG_COLOR);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pctRemaining);
    gfx->setCursor(screenW - margin - 54, textY);
    gfx->print(pctBuf);

    // Redraw the bar background, then the fill, to erase the previous frame
    gfx->fillRect(margin, barY, barW, barH, BAR_BG_COLOR);
    uint16_t barColor = colorForPercent(pctRemaining);
    gfx->fillRect(margin, barY, filledW, barH, barColor);

    // Clear and redraw used / limit numbers
    gfx->setTextSize(1);
    gfx->fillRect(margin, barY + barH + 4, 84, 10, BG_COLOR);
    char numBuf[32];
    snprintf(numBuf, sizeof(numBuf), "%lu / %lu", win.used, win.limit);
    gfx->setCursor(margin, barY + barH + 4);
    gfx->print(numBuf);

    // Small WiFi status indicator on the first window
    if (i == 0) {
      drawWiFiIndicator(screenW - margin - 78, textY + 4);
    }
  }
}

void drawWiFiIndicator(int16_t x, int16_t y) {
  bool connected = (WiFi.status() == WL_CONNECTED);
  uint16_t color = connected ? WIFI_CONNECTED_COLOR : WIFI_DISCONNECTED_COLOR;

  // 8x8 status square
  gfx->fillRect(x, y, 8, 8, color);
}

uint16_t colorForPercent(int pctRemaining) {
  if (pctRemaining > 50) return BAR_HIGH_COLOR;
  if (pctRemaining > 20) return BAR_MED_COLOR;
  return BAR_LOW_COLOR;
}
