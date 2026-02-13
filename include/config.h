#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Initialize config (loads values from NVS, call once in setup)
void ConfigInit();

// Getters
String GetDeviceName();
String GetMode();
String GetEventName();
String GetWiFiSSID();
String GetWiFiPassword();

// Setters
void SetDeviceName(const String& value);
void SetMode(const String& value);
void SetEventName(const String& value);
void SetWiFiSSID(const String& value);
void SetWiFiPassword(const String& value);

// Process serial commands for device configuration (call in loop)
// Commands: IDENTIFY, GET_CONFIG, SET_NAME, SET_MODE, SET_EVENT, SET_WIFI, LED_ON, LED_OFF, BLINK
bool ProcessConfigCommand();

#endif // CONFIG_H
