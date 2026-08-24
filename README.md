# ESP32-C6 Quota Display with WiFi and MQTT

Arduino sketch for the ESP32-C6 that shows quota usage bars on an ST7789
display. Quota data arrives over MQTT; WiFi is used to reach the broker.

## Requirements

- **ESP32 board core** (`esp32:esp32`) v3.x or newer — the sketch uses the
  unified LEDC/RGB-LED API (`ledcAttach`, `rgbLedWriteOrdered`) introduced in
  v3.0. Install with:

  ```sh
  arduino-cli core install esp32:esp32
  ```

  (Requires the Espressif boards manager URL if not already configured.)

- **GFX Library for Arduino** (by moononournation), **PubSubClient** (by
  Nick O'Leary), and **ArduinoJson** (by Benoit Blanchon) — install with:

  ```sh
  arduino-cli lib install "GFX Library for Arduino" "PubSubClient" "ArduinoJson"
  ```

`WiFi` and `SPI` are bundled with the ESP32 core and need no separate install.

## Configure credentials

1. Copy the example config file:

   ```sh
   cp config.example.h config.h
   ```

2. Edit `config.h` and set your WiFi network credentials and MQTT broker
   settings:

   ```cpp
   #define WIFI_SSID     "your-actual-ssid"
   #define WIFI_PASSWORD "your-actual-password"

   #define MQTT_HOST        "192.168.x.x"
   #define MQTT_PORT        1883
   #define MQTT_CLIENT_ID   "esp32-c6-quota"
   #define MQTT_TOPIC_QUOTA "quota/llm"
   ```

`config.h` is listed in `.gitignore`, so your real credentials will not
be committed.

## MQTT payload

The device subscribes to `MQTT_TOPIC_QUOTA` and expects a JSON object that
maps each quota window label to an integer percentage of remaining
quota (0-100):

```json
{"5h": 70, "7d": 93}
```

In the example above, `70` means 70 % left on the 5h window and `93`
means 93 % left on the 7d window.

Publish the message with the retain flag set so the device receives the
latest values immediately when it (re)connects:

```sh
mosquitto_pub -h 192.168.x.x -t quota/llm -r -m '{"5h": 70, "7d": 93}'
```

Until the first valid message arrives, the display shows randomly generated
stub data.

## Build

```sh
just build
```

The `justfile` stages the sketch in `.build/esp32-c6-llm-quota/` so the
folder name matches the `.ino` file, then compiles it with `arduino-cli`.

## Flash and monitor

```sh
just flash
just monitor
```

Change `PORT` in the `justfile` if your board is on a different serial port.
