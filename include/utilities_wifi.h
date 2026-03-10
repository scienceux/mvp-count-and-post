#pragma once

#include <WiFi.h>

// WiFi connection helper with enhanced error diagnostics
bool wifi_connect(const char* ssid, const char* user, const char* pass);

// Helper functions for WiFi diagnostics
const char* wifi_status_to_string(wl_status_t status);
int wifi_scan_for_network(const char* ssid);
