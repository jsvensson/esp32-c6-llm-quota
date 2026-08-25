#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp32-hal-rgb-led.h>
#include "config.h"

#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Copy config.example.h to config.h and fill in your credentials."
#endif

#ifndef MQTT_HOST
#error "MQTT_HOST not defined. Copy config.example.h to config.h and fill in your broker settings."
#endif

// Waveshare ESP32-C6-LCD-1.47 pinout
#define TFT_DC       15
#define TFT_CS       14
#define TFT_SCK      7
#define TFT_MOSI     6
#define TFT_RST      21
#define TFT_BL_PIN   22
#define STATUS_LED_PIN 8
#define BOOT_BUTTON_PIN 9

// 1. Define the Data Bus (SPI)
// Arduino_HWSPI on ESP32 takes DC, CS, SCK, MOSI, MISO, SPIClass*, and shared flag.
// It calls SPI.begin(SCK, MISO, MOSI) internally during gfx->begin().
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1, &SPI);

// 2. Define the Display Driver (ST7789)
Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  TFT_RST,
  0  /* rotation */,
  true /* IPS display */,
  172 /* width */,
  320 /* height */,
  34 /* col offset1 */,
  0  /* row offset1 */,
  34 /* col offset2: needed for landscape rotation 1 */,
  0  /* row offset2 */
);

// Brightness settings (0-255). Lower these if the board runs hot.
#define DISPLAY_BRIGHTNESS    10   // TFT backlight PWM duty
#define STATUS_LED_BRIGHTNESS 53  // Onboard RGB LED master brightness

// Colors (RGB565)
#define BG_COLOR       0x0000
#define TEXT_COLOR     0xFFFF
#define BAR_BG_COLOR   0x18E3  // dark grey
#define BAR_HIGH_COLOR 0x07E0  // green
#define BAR_MED_COLOR 0xFD20  // orange
#define BAR_LOW_COLOR 0xF800  // red
#define WIFI_CONNECTED_COLOR 0x07E0 // green
#define WIFI_DISCONNECTED_COLOR 0xF800 // red
#define MQTT_CONNECTED_COLOR 0x07E0 // green
#define MQTT_DISCONNECTED_COLOR 0xF800 // red

// Set to 1 to rotate the display 180 degrees (landscape flipped)
#define FLIP_DISPLAY 1

struct QuotaWindow {
  const char* label;
  int pctAvailable;
  time_t resetsAt;
  bool hasResetAt;
};

QuotaWindow quotaWindows[] = {
  { "5h", 22, 0, false },
  { "7d", 75, 0, false }
};
const int WINDOW_COUNT = sizeof(quotaWindows) / sizeof(quotaWindows[0]);

unsigned long lastUpdate = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastQuotaChangeMillis = 0;
unsigned long bootMillis = 0;
bool quotaDataReceived = false;
bool timeSynced = false;
bool powerSaveActive = false;
bool bootButtonPressed = false;
unsigned long lastBootButtonDebounce = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void setup() {
  Serial.begin(115200);
  Serial.println("Kimi quota display starting");

  // Initialize display early so we can show the WiFi connection screen
  gfx->begin();
  gfx->setRotation(FLIP_DISPLAY ? 3 : 1); // Landscape: 1 = normal, 3 = 180° flipped
  gfx->fillScreen(BG_COLOR);

  // Backlight via LEDC (ESP32-C6 uses the new unified LEDC API)
  ledcAttach(TFT_BL_PIN, 5000, 8);
  ledcWrite(TFT_BL_PIN, DISPLAY_BRIGHTNESS);

  setupWiFi();
  setupMQTT();
  setupNTP();

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  randomSeed(micros());
  bootMillis = millis();

  // Clear the WiFi connection screen before showing the gauges
  gfx->fillScreen(BG_COLOR);
  drawQuota();
}

void loop() {
  unsigned long now = millis();

  maintainNTP();
  maintainMQTT();

  // Redraw every 5 s while awake. The reset countdown only has minute-level
  // granularity, and this is frequent enough for the status indicators and bar
  // animation. MQTT messages still trigger an immediate redraw in mqttCallback().
  if (now - lastUpdate >= 5000) {
    lastUpdate = now;
    if (!powerSaveActive) {
      if (!quotaDataReceived) {
        updateStubQuota();
      }
      drawQuota();
    }
  }

  // Enter power-save after the configured idle timeout, but stay awake for at
  // least that long after the initial boot so the display is visible on startup.
  // A value of 0 disables power-save entirely. WiFi/MQTT/NTP keep running so a
  // new quota message can wake the device in mqttCallback().
  unsigned long powerSaveTimeoutMs = (unsigned long)POWER_SAVE_AFTER_MINUTES * 60000UL;
  if (POWER_SAVE_AFTER_MINUTES > 0 && quotaDataReceived && !powerSaveActive &&
      (now - bootMillis >= powerSaveTimeoutMs) &&
      (now - lastQuotaChangeMillis >= powerSaveTimeoutMs)) {
    enterPowerSave();
  }

  // Handle the BOOT button (active LOW) to manually toggle power-save mode.
  // A simple 200 ms debounce prevents accidental double toggles.
  bool buttonDown = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (buttonDown && !bootButtonPressed && (now - lastBootButtonDebounce >= 200)) {
    bootButtonPressed = true;
    lastBootButtonDebounce = now;
    togglePowerSave();
  } else if (!buttonDown && bootButtonPressed) {
    bootButtonPressed = false;
  }

  if (now - lastWiFiCheck >= 5000) {
    lastWiFiCheck = now;
    maintainWiFi();
  }
}

