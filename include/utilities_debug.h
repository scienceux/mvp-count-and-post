#pragma once

#include <Arduino.h>

// Photo capture (triggered from remote web UI)
// Returns true once if user pressed 'Take Photo'. outPath is filled with
// the suggested SD path (e.g. "/snap_003.jpg").
bool remote_take_photo_pending(char* outPath, size_t pathSize);

// Call after saving a photo to register it in the web gallery.
void remote_register_photo(const char* path);

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
