#pragma once

#include <WiFi.h>

// WiFi connection helper with enhanced error diagnostics
bool wifi_connect(const String& ssid, const String& user, const String& pass, const char* hostname = "esp32device");

// Helper functions for WiFi diagnostics
const char* wifi_status_to_string(wl_status_t status);
int wifi_scan_for_network(const char* ssid);