const char* wifiStatusName(uint8_t status) {
  switch (status) {
    case WL_IDLE_STATUS:     return "WL_IDLE_STATUS";
    case WL_SCAN_COMPLETED:  return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:       return "WL_CONNECTED";
    case WL_CONNECT_FAILED:  return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:    return "WL_DISCONNECTED";
    case WL_NO_SHIELD:       return "WL_NO_SHIELD";
    default:                 return "UNKNOWN";
  }
}

void drawWiFiConnectScreen(const char* ssid, int dots) {
  int16_t screenW = gfx->width();
  int16_t screenH = gfx->height();
  const int16_t margin = 8;

  gfx->fillScreen(BG_COLOR);

  gfx->setTextSize(2);
  gfx->setTextColor(TEXT_COLOR);
  gfx->setCursor(margin, screenH / 2 - 30);
  gfx->print("Connecting to WiFi");

  // Animated dots (cycle 1-3)
  gfx->setCursor(margin, screenH / 2);
  int dotCount = (dots % 3) + 1;
  for (int i = 0; i < dotCount; i++) {
    gfx->print(".");
  }
  // Clear any leftover dots from previous frame
  gfx->fillRect(margin + dotCount * 12, screenH / 2, screenW - margin - dotCount * 12, 16, BG_COLOR);

  gfx->setTextSize(1);
  gfx->setCursor(margin, screenH / 2 + 24);
  gfx->print("SSID: ");
  gfx->print(ssid);
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);

  Serial.print("[WiFi] MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("[WiFi] Configured SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("[WiFi] SSID length: ");
  Serial.println(strlen(WIFI_SSID));
  Serial.print("[WiFi] Password length: ");
  Serial.println(strlen(WIFI_PASSWORD));

  // Clear any stale connection state before starting
  WiFi.disconnect(true);
  delay(100);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("[WiFi] Starting connection...");

  unsigned long startAttempt = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    drawWiFiConnectScreen(WIFI_SSID, dots++);

    uint8_t status = WiFi.status();
    Serial.print("[WiFi] status: ");
    Serial.print(wifiStatusName(status));
    Serial.print(" (");
    Serial.print(status);
    Serial.println(")");

    delay(500);
  }

  gfx->fillScreen(BG_COLOR);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected successfully");
    Serial.print("[WiFi] IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Subnet mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("[WiFi] Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("[WiFi] DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("[WiFi] RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("[WiFi] MAC: ");
    Serial.println(WiFi.macAddress());

    gfx->setTextSize(2);
    gfx->setTextColor(WIFI_CONNECTED_COLOR);
    gfx->setCursor(8, gfx->height() / 2 - 12);
    gfx->print("WiFi connected");
  } else {
    uint8_t finalStatus = WiFi.status();
    Serial.print("[WiFi] Connection failed, final status: ");
    Serial.print(wifiStatusName(finalStatus));
    Serial.print(" (");
    Serial.print(finalStatus);
    Serial.println(")");

    gfx->setTextSize(2);
    gfx->setTextColor(WIFI_DISCONNECTED_COLOR);
    gfx->setCursor(8, gfx->height() / 2 - 12);
    gfx->print("WiFi failed");
  }

  delay(1500);
}

void maintainWiFi() {
  uint8_t status = WiFi.status();
  if (status != WL_CONNECTED) {
    Serial.print("[WiFi] Link lost, status: ");
    Serial.print(wifiStatusName(status));
    Serial.print(" (");
    Serial.print(status);
    Serial.println("); reconnecting...");

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("[WiFi] Reconnect initiated");
  }
}

