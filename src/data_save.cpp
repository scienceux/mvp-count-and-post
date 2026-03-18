#include "data_save.h"
#include <Arduino.h>
#include <SD.h>
#include "utilities_time.h"
#include "utilities_debug.h"

// CSV name format (WiFi on):  /logs/2026-03-17_1602_device01.csv
// CSV name format (no WiFi):  /logs/After-2026-03-17_1602_device01.csv

// Device ID — replace later with value read from flash/preferences
const char* DEVICE_ID = "living-room";

// Full path to the current CSV file on SD card
char g_csvPath[64] = {0};

bool g_wifiSetTime = false;


// Reads the last data row of a CSV and sets the system RTC to that timestamp.
// Used as a best-effort clock when NTP is unavailable.
static void SetClockFromLastCSVEntry(const char* path) {
    File f = SD.open(path);
    if (!f) return;

    char lastRow[96] = {0};
    while (f.available()) {
        f.readBytesUntil('\n', lastRow, sizeof(lastRow) - 1);
    }
    f.close();

    // Row format: "2026-03-18 16:02:00,Wednesday,ENTER,hallway,ntp"
    struct tm t = {};
    if (sscanf(lastRow, "%4d-%2d-%2d %2d:%2d:%2d",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) == 6) {
        t.tm_year -= 1900;
        t.tm_mon  -= 1;
        time_t est = mktime(&t);
        struct timeval tv = { est, 0 };
        settimeofday(&tv, nullptr);
        log_print("Clock set from last CSV entry: " + String(lastRow));
    } else {
        log_print("Could not parse timestamp from last CSV row");
    }
}

// Scans /logs for the alphabetically-last CSV that is not an "After-" or "notime-" file.
// Writes just the filename (no path) into outName. Returns true on success.
static bool FindLastCSVName(char* outName, size_t outSize) {
    File dir = SD.open("/logs");
    if (!dir || !dir.isDirectory()) return false;

    char best[64] = {0};
    File entry;
    while ((entry = dir.openNextFile())) {
        const char* raw = entry.name();
        const char* name = strrchr(raw, '/');
        name = name ? name + 1 : raw;

        size_t len = strlen(name);
        bool isCsv    = (len > 4 && strcasecmp(name + len - 4, ".csv") == 0);
        bool isAfter  = (strncmp(name, "After-",  6) == 0);
        bool isNotime = (strncmp(name, "notime-", 7) == 0);

        if (isCsv && !isAfter && !isNotime && strcmp(name, best) > 0) {
            strlcpy(best, name, sizeof(best));
        }
        entry.close();
    }
    dir.close();

    if (best[0] == '\0') return false;
    strlcpy(outName, best, outSize);
    return true;
}

// Generates filename from today's date + time + device ID, sets g_csvPath.
// Falls back to "After-<last-csv-name>" when RTC is not NTP-synced (no WiFi).
void NameTheCSVFile() {
    TimeExact t = WhatTimeIsItExactly();

    if (t.valid) {
        snprintf(g_csvPath, sizeof(g_csvPath),
            "/logs/%04d-%02d-%02d_%02d%02d_%s.csv",
            t.year, t.month, t.day, t.hour, t.minute, DEVICE_ID); // e.g. /logs/2026-03-17_1602_device01.csv
    } else {
        char refName[64] = {0};
        if (FindLastCSVName(refName, sizeof(refName))) {
            snprintf(g_csvPath, sizeof(g_csvPath), "/logs/After-%s", refName);

            // Set RTC to last known time so row timestamps aren't epoch-zero
            char refPath[72];
            snprintf(refPath, sizeof(refPath), "/logs/%s", refName);
            SetClockFromLastCSVEntry(refPath);
            // g_wifiSetTime stays false — time is estimated
        } else {
            snprintf(g_csvPath, sizeof(g_csvPath),
                "/logs/notime-%lu_%s.csv", (unsigned long)(millis() / 1000), DEVICE_ID);
        }
    }

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

    f.println("timestamp,weekday,event,device_id,time_source");
    f.close();
    log_print("CSV created: " + String(g_csvPath));
    return true;
}

// Weekday name lookup
static const char* kWeekdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// Appends one timestamped event row to the CSV
// eventType should be "ENTER" or "EXIT"
bool SaveEvent(const char* eventType) {
    if (g_csvPath[0] == '\0' || !SD.exists(g_csvPath)) {
        if (!CreateCSVFile()) return false;
    }

    TimeExact t = WhatTimeIsItExactly();

    char row[96];
    snprintf(row, sizeof(row),
        "%04d-%02d-%02d %02d:%02d:%02d,%s,%s,%s,%s",
        t.year, t.month, t.day,
        t.hour, t.minute, t.second,
        kWeekdays[t.weekday],
        eventType, DEVICE_ID,
        g_wifiSetTime ? "ntp" : "estimated");

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

