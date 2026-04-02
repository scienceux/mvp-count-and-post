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

// Saves the queued events to the current CSV file on SD card
bool SaveQueToCSV() {
    if (g_csvPath[0] == '\0') {
        log_print("CSV path not set, cannot save");
        return false;
    }

    File f = SD.open(g_csvPath, FILE_APPEND);
    if (!f) {
        log_print("Failed to open CSV for appending");
        return false;
    }

    for (const String& row : g_recentCSVRows) {
        if (row.length() > 0) {
            f.println(row);
        }
    }
    f.close();
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


// Upload que to server (when using 'que stored in memory until scucessfully porcessed' method, which should be deprecated soon)
bool UploadQueToServer() {
    for (const String& row : g_recentCSVRows) {
        if (row.length() > 0) {
            if (!UploadEventToServer(row.c_str())) {
                log_print("Failed to upload event: " + row);
                return false;
            }
        }
    }
    return true;
}

bool syncCSVwithServer() {
    // Read csv where uploaded_timestamp is "not uploaded" and upload those rows, then update uploaded_timestamp to current timestamp
    File f = SD.open(g_csvPath, FILE_READ);
    if (!f) {
        log_print("Failed to open CSV for reading");
        return false;
    }

    // Temporary array to hold rows with "not uploaded" status
    String rowsToUpload[50] = {""};
    int uploadCount = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.endsWith("not uploaded")) {
            if (uploadCount < 50) {
                rowsToUpload[uploadCount++] = line;
            } else {
                log_print("Too many rows to upload, skipping some");
                break;
            }
        }
    }

    // Upload each row and update status in CSV
    for (int i = 0; i < uploadCount; ++i) {
        String& row = rowsToUpload[i];
        if (UploadEventToServer(row.c_str())) {
            // Update row in CSV to mark as uploaded
            String updatedRow = row.substring(0, row.length() - 12) + "uploaded";
            // Here you would implement logic to replace the old row with updatedRow in the CSV file
            // This is a bit complex due to file handling, so it is left as a comment for now
            // updateCSVRow(g_csvPath, row, updatedRow);
        } else {
            log_print("Failed to upload event: " + row);
        }
    }
}

bool updateCSVRow(const char* path, const String& oldRow, const String& newRow) {
    // This function would read the entire CSV, replace oldRow with newRow, and write it back.
    // Due to the complexity of file handling on embedded systems, this is a placeholder.
    // In a real implementation, you might read line by line and write to a new file, then replace the old file.

    log_print("updateCSVRow is not implemented. Would replace: " + oldRow + " with: " + newRow);
    return false;
}

// Called by main after timer
// Process que
bool LogQuedEvents() {
    if (!SaveQueToCSV()) {
        log_print("Failed to save que to CSV");
        return false;
    }
    if (!UploadQueToServer()) {
        log_print("Failed to upload que to server");
        return false;
    }
    ClearQue();
    return true;
}


//============================================================

// // Old: Upload single event to google apps sheet
// bool UploadEventToServer(const char* row) {
//     if (WiFi.status() != WL_CONNECTED) {
//         log_print("Not connected to WiFi, cannot upload event");
//         return false;
//     }

//     const char* basename = strrchr(g_csvPath, '/');
//     basename = basename ? basename + 1 : g_csvPath;

//     String query = "eventId=" + UrlEncodeFormValue(EVENT_NAME) +
//                    "&rows=" + UrlEncodeFormValue(row);
//     String requestUrl = String(API_URL) + "?" + query;

//     WiFiClientSecure client;
//     client.setInsecure(); // Google certs change often; insecure is usually safer for IoT
    
//     HTTPClient http;
    
//     // Use query params with GET to avoid current POST path issues.
//     http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
//     http.begin(client, requestUrl);
//     http.addHeader("User-Agent", "PosterBuddyESP32");

//     int httpCode = http.GET();

//     String response = http.getString();
    
//     if (httpCode > 0) {
//         log_print("HTTP Code: " + String(httpCode));
//         // if (httpCode == 200) {
//         //     log_print("Success: " + response);
//         // }
//     } else {
//         log_print("Error: " + http.errorToString(httpCode));
//     }

//     http.end();
//     return httpCode == 200;
// }

// static String UrlEncodeFormValue(const char* value) {
//     String encoded;
//     while (*value) {
//         const unsigned char ch = static_cast<unsigned char>(*value++);
//         if ((ch >= 'a' && ch <= 'z') ||
//             (ch >= 'A' && ch <= 'Z') ||
//             (ch >= '0' && ch <= '9') ||
//             ch == '-' || ch == '_' || ch == '.' || ch == '*') {
//             encoded += static_cast<char>(ch);
//         } else if (ch == ' ') {
//             encoded += '+';
//         } else {
//             char hex[4];
//             snprintf(hex, sizeof(hex), "%%%02X", ch);
//             encoded += hex;
//         }
//     }
//     return encoded;
// }




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

    f.println("timestamp,weekday,event,device_id,time_source,uploaded_timestamp");
    f.close();
    log_print("CSV created: " + String(g_csvPath));
    return true;
}


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
        g_wifiSetTime ? "ntp" : "estimated",
    "not uploaded");

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

