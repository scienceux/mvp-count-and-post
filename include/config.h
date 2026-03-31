#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Initialize config (loads values from NVS, call once in setup)
void ConfigInit();

// Global variables for config values
extern String g_deviceName;
extern String g_deviceMode;
extern String g_eventName;
extern String g_wifiSsid;
extern String g_wifiUser;  // unused for WPA2-PSK, but keep for potential future use
extern String g_wifiPass;

// Getters
String GetDeviceName();
String GetMode();
String GetEventName();
String GetWiFiSSID();
String GetWiFiPassword();

// Setters
void setConfigFromSD();  // Load config values from SD card's config.txt and set global vars
void SetDeviceName(const String& value);
void SetMode(const String& value);
void SetEventName(const String& value);
void SetWiFiSSID(const String& value);
void SetWiFiPassword(const String& value);

// Default config constants
extern const int CAMERA_FPS;

// Process serial commands for device configuration (call in loop)
// Commands: IDENTIFY, GET_CONFIG, SET_NAME, SET_MODE, SET_EVENT, SET_WIFI, LED_ON, LED_OFF, BLINK
bool ProcessConfigCommand();

#endif // CONFIG_H
