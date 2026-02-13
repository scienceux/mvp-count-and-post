#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static bool prefsInitialized = false;

// Cached config values (loaded once at init)
static String g_deviceName = "";
static String g_deviceMode = "";
static String g_eventName = "";
static String g_wifiSsid = "";
static String g_wifiPass = "";

static const char* MODEL = "XIAO ESP32S3";

void ConfigInit()
{
    if (prefsInitialized) return;
    prefs.begin("labeler", false);  // Keep open, read-write mode
    g_deviceName = prefs.getString("name", "");
    g_deviceMode = prefs.getString("mode", "");
    g_eventName = prefs.getString("event", "");
    g_wifiSsid = prefs.getString("ssid", "");
    g_wifiPass = prefs.getString("pass", "");
    prefsInitialized = true;
}

String GetDeviceName() { 
 // Read device name directly from NVM
  Preferences prefs;
  prefs.begin("labeler", true);  // read-only
  String deviceName = prefs.getString("name", "");
  prefs.end(); 
  return deviceName;
}
String GetMode() { return g_deviceMode; }
String GetEventName() { return g_eventName; }
String GetWiFiSSID() { return g_wifiSsid; }
String GetWiFiPassword() { return g_wifiPass; }

void SetDeviceName(const String& value) {
    g_deviceName = value;
    prefs.putString("name", value);
}

void SetMode(const String& value) {
    g_deviceMode = value;
    prefs.putString("mode", value);
}

void SetEventName(const String& value) {
    g_eventName = value;
    prefs.putString("event", value);
}

void SetWiFiSSID(const String& value) {
    g_wifiSsid = value;
    prefs.putString("ssid", value);
}

void SetWiFiPassword(const String& value) {
    g_wifiPass = value;
    prefs.putString("pass", value);
}

// Read a full line from serial (blocking until newline)
static String readSerialLine() {
    String line = Serial.readStringUntil('\n');
    line.trim();
    return line;
}

// Process serial commands for device configuration
// Returns true if a command was handled
bool ProcessConfigCommand()
{
    if (!Serial.available()) {
        return false;
    }

    String cmd = readSerialLine();
    cmd.trim();
    if (cmd.length() == 0) return false;

    if (cmd == "IDENTIFY") {
        Serial.print("MODEL=");
        Serial.print(MODEL);
        Serial.print(";NAME=");
        Serial.print(g_deviceName);
        Serial.print(";MODE=");
        Serial.print(g_deviceMode);
        Serial.print(";EVENT=");
        Serial.print(g_eventName);
        Serial.print(";WIFI_SSID=");
        Serial.println(g_wifiSsid);
        return true;
    } 
    else if (cmd == "LED_ON") {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("OK");
        return true;
    } 
    else if (cmd == "LED_OFF") {
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("OK");
        return true;
    } 
    else if (cmd.startsWith("BLINK")) {
        int count = cmd.substring(5).toInt();
        if (count <= 0) count = 3;
        for (int i = 0; i < count; i++) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(200);
        }
        Serial.println("OK");
        return true;
    } 
    else if (cmd.startsWith("SET_NAME")) {
        String name = cmd.substring(8);
        name.trim();
        SetDeviceName(name);
        Serial.println("OK");
        return true;
    } 
    else if (cmd.startsWith("SET_MODE")) {
        String mode = cmd.substring(8);
        mode.trim();
        SetMode(mode);
        Serial.println("OK");
        return true;
    } 
    else if (cmd.startsWith("SET_EVENT")) {
        String eventVal = cmd.substring(9);
        eventVal.trim();
        SetEventName(eventVal);
        Serial.println("OK");
        return true;
    } 
    else if (cmd.startsWith("SET_WIFI")) {
        String payload = cmd.substring(8);
        payload.trim();
        int sep = payload.indexOf('|');
        if (sep < 0) {
            Serial.println("ERR");
        } else {
            String ssid = payload.substring(0, sep);
            String pass = payload.substring(sep + 1);
            ssid.trim();
            pass.trim();
            SetWiFiSSID(ssid);
            SetWiFiPassword(pass);
            Serial.println("OK");
        }
        return true;
    } 
    else if (cmd == "GET_CONFIG") {
        Serial.print("NAME=");
        Serial.print(g_deviceName);
        Serial.print(";MODE=");
        Serial.print(g_deviceMode);
        Serial.print(";EVENT=");
        Serial.print(g_eventName);
        Serial.print(";WIFI_SSID=");
        Serial.println(g_wifiSsid);
        return true;
    }

    return false;  // Unknown command
}
