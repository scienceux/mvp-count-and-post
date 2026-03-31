#include "config.h"
#include <Preferences.h>
#include <SD.h>
#include "utilities_debug.h"

static Preferences prefs;
static bool prefsInitialized = false;

// Cached config values (loaded once at init)
String g_deviceName = "";
String g_deviceMode = "ENTEREXIT";
String g_eventName = "MIKETESTING";
String g_wifiSsid = "";
String g_wifiUser = "";  // unused for WPA2-PSK, but keep for potential future use
String g_wifiPass = "";

static const char* MODEL = "XIAO ESP32S3";


// DEFAULT Config (if not set by SD config.txt)
//==================================================
// ---- adjust these as needed ----
// ENTEREXIT, OCCUPANCY, VIDEO_FOR_TRAINING (for training)
const char* DEVICE_MODE = "ENTEREXIT"; 
const int CAMERA_FPS = 4;


// Set from SD now. These are old hardcoded defaults
// WiFi config (STA)
// const char* WIFI_SSID = "posterbuddyTR";
// const char* WIFI_USER = ""; // unused for WPA2-PSK
// const char* WIFI_PASS = "money4connie";

// const char* WIFI_SSID = "SolisWiFi_usf";
// const char* WIFI_USER = ""; // unused for WPA2-PSK
// const char* WIFI_PASS = "47319451";
//==================================================


// Pull global config vars from SD card
void setConfigFromSD() 
{
    // Format is like:
    // device_name_from_sd=hallway
    // wifi_ssid_from_sd=easl2026
    // wifi_pass_from_sd=thepassword
    // Read this to replace hardcoded config values above, e.g. DEVICE_MODE, WIFI_SSID, etc.    

    if (SD.exists("/config.txt")) {
        File configFile = SD.open("/config.txt");
        if (configFile) {
            while (configFile.available()) {
                String line = configFile.readStringUntil('\n');
                line.trim();
                if (line.startsWith("device_name_from_sd=")) {
                g_deviceName = line.substring(20);
                } else if (line.startsWith("wifi_ssid_from_sd=")) {
                g_wifiSsid = line.substring(18);
                } else if (line.startsWith("wifi_pass_from_sd=")) {
                g_wifiPass = line.substring(18);
                }
            }
            configFile.close();
        }
    } else {
        // No config on SD, keep using hardcoded defaults
        g_deviceName = "unnamed_device";
        log_print("Needs a config.txt on SD card with device_name_from_sd=, wifi_ssid_from_sd=, and wifi_pass_from_sd= values");
    }
}

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
