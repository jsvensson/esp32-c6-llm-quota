#ifndef CONFIG_H
#define CONFIG_H

// Copy this file to config.h and replace the placeholders with your
// network credentials. config.h is gitignored so your real credentials
// are not committed.

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// MQTT broker (plain TCP, no auth)
#define MQTT_HOST        "192.168.x.x"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32-c6-quota"
#define MQTT_TOPIC_QUOTA "quota/llm"

// NTP server for converting reset timestamps into time-left strings
#define NTP_SERVER "pool.ntp.org"

// Power saving: turn off backlight and LED after this many minutes of no
// quota changes. A message that changes the quota values wakes the device
// immediately; messages with unchanged values do not.
#define POWER_SAVE_AFTER_MINUTES 60

// Set to 1 to show the WiFi and MQTT status dots, 0 to hide them.
#define SHOW_CONNECTIVITY_DOTS 1

#endif // CONFIG_H
