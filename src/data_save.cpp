#include "data_save.h"
#include <Arduino.h>
#include <SD.h>
#include "utilities_time.h"
#include "utilities_debug.h"
#include "WiFi.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"


// CSV name format (WiFi on):  /logs/2026-03-17_1602_device01.csv
// CSV name format (no WiFi):  /logs/After-2026-03-17_1602_device01.csv

// Device ID and event name — read from global config at point of use via .c_str()
// Do NOT cache .c_str() at startup; String internal buffer may move after SD load
#define DEVICE_ID (g_deviceName.c_str())
#define EVENT_NAME (g_eventName.c_str())
const char* API_URL = "https://script.google.com/macros/s/AKfycbxievFNu5o6SfLJm4da9DU8ci3qPRs9zRLqpAAVbiCkEpDq82b6GQKW-QwOY3Z-Erg/exec";

// Full path to the current CSV file on SD card
char g_csvPath[64] = {0};
// Staging path for events not yet uploaded to server
char g_queuePath[96] = {0};

// If wifi, switchted to true
bool g_wifiSetTime = false;

// Weekday name lookup
static const char* kWeekdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };


//============================================================
// The QUE: new enter/exit events are saved to global array then logged/uploaded every X seconds to avoid blocking counts
// create que
// add event row to que
// save to csv
// upload to server
// clear que
// global array to hold the most recent CSV event rows
String g_recentCSVRows[10] = {""};

// clear que
void ClearQue() {
    for (int i = 0; i < 10; ++i) {
        g_recentCSVRows[i] = "";
    }
}

// adds a new event row to the global array
void queueCSVRowForSaving(const String& newRow) {
    // Shift existing rows down
    for (int i = 9; i > 0; --i) {
        g_recentCSVRows[i] = g_recentCSVRows[i - 1];
    }
    // Add new row at the front
    g_recentCSVRows[0] = newRow;
}

// Create CSV row from an event type (add time, etc.) and add to que
void addEventToQue(const char* eventType) {
    TimeExact t = WhatTimeIsItExactly();

    char row[96];
    snprintf(row, sizeof(row),
        "%04d-%02d-%02d %02d:%02d:%02d,%s,%s,%s,%s",
        t.year, t.month, t.day,
        t.hour, t.minute, t.second,
        kWeekdays[t.weekday],
        eventType, DEVICE_ID,
        g_wifiSetTime ? "timefromwifi" : "estimated",
    "not uploaded");

    queueCSVRowForSaving(String(row));
}

// Flushes in-memory queue to the queue CSV on SD, then clears in-memory queue.
// Always called regardless of WiFi status — ensures data survives a reboot.
static bool FlushQueToQueueCSV() {
    if (g_queuePath[0] == '\0') {
        log_print("Queue path not set, cannot flush");
        return false;
    }

    bool hasRows = false;
    for (const String& row : g_recentCSVRows) {
        if (row.length() > 0) { hasRows = true; break; }
    }
    if (!hasRows) return true;

    File f = SD.open(g_queuePath, FILE_APPEND);
    if (!f) {
        log_print("Failed to open queue CSV for appending");
        return false;
    }

    for (const String& row : g_recentCSVRows) {
        if (row.length() > 0) {
            f.println(row);
        }
    }
    f.close();
    ClearQue();
    return true;
}

static String UrlEncodeFormValue(const char* value) {
    String encoded;
    while (*value) {
        const unsigned char ch = static_cast<unsigned char>(*value++);
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '*') {
            encoded += static_cast<char>(ch);
        } else if (ch == ' ') {
            encoded += '+';
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", ch);
            encoded += hex;
        }
    }
    return encoded;
}

// Upload single event to google apps sheet
bool UploadEventToServer(const char* row) {
    if (WiFi.status() != WL_CONNECTED) {
        log_print("Not connected to WiFi, cannot upload event");
        return false;
    }

    const char* basename = strrchr(g_csvPath, '/');
    basename = basename ? basename + 1 : g_csvPath;

    String query = "eventId=" + UrlEncodeFormValue(EVENT_NAME) +
                   "&rows=" + UrlEncodeFormValue(row);
    String requestUrl = String(API_URL) + "?" + query;

    WiFiClientSecure client;
    client.setInsecure(); // Google certs change often; insecure is usually safer for IoT
    
    HTTPClient http;
    
    // Use query params with GET to avoid current POST path issues.
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.begin(client, requestUrl);
    http.addHeader("User-Agent", "PosterBuddyESP32");

    int httpCode = http.GET();

    String response = http.getString();
    
    if (httpCode > 0) {
        log_print("HTTP Code: " + String(httpCode));
        // if (httpCode == 200) {
        //     log_print("Success: " + response);
        // }
    } else {
        log_print("Error: " + http.errorToString(httpCode));
    }

    http.end();
    return httpCode == 200;
}


// Reads queue CSV, uploads each row to server, on full success moves rows to uploaded CSV and clears queue CSV.
// Stops on first upload failure and leaves queue CSV intact for retry next cycle.
static bool UploadQueueCSVToServer() {
    if (WiFi.status() != WL_CONNECTED) {
        log_print("No WiFi, skipping upload");
        return false;
    }

    if (!SD.exists(g_queuePath)) {
        return true; // Nothing pending
    }

    // Read up to 100 rows from queue CSV into memory
    String rows[100];
    int count = 0;
    bool allRead = true;
    {
        File f = SD.open(g_queuePath, FILE_READ);
        if (!f) {
            log_print("Failed to open queue CSV for reading");
            return false;
        }
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            if (count < 100) {
                rows[count++] = line;
            } else {
                allRead = false;
                break;
            }
        }
        f.close();
    }

    if (count == 0) {
        if (allRead) SD.remove(g_queuePath); // empty file cleanup
        return true;
    }

    // Upload row by row; on failure leave queue CSV intact for retry
    for (int i = 0; i < count; ++i) {
        if (!UploadEventToServer(rows[i].c_str())) {
            log_print("Upload failed at row " + String(i) + ", retrying next cycle");
            return false;
        }
    }

    // All uploaded — append to permanent uploaded CSV, marking rows as uploaded
    {
        File f = SD.open(g_csvPath, FILE_APPEND);
        if (f) {
            for (int i = 0; i < count; ++i) {
                String row = rows[i];
                // Replace trailing "not uploaded" with "uploaded"
                if (row.endsWith("not uploaded")) {
                    row = row.substring(0, row.length() - 12) + "uploaded";
                }
                f.println(row);
            }
            f.close();
        } else {
            log_print("Could not open uploaded CSV; rows may re-upload next cycle");
        }
    }

    // Remove processed rows from queue CSV
    if (allRead) {
        SD.remove(g_queuePath);
        log_print("Queue cleared. " + String(count) + " rows moved to uploaded CSV");
    } else {
        // More rows remain — rewrite queue CSV without the processed rows
        log_print("Queue >100 rows; rewriting remainder after uploading " + String(count));
        const char* tmpPath = "/logs/qtmp.csv";
        {
            File src = SD.open(g_queuePath, FILE_READ);
            File dst = SD.open(tmpPath, FILE_WRITE);
            if (src && dst) {
                int skip = count;
                while (src.available()) {
                    String line = src.readStringUntil('\n');
                    if (skip > 0) { --skip; continue; }
                    line.trim();
                    if (line.length() > 0) dst.println(line);
                }
            }
            if (src) src.close();
            if (dst) dst.close();
        }
        SD.remove(g_queuePath);
        {
            File src = SD.open(tmpPath, FILE_READ);
            File dst = SD.open(g_queuePath, FILE_WRITE);
            if (src && dst) {
                uint8_t buf[128];
                size_t br;
                while ((br = src.read(buf, sizeof(buf))) > 0) dst.write(buf, br);
            }
            if (src) src.close();
            if (dst) dst.close();
        }
        SD.remove(tmpPath);
    }

    return true;
}

// Called by main on UploadData timer.
// Phase 1 (always): flush in-memory queue to queue CSV on SD, then clear in-memory queue.
// Phase 2 (WiFi only): upload queue CSV to server; on success move rows to uploaded CSV and clear queue CSV.
bool LogQuedEvents() {
    FlushQueToQueueCSV();

    if (WiFi.status() != WL_CONNECTED) {
        log_print("No WiFi — queue saved to SD, upload deferred");
        return false;
    }

    return UploadQueueCSVToServer();
}





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
        bool isQueue  = (strstr(name, "-queue-not_uploaded_yet") != nullptr);

        if (isCsv && !isAfter && !isNotime && !isQueue && strcmp(name, best) > 0) {
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

    // Queue path is always fixed regardless of clock state
    snprintf(g_queuePath, sizeof(g_queuePath),
        "/logs/%s-%s-queue-not_uploaded_yet.csv",
        g_eventName, g_deviceName);

    if (t.valid) {
        // snprintf(g_csvPath, sizeof(g_csvPath),
        //     "/logs/%04d-%02d-%02d_%02d%02d_%s.csv",
        //     t.year, t.month, t.day, t.hour, t.minute, DEVICE_ID); // e.g. /logs/2026-03-17_1602_device01.csv
        snprintf(g_csvPath, sizeof(g_csvPath),
            "/logs/%s-%s.csv",
            g_eventName, g_deviceName); // e.g. /logs/2026-03-17_1602_device01.csv        
            
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

    f.println("timestamp,weekday,event,device_id,time_source,uploaded_timestamp");
    f.close();
    log_print("CSV created: " + String(g_csvPath));
    return true;
}

