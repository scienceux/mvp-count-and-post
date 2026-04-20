#pragma once
// DEVICE_ID and EVENT_NAME are macros defined in data_save.cpp via g_deviceName/g_eventName
extern char g_csvPath[64];
extern char g_queuePath[96];
extern bool g_wifiSetTime; // true = time was NTP-synced, false = estimated from CSV

void NameTheCSVFile();
bool CreateCSVFile();

// Que for events waiting to be saved/uploaded
void addEventToQue(const char* eventType);
bool LogQuedEvents();
