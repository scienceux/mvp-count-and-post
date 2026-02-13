#pragma once

#include <Arduino.h>

// Remote serial (HTTP) monitoring
void turn_on_remote_serial_monitoring();
void remote_serial_poll();
void remote_serial_write(const char* text);
void remote_serial_println(const char* text);
void enable_remote_serial(bool enabled);

// Logging helpers (serial + optional remote) — always prints a newline
void log_print(const char* text);
void log_print(const String& text);

// Log a JPEG file as Base64 payload lines.
bool log_print_jpeg_file(const char* label, const char* path);

// Overloads for numeric types
void log_print(int value);
void log_print(unsigned int value);
void log_print(long value);
void log_print(unsigned long value);
void log_print(float value);
void log_print(double value);