void setupMQTT() {
  Serial.print("[MQTT] Server: ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.print(MQTT_PORT);
  Serial.print(", client ID: ");
  Serial.println(MQTT_CLIENT_ID);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void setupNTP() {
  Serial.print("[NTP] Configuring server: ");
  Serial.println(NTP_SERVER);
  configTime(0, 0, NTP_SERVER);
}

void maintainNTP() {
  if (timeSynced) {
    return;
  }

  time_t now = time(nullptr);
  if (now > 1000000000) {
    timeSynced = true;
    Serial.print("[NTP] Time synced: ");
    Serial.println(now);
    if (!powerSaveActive) {
      drawQuota();
    }
  }
}

const char* mqttStateName(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT:      return "MQTT_CONNECTION_TIMEOUT";
    case MQTT_CONNECTION_LOST:         return "MQTT_CONNECTION_LOST";
    case MQTT_CONNECT_FAILED:          return "MQTT_CONNECT_FAILED";
    case MQTT_DISCONNECTED:            return "MQTT_DISCONNECTED";
    case MQTT_CONNECTED:               return "MQTT_CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL:    return "MQTT_CONNECT_BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID:   return "MQTT_CONNECT_BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE:     return "MQTT_CONNECT_UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "MQTT_CONNECT_BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED:    return "MQTT_CONNECT_UNAUTHORIZED";
    default:                           return "UNKNOWN";
  }
}

void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
    return;
  }

  // Non-blocking reconnect: attempt at most every 5 seconds
  unsigned long now = millis();
  if (now - lastMqttAttempt < 5000) {
    return;
  }
  lastMqttAttempt = now;

  Serial.println("[MQTT] Connecting...");
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println("[MQTT] Connected");
    if (mqttClient.subscribe(MQTT_TOPIC_QUOTA)) {
      Serial.print("[MQTT] Subscribed to ");
      Serial.println(MQTT_TOPIC_QUOTA);
    } else {
      Serial.print("[MQTT] Subscribe to ");
      Serial.print(MQTT_TOPIC_QUOTA);
      Serial.println(" failed");
    }
  } else {
    int state = mqttClient.state();
    Serial.print("[MQTT] Connect failed, rc=");
    Serial.print(state);
    Serial.print(" (");
    Serial.print(mqttStateName(state));
    Serial.println("), will retry");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Message on ");
  Serial.print(topic);
  Serial.print(" (");
  Serial.print(length);
  Serial.println(" bytes)");

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.print("[MQTT] JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  // Expected payload: {"5h": {"pct": 70, "resets_at": 1756340000}, ...}
  String log;
  for (int i = 0; i < WINDOW_COUNT; i++) {
    QuotaWindow &win = quotaWindows[i];
    if (!doc[win.label].is<JsonObject>()) {
      continue;
    }

    JsonObject obj = doc[win.label].as<JsonObject>();
    if (!obj["pct"].is<int>()) {
      continue;
    }

    int pct = obj["pct"].as<int>();
    bool hasReset = obj["resets_at"].is<time_t>();
    time_t resetTs = hasReset ? obj["resets_at"].as<time_t>() : 0;

    if (pct < 0) {
      pct = 0;
    } else if (pct > 100) {
      pct = 100;
    }
    win.pctAvailable = pct;
    win.hasResetAt = hasReset;
    win.resetsAt = resetTs;

    if (log.length() > 0) {
      log += ", ";
    }
    log += win.label;
    log += "=";
    log += win.pctAvailable;
    log += "%";
  }

  if (log.length() > 0) {
    quotaDataReceived = true;
    lastQuotaChangeMillis = millis();
    Serial.print("[MQTT] Quota updated: ");
    Serial.println(log);
    if (powerSaveActive) {
      exitPowerSave();
    }
    drawQuota();
  } else {
    Serial.println("[MQTT] No known quota windows in payload, ignoring");
  }
}

void updateStubQuota() {
  for (int i = 0; i < WINDOW_COUNT; i++) {
    int delta = random(-5, 6) + (i == 1 ? -2 : 0);
    quotaWindows[i].pctAvailable += delta;
    if (quotaWindows[i].pctAvailable > 100) {
      quotaWindows[i].pctAvailable = 100;
    } else if (quotaWindows[i].pctAvailable < 0) {
      quotaWindows[i].pctAvailable = 0;
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
    int pctAvailable = win.pctAvailable;

    int16_t y0 = i * bandH;
    int16_t textY = y0 + margin;
    int16_t barY = textY + 22;
    int16_t barW = screenW - 2 * margin;
    int16_t barH = 26;
    int16_t filledW = (int32_t)barW * pctAvailable / 100;

    // Window label (static, safe to redraw every time)
    gfx->setTextSize(2);
    gfx->setTextColor(TEXT_COLOR);
    gfx->setCursor(margin, textY);
    gfx->print(win.label);

    // Right-align the percentage text on the right side of the window label.
    // Clear the full possible width first to avoid ghosting from longer values.
    gfx->setTextSize(2);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pctAvailable);
    int16_t pctW = strlen(pctBuf) * 6 * 2;
    int16_t maxPctW = strlen("100%") * 6 * 2;
    int16_t pctX = screenW - margin - pctW;
    gfx->fillRect(screenW - margin - maxPctW, textY, maxPctW, 16, BG_COLOR);
    gfx->setCursor(pctX, textY);
    gfx->print(pctBuf);

    // Redraw the bar background, then the fill, to erase the previous frame
    gfx->fillRect(margin, barY, barW, barH, BAR_BG_COLOR);
    uint16_t barColor = colorForPercent(pctAvailable);
    gfx->fillRect(margin, barY, filledW, barH, barColor);

    // Match the onboard LED to the 5h window gauge color
    if (i == 0) {
      setStatusLedFromRgb565(barColor);
    }

    // Show the reset countdown below the bar when we have a timestamp.
    // If the clock is not synced yet, show a placeholder instead.
    gfx->setTextSize(1);
    gfx->fillRect(margin, barY + barH + 4, barW, 10, BG_COLOR);
    if (win.hasResetAt) {
      gfx->setCursor(margin, barY + barH + 4);
      gfx->print("Reset: ");
      if (timeSynced) {
        char timeBuf[32];
        formatTimeLeft(win.resetsAt, timeBuf, sizeof(timeBuf));
        gfx->print(timeBuf);
      } else {
        gfx->print("--");
      }
    }

    // Small WiFi and MQTT status indicators on the first window
    if (i == 0) {
      drawWiFiIndicator(screenW - margin - 78, textY + 4);
      drawMqttIndicator(screenW - margin - 90, textY + 4);
    }
  }
}

void formatTimeLeft(time_t resetsAt, char* buf, size_t bufSize) {
  time_t now = time(nullptr);
  long secondsLeft = (long)(resetsAt - now);
  if (secondsLeft < 0) {
    secondsLeft = 0;
  }

  long days = secondsLeft / 86400;
  long hours = (secondsLeft % 86400) / 3600;
  long minutes = (secondsLeft % 3600) / 60;

  if (days > 0) {
    snprintf(buf, bufSize, "%ldd %ldh", days, hours);
  } else if (hours > 0) {
    snprintf(buf, bufSize, "%ldh %ldm", hours, minutes);
  } else {
    snprintf(buf, bufSize, "%ldm", minutes);
  }
}

void drawWiFiIndicator(int16_t x, int16_t y) {
  bool connected = (WiFi.status() == WL_CONNECTED);
  uint16_t color = connected ? WIFI_CONNECTED_COLOR : WIFI_DISCONNECTED_COLOR;

  // 8x8 status square
  gfx->fillRect(x, y, 8, 8, color);
}

void drawMqttIndicator(int16_t x, int16_t y) {
  uint16_t color = mqttClient.connected() ? MQTT_CONNECTED_COLOR : MQTT_DISCONNECTED_COLOR;

  // 8x8 status square
  gfx->fillRect(x, y, 8, 8, color);
}

void enterPowerSave() {
  powerSaveActive = true;
  ledcWrite(TFT_BL_PIN, 0);
  rgbLedWriteOrdered(STATUS_LED_PIN, LED_COLOR_ORDER_RGB, 0, 0, 0);
  Serial.println("[POWER] Entering power-save mode");
}

void exitPowerSave() {
  powerSaveActive = false;
  ledcWrite(TFT_BL_PIN, DISPLAY_BRIGHTNESS);
  Serial.println("[POWER] Leaving power-save mode");
}

void togglePowerSave() {
  if (powerSaveActive) {
    exitPowerSave();
    lastQuotaChangeMillis = millis();
    drawQuota();
  } else {
    enterPowerSave();
  }
}

uint16_t colorForPercent(int pctAvailable) {
  if (pctAvailable > 50) return BAR_HIGH_COLOR;
  if (pctAvailable > 20) return BAR_MED_COLOR;
  return BAR_LOW_COLOR;
}

void setStatusLedFromRgb565(uint16_t color) {
  // Convert RGB565 to RGB888
  uint8_t r = ((color >> 11) & 0x1F) << 3;
  uint8_t g = ((color >> 5)  & 0x3F) << 2;
  uint8_t b = ((color)       & 0x1F) << 3;

  // Scale by STATUS_LED_BRIGHTNESS (0-255)
  r = (uint8_t)((uint16_t)r * STATUS_LED_BRIGHTNESS / 255);
  g = (uint8_t)((uint16_t)g * STATUS_LED_BRIGHTNESS / 255);
  b = (uint8_t)((uint16_t)b * STATUS_LED_BRIGHTNESS / 255);

  // The onboard LED expects data in RGB order (not the WS2812 default GRB)
  rgbLedWriteOrdered(STATUS_LED_PIN, LED_COLOR_ORDER_RGB, r, g, b);
}
