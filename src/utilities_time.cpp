#include "utilities_time.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "utilities_wifi.h"
#include "utilities_debug.h"

// Function quick reference:
// setupClock(ssid, user, pass) -> bool success syncing RTC via NTP over Wi-Fi.
// WhatTimeIsItExactly() -> TimeExact local time snapshot with milliseconds and validity.
// SecondsToMs(seconds) -> uint32_t milliseconds from float seconds.
// CreateTimer(name, intervalSeconds) -> bool success creating/updating named timer.
// RestartTimer(name) -> bool success resetting named timer start time.
// GetTimerLimitSeconds(name) -> float configured interval seconds or -1.0f if missing.
// GetTimerCurrent(name) -> float elapsed seconds since timer start or -1.0f if missing.



namespace
{
    const int kMaxTimers = 8;

    struct NamedTimer
    {
        bool inUse = false;
        char name[32] = {0};
        float intervalSeconds = 0.0f;
        uint32_t startMs = 0;
    };

    NamedTimer g_timers[kMaxTimers];

    int FindTimerIndex(const char* name)
    {
        if (!name || !name[0]) return -1;
        for (int i = 0; i < kMaxTimers; ++i) {
            if (g_timers[i].inUse && strcmp(g_timers[i].name, name) == 0) {
                return i;
            }
        }
        return -1;
    }

    int FindFreeIndex()
    {
        for (int i = 0; i < kMaxTimers; ++i) {
            if (!g_timers[i].inUse) return i;
        }
        return -1;
    }
}

bool setupClock(const char* ssid, const char* user, const char* pass)
{
    if (!wifi_connect(ssid, user, pass)) {
        log_print("Clock sync failed: WiFi connect");
        return false;
    }

    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

    const unsigned long start = millis();
    while ((millis() - start) < 15000) {
        time_t now = time(nullptr);
        struct tm tmNow;
        localtime_r(&now, &tmNow);
        if (tmNow.tm_year >= (2020 - 1900)) {
            log_print("Clock sync OK");
            return true;
        }
        delay(250);
    }

    log_print("Clock sync failed: NTP timeout");
    return false;
}

TimeExact WhatTimeIsItExactly()
{
    TimeExact out = {};

    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0) {
        time_t now = tv.tv_sec;
        struct tm tmNow;
        localtime_r(&now, &tmNow);

        out.year = tmNow.tm_year + 1900;
        out.month = tmNow.tm_mon + 1;
        out.day = tmNow.tm_mday;
        out.weekday = tmNow.tm_wday;
        out.hour = tmNow.tm_hour;
        out.minute = tmNow.tm_min;
        out.second = tmNow.tm_sec;
        out.millisecond = (int)(tv.tv_usec / 1000);
        out.valid = (tmNow.tm_year >= (2020 - 1900));
        return out;
    }

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    out.year = tmNow.tm_year + 1900;
    out.month = tmNow.tm_mon + 1;
    out.day = tmNow.tm_mday;
    out.weekday = tmNow.tm_wday;
    out.hour = tmNow.tm_hour;
    out.minute = tmNow.tm_min;
    out.second = tmNow.tm_sec;
    out.millisecond = (int)(millis() % 1000);
    out.valid = (tmNow.tm_year >= (2020 - 1900));
    return out;
}

uint32_t SecondsToMs(float seconds)
{
    if (seconds <= 0.0f) return 0;
    return static_cast<uint32_t>(seconds * 1000.0f);
}

bool CreateTimer(const char* name, float intervalSeconds)
{
    int idx = FindTimerIndex(name);
    if (idx < 0) {
        idx = FindFreeIndex();
        if (idx < 0) return false;
        memset(&g_timers[idx], 0, sizeof(NamedTimer));
        strncpy(g_timers[idx].name, name, sizeof(g_timers[idx].name) - 1);
        g_timers[idx].inUse = true;
    }

    g_timers[idx].intervalSeconds = intervalSeconds;
    g_timers[idx].startMs = millis();
    return true;
}

bool RestartTimer(const char* name)
{
    int idx = FindTimerIndex(name);
    if (idx < 0) return false;
    g_timers[idx].startMs = millis();
    return true;
}

float GetTimerLimitSeconds(const char* name)
{
    int idx = FindTimerIndex(name);
    if (idx < 0) return -1.0f;
    return g_timers[idx].intervalSeconds;
}

float GetTimerCurrent(const char* name)
{
    int idx = FindTimerIndex(name);
    if (idx < 0) return -1.0f;

    uint32_t elapsedMs = millis() - g_timers[idx].startMs;
    return static_cast<float>(elapsedMs) / 1000.0f;
}

