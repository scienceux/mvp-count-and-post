# Project Function Outline

## src/main.cpp

**Global Config Variables:**
| Variable | Default | Description |
|----------|---------|-------------|
| `DEVICE_MODE` | `"VIDEO_FOR_TRAINING"` | Operating mode: `ENTEREXIT`, `OCCUPANCY`, or `VIDEO_FOR_TRAINING` |
| `CAMERA_FPS` | `4` | Target frames per second for camera capture |
| `VIDEO_SAVE_FOLDER` | `"/videos"` | SD card folder for video files (VIDEO_FOR_TRAINING mode) |
| `HOW_OFTEN_SAVE_VIDEO_FROM_JPG` | `60.0f` | Seconds between new video file saves |
| `OCCUPANCY_EVERY_SECS` | `30.0f` | How often to check occupancy (seconds) |
| `global_csv_path` | `"/occupancy.csv"` | Path to occupancy log file on SD card |
| `WIFI_SSID` | `"posterbuddy"` | WiFi network name |
| `WIFI_PASS` | `"money4connie"` | WiFi password |

- `setup()` - Initializes LED, camera, SD, WiFi, and average frame
  1. Start serial at 9600 baud, wait 5 seconds
  2. Load config from NVS (device name, etc.)
  3. Create mode-specific timers:
     - VIDEO_FOR_TRAINING: `SaveMJpeg` timer (60s)
     - OCCUPANCY: `OccupancyEverySecs` (30s) + `UpdateAverageFrameSecs` (5min)
     - ENTEREXIT: not implemented yet
  4. Initialize LED → blink 1x on success
  5. Initialize camera at 4 FPS, rotate 90° → blink 2x on success
  6. Initialize SD card → blink 3x on success, or SOS loop forever if failed
  7. Connect WiFi and sync clock via NTP → start remote logging server if connected
  8. If not VIDEO_FOR_TRAINING: capture 10s of frames to create initial average

- `loop()` - Runs video recording or occupancy counting loop
  - **VIDEO_FOR_TRAINING mode:** Restart SaveMJpeg timer and return (video capture not yet implemented)
  - **OCCUPANCY mode:**
    1. Poll remote serial for HTTP requests
    2. Every 5 minutes: update average frame by appending 10s of new frames
    3. Every 30 seconds: capture frame, compare to average, count occupancy, log result
  - **ENTEREXIT mode:** Not implemented yet

## src/utilities_camera.cpp
- `CameraSetup(targetFps)` - Initializes ESP32 camera with given FPS
- `CameraGetTargetFps()` - Returns configured FPS value
- `CameraSetFrameRotation(degrees)` - Sets frame rotation (0/90/180/270)
- `CameraGetLatestFrame()` - Captures and returns current grayscale frame
- `CameraRelease(frame)` - Frees frame memory buffers. Called after frame processing completes
- `SaveLastFrameJpeg(frame)` - Saves frame as JPEG to SD card
- `StartVideoRecording(intervalMinutes)` - Starts MJPEG recording to SD card
- `StopVideoRecording()` - Stops recording and closes video file
- `VideoRecordingLoop()` - Captures frames at target FPS for recording
- `RotateGrayFrame()` *(static)* - Rotates grayscale buffer by specified degrees

## src/utilities_led.cpp
- `setupLED()` - Configures LED pin as output
- `turnOnLED()` - Turns LED on (active low)
- `turnOffLED()` - Turns LED off
- `blinkLED(count, mode)` - Non-blocking blink with fast/slow/SOS patterns
- `blinkLEDReset()` - Resets blink state machine
- `ConvertFastSlowToMilliseconds(mode)` - Returns delay for blink mode

## src/utilities_sd_card.cpp
- `setupSDCard()` - Initializes SD card with SPI pins

## src/utilities_time.cpp
- `setupClock(ssid, user, pass)` - Syncs RTC via NTP over WiFi
- `WhatTimeIsItExactly()` - Returns current time with milliseconds
- `SecondsToMs(seconds)` - Converts float seconds to milliseconds
- `CreateTimer(name, intervalSeconds)` - Creates or updates named timer
- `RestartTimer(name)` - Resets named timer start time
- `GetTimerLimitSeconds(name)` - Returns timer interval in seconds
- `GetTimerCurrent(name)` - Returns elapsed seconds since timer start

## src/utilities_wifi.cpp
- `wifi_connect(ssid, user, pass)` - Connects to WiFi network (WPA2-PSK)

## src/utilities_debug.cpp
- `turn_on_remote_serial_monitoring()` - Starts HTTP server for remote logging
- `remote_serial_poll()` - Handles HTTP client requests
- `remote_serial_write(text)` - Appends text to current log line
- `remote_serial_println(text)` - Writes line to log buffer
- `enable_remote_serial(enabled)` - Enables/disables remote logging
- `log_print(text)` - Prints to Serial and remote log
- `log_print_jpeg_file(label, path)` - Sends base64 JPEG over serial
- `log_println(value)` - Prints numeric values with newline

## src/average_frame.cpp
- `CameraCreateAvgFrame(duration, newOrAppend)` - Creates/updates baseline average frame

## src/config.cpp
- `ConfigInit()` - Loads config from NVS into memory
- `GetDeviceName()` - Returns stored device name
- `GetMode()` - Returns device operating mode
- `GetEventName()` - Returns event name string
- `GetWiFiSSID()` - Returns stored WiFi SSID
- `GetWiFiPassword()` - Returns stored WiFi password
- `SetDeviceName(value)` - Saves device name to NVS
- `SetMode(value)` - Saves mode to NVS
- `SetEventName(value)` - Saves event name to NVS
- `SetWiFiSSID(value)` - Saves WiFi SSID to NVS
- `SetWiFiPassword(value)` - Saves WiFi password to NVS
- `ProcessConfigCommand()` - Handles serial config commands (IDENTIFY, SET_*, etc.)

## src/count_occupancy_in_frame.cpp
- `CountOccupancyInFrame(frame, avgPath)` - Estimates people count from frame difference

## src/count_enter_exit.cpp
*(empty)*

## src/data_save.cpp
*(empty)*
