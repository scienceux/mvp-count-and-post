#include <Arduino.h>
#include "utilities_led.h"
#include "utilities_camera.h"
#include "average_frame.h"
#include "count_enter_exit.h"
#include "count_occupancy_in_frame.h"
#include "utilities_sd_card.h"
#include "utilities_time.h"
#include "utilities_wifi.h"
#include "utilities_debug.h"
#include "config.h"
#include "img_converters.h"
#include <SD.h>
#include <Preferences.h>

// For Seeed XIAO ESP32-S3, the built-in LED is typically on pin 21
// But LED_BUILTIN should work if defined correctly in the board files
#ifndef LED_BUILTIN
#define LED_BUILTIN 21
#endif


// CONFIG
// ---- adjust these as needed ----
const char* DEVICE_MODE = "VIDEO_FOR_TRAINING"; // ENTEREXIT, OCCUPANCY, VIDEO_FOR_TRAINING (for training)
const int CAMERA_FPS = 4;
const float OCCUPANCY_EVERY_SECS = 30.0f;
char global_csv_path[64] = "/occupancy.csv";


// WiFi config (STA)
const char* WIFI_SSID = "posterbuddy";
const char* WIFI_USER = ""; // unused for WPA2-PSK
const char* WIFI_PASS = "money4connie";
// -------------------------------




// Track most recent occupancy result so we can skip baseline updates when occupied.
static int g_lastOccupancyCount = 0;



void setup() {
  // Initialize serial communication for debugging (optional)
  Serial.begin(9600);
  delay(5000);

  // Initialize config from NVS (must be called before using config getters)
  ConfigInit();
  
  if (GetDeviceName().length() > 0) {
    log_print(String("Device Name: ") + GetDeviceName());
  } else {
    log_print("No device name set in preferences.");
  }

  
  CreateTimer("OccupancyEverySecs", OCCUPANCY_EVERY_SECS);
  CreateTimer("UpdateAverageFrameSecs", 300.0f);  

  bool ledOk = setupLED();
  if (ledOk) {
    log_print("LED setup successful.");
    blinkLED(1, "fast");
  } else {
    log_print("LED setup failed.");
  }

  bool cameraOk = CameraSetup(CAMERA_FPS);
  if (cameraOk) {
    CameraSetFrameRotation(90);
    log_print("Camera setup successful.");
    blinkLED(2, "fast");
  } else {
    log_print("Camera setup failed.");
  }

  bool sdOk = setupSDCard();
  if (sdOk) {
    log_print("SD Card setup successful.");
    blinkLED(3, "fast");
  } else {
    log_print("SD Card setup failed.");
    // Fail and blink SOS pattern if SD card is not working, since it's critical for operation
    while (true) {
      blinkLED(0, "SOS");
      delay(1000);
    }
  }

  bool wifiOk = setupClock(WIFI_SSID, WIFI_USER, WIFI_PASS);
  if (wifiOk) {
    log_print("WiFi connected.");
    turn_on_remote_serial_monitoring();
    enable_remote_serial(true);
  } else {
    log_print("WiFi connection failed.");
  }

  if (cameraOk && sdOk) {
    log_print("Creating initial average frame...");
    CameraCreateAvgFrame(10, "new"); // Create average frame over 10 seconds

    if (String(DEVICE_MODE) == "VIDEO_FOR_TRAINING") {
      StartVideoRecording(5); // Start video recording with 5 minute intervals
      log_print("Video recording started (5-min files).");
    }
  }


}

void loop() {
    if (strcmp(DEVICE_MODE, "VIDEO_FOR_TRAINING") == 0) {
        StartVideoRecording(5);  // new AVI file every 5 minutes
        log_print("Video recording started (5-min files).");
        return;
    }

    remote_serial_poll();

    // Check for config commands over serial (IDENTIFY, SET_NAME, GET_CONFIG, etc.)
    ProcessConfigCommand();

    // Periodically update the average frame every 5 minutes
    if (GetTimerCurrent("UpdateAverageFrameSecs") >= GetTimerLimitSeconds("UpdateAverageFrameSecs")) {
        CameraCreateAvgFrame(10, "append"); // Append 10 seconds of frames to the average
        RestartTimer("UpdateAverageFrameSecs");
    }

    if (GetTimerCurrent("OccupancyEverySecs") >= GetTimerLimitSeconds("OccupancyEverySecs")) {
        TimeExact exactTime = WhatTimeIsItExactly();
        Frame frame = CameraGetLatestFrame();
        SaveLastFrameJpeg(frame);
        g_lastOccupancyCount = CountOccupancyInFrame(frame, global_averageFrame_path);
        CameraRelease(frame);
        log_print(g_lastOccupancyCount);
        RestartTimer("OccupancyEverySecs");

        char timeBuf[48];
        snprintf(timeBuf, sizeof(timeBuf),
                 "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 exactTime.year, exactTime.month, exactTime.day,
                 exactTime.hour, exactTime.minute, exactTime.second,
                 exactTime.millisecond);
        log_print(timeBuf);

        // Log the data
    }

    // Diagnostics (uncomment to log base64 payloads)
    /*
    log_print_jpeg_file("average_frame", "/avg_frame_current.jpg");
    log_print_jpeg_file("last_frame", "/last_frame.jpg");
    log_print_jpeg_file("last_diff", "/last_diff.jpg");
    log_print(g_lastOccupancyCount);
    */
}