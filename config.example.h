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

#endif // CONFIG_H
