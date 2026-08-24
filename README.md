# ESP32-C6 Quota Display with WiFi

Arduino sketch for the ESP32-C6 that shows quota usage bars on an ST7789
display and connects to WiFi.

## Requirements

- **ESP32 board core** (`esp32:esp32`) v3.x or newer — the sketch uses the
  unified LEDC/RGB-LED API (`ledcAttach`, `rgbLedWriteOrdered`) introduced in
  v3.0. Install with:

  ```sh
  arduino-cli core install esp32:esp32
  ```

  (Requires the Espressif boards manager URL if not already configured.)

- **GFX Library for Arduino** (by moononournation) — install with:

  ```sh
  arduino-cli lib install "GFX Library for Arduino"
  ```

`WiFi` and `SPI` are bundled with the ESP32 core and need no separate install.

## Configure WiFi credentials

1. Copy the example config file:

   ```sh
   cp wifi_config.example.h wifi_config.h
   ```

2. Edit `wifi_config.h` and set your network credentials:

   ```cpp
   #define WIFI_SSID     "your-actual-ssid"
   #define WIFI_PASSWORD "your-actual-password"
   ```

`wifi_config.h` is listed in `.gitignore`, so your real credentials will not
be committed.

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
