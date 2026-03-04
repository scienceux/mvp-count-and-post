#include "data_save.h"
#include <Arduino.h>
#include <SD.h>
#include "utilities_time.h"
#include "utilities_debug.h"

// Device ID — replace later with value read from flash/preferences
const char* DEVICE_ID = "device01";

// Full path to the current CSV file on SD card
// e.g. "/logs/2026-03-04_device01.csv"
char g_csvPath[64] = {0};

// Generates filename from today's date + device ID, sets g_csvPath
void NameTheCSVFile() {
    TimeExact t = WhatTimeIsItExactly();
    snprintf(g_csvPath, sizeof(g_csvPath),
        "/logs/%04d-%02d-%02d_%s.csv",
        t.year, t.month, t.day, DEVICE_ID);
    log_print("CSV path: " + String(g_csvPath));
}

// Creates CSV file if it doesn't exist, writes header row
bool CreateCSVFile() {
    if (g_csvPath[0] == '\0') {
        NameTheCSVFile();
    }

    // Create /logs directory if it doesn't exist
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }

    // If file already exists, nothing to do
    if (SD.exists(g_csvPath)) {
        log_print("CSV already exists: " + String(g_csvPath));
        return true;
    }

    // Create file and write header
    File f = SD.open(g_csvPath, FILE_WRITE);
    if (!f) {
        log_print("Failed to create CSV: " + String(g_csvPath));
        return false;
    }

    f.println("timestamp,weekday,event,device_id");
    f.close();
    log_print("CSV created: " + String(g_csvPath));
    return true;
}

// Weekday name lookup
static const char* kWeekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

// Appends one timestamped event row to the CSV
// eventType should be "ENTER" or "EXIT"
bool SaveEvent(const char* eventType) {
    if (g_csvPath[0] == '\0' || !SD.exists(g_csvPath)) {
        if (!CreateCSVFile()) return false;
    }

    TimeExact t = WhatTimeIsItExactly();

    char row[80];
    snprintf(row, sizeof(row),
        "%04d-%02d-%02d %s %02d:%02d:%02d,%s,%s",
        t.year, t.month, t.day,
        kWeekdays[t.weekday],
        t.hour, t.minute, t.second,
        eventType, DEVICE_ID);

    File f = SD.open(g_csvPath, FILE_APPEND);
    if (!f) {
        log_print("SaveEvent: failed to open CSV");
        return false;
    }

    f.println(row);
    f.flush();
    f.close();
    log_print("Event saved: " + String(row));
    return true;
}