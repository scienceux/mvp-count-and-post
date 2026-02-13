#ifndef UTILITIES_TIME_H
#define UTILITIES_TIME_H

#include <Arduino.h>
#include <stdint.h>

// Start the occupancy timer (sets the reference time)
void restartOccupancyTimer();

// Get elapsed time since start/reset, in seconds
float getOccupancyTimer();

// Convert seconds to milliseconds
uint32_t SecondsToMs(float seconds);

// Named timer API (string names for clarity)
bool CreateTimer(const char* name, float intervalSeconds);
bool RestartTimer(const char* name);
float GetTimerCurrent(const char* name);
float GetTimerLimitSeconds(const char* name);

// Connect to Wi-Fi and sync the RTC using NTP.
bool setupClock(const char* ssid, const char* user, const char* pass);

// Detailed local time snapshot (with milliseconds when available).
struct TimeExact {
	int year;        // 4-digit year
	int month;       // 1-12
	int day;         // 1-31
	int weekday;     // 0-6 (Sunday=0)
	int hour;        // 0-23
	int minute;      // 0-59
	int second;      // 0-59
	int millisecond; // 0-999
	bool valid;      // true if RTC looks valid
};

// Return current local time with millisecond precision when available.
TimeExact WhatTimeIsItExactly();

// Returns the interval (seconds) set for this timer, or -1.0f if not found.
float GetTimerLimitSeconds(const char* name);

#endif // UTILITIES_TIME_H
