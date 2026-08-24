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

   #define NTP_SERVER "pool.ntp.org"
   #define POWER_SAVE_AFTER_MINUTES 60
    ```

`config.h` is listed in `.gitignore`, so your real credentials will not
be committed.

## MQTT payload

The device subscribes to `MQTT_TOPIC_QUOTA` and expects a JSON object where
each quota window maps to an object with `pct` (remaining quota, 0-100) and
`resets_at` (Unix epoch timestamp when the window resets):

```json
{"5h": {"pct": 70, "resets_at": 1756340000}, "7d": {"pct": 93, "resets_at": 1756340000}}
```

`resets_at` is optional. Windows without a `resets_at` value show no
`Reset:` line.

The ESP32 syncs its clock from `NTP_SERVER` and renders the time left until
reset below each bar as `Reset: 1h 23m`. The countdown is hidden until the
clock is synced.

Publish the message with the retain flag set so the device receives the
latest values immediately when it (re)connects:

```sh
mosquitto_pub -h 192.168.x.x -t quota/llm -r -m '{"5h": {"pct": 70, "resets_at": 1756340000}, "7d": {"pct": 93, "resets_at": 1756340000}}'
```

Until the first valid message arrives, the display shows randomly generated
stub data.

## Power saving

If no quota change has been received for `POWER_SAVE_AFTER_MINUTES`, the
device turns off the TFT backlight and the onboard RGB LED. WiFi, MQTT and
NTP keep running, and any new MQTT message wakes the device immediately,
restoring the backlight and redrawing the display.

You can also press the **BOOT button** (GPIO 9) to toggle power-save mode
on or off at any time.

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
