#pragma once

extern const char* DEVICE_ID;
extern char g_csvPath[64];
extern bool g_wifiSetTime; // true = time was NTP-synced, false = estimated from CSV

void NameTheCSVFile();
bool CreateCSVFile();
bool SaveEvent(const char* eventType);