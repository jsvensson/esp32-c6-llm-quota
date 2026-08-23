# ESP32-C6 Quota Display with WiFi

Arduino sketch for the ESP32-C6 that shows quota usage bars on an ST7789
display and connects to WiFi.

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

The sketch folder name (`add-wifi`) matches the main sketch file
(`add-wifi.ino`), so the default `justfile` recipe compiles the current
directory.

## Flash and monitor

```sh
just flash
just monitor
```

Change `PORT` in the `justfile` if your board is on a different serial port.
