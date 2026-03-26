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
#include <WiFi.h>
#include "data_save.h"


// For Seeed XIAO ESP32-S3, the built-in LED is typically on pin 21
// But LED_BUILTIN should work if defined correctly in the board files
// #ifndef LED_BUILTIN
// #define LED_BUILTIN 21
// #endif


// CONFIG
//==================================================
// ---- adjust these as needed ----
// ENTEREXIT, OCCUPANCY, VIDEO_FOR_TRAINING (for training)
const char* DEVICE_MODE = "ENTEREXIT"; 
const int CAMERA_FPS = 4;

// Occupancy counting mode
const float OCCUPANCY_EVERY_SECS = 30.0f;
char global_csv_path[64] = "/occupancy.csv";


// WiFi config (STA)
// const char* WIFI_SSID = "posterbuddyTR";
// const char* WIFI_USER = ""; // unused for WPA2-PSK
// const char* WIFI_PASS = "money4connie";

const char* WIFI_SSID = "SolisWiFi_usf";
const char* WIFI_USER = ""; // unused for WPA2-PSK
const char* WIFI_PASS = "47319451";
//==================================================



// Track most recent occupancy result so we can skip baseline updates when occupied.
static int g_lastOccupancyCount = 0;

int g_EntersCount = 0;
int g_ExitsCount = 0;


void setup() {
  // Initialize serial communication for debugging (optional)
  Serial.begin(9600);
  delay(5000);

  log_print(psramFound() ? "PSRAM: OK" : "PSRAM: NOT FOUND - camera may crash");

  log_print(DEVICE_MODE);
  
  // What mode?
  //================================
  if (strcmp(DEVICE_MODE, "OCCUPANCY") == 0) {
    CreateTimer("OccupancyEverySecs", OCCUPANCY_EVERY_SECS);

  } else if (strcmp(DEVICE_MODE, "ENTEREXIT") == 0) {
    log_print("ENTEREXIT mode ");
  } else {
    log_print(String("Unknown DEVICE_MODE: ") + DEVICE_MODE);
  }

  bool ledOk = setupLED();
  if (ledOk) {
    log_print("LED setup successful.");
    blinkLED(1, "fast");
  } else {
    log_print("LED setup failed.");
  }

  bool sdOk = setupSDCard();
  if (sdOk) {
    log_print("SD Card setup successful.");
    blinkLED(2, "fast");
  } else {
    log_print("SD Card setup failed.");
    // Fail and blink SOS pattern if SD card is not working, since it's critical for operation
    while (true) {
      blinkLED(0, "SOS");
      delay(1000);
    }
  }

  // WiFi and clock are set up BEFORE the camera so that WiFi channel scanning
  // does not cause VSYNC overflow in the camera DMA pipeline (cam_task stack overflow).
  bool wifiOk = wifi_connect(WIFI_SSID, WIFI_USER, WIFI_PASS, DEVICE_ID);
  if (wifiOk) {
    log_print("WiFi connected.");
    turn_on_remote_serial_monitoring();
    enable_remote_serial(true);
  } else {
    log_print("WiFi connection failed.");
  }

  bool clockOk = setupClock(WIFI_SSID, WIFI_USER, WIFI_PASS);
  if (clockOk) {
    g_wifiSetTime = true;   // ← add this
    log_print("Clock synced.");
    TimeExact theTime = WhatTimeIsItExactly();
    log_print(String("Current time: ") + theTime.hour + ":" + theTime.minute + ":" + theTime.second);

  } else {
    log_print("Clock sync failed.");
  }

  bool cameraOk = CameraSetup(CAMERA_FPS, DEVICE_MODE);
  if (cameraOk) {
    log_print("Camera setup successful.");
    blinkLED(3, "fast");
  } else {
    log_print("Camera setup failed.");
  }

  // Test LED
  turnOnLED();
  delay(2000);
  turnOffLED();


  CreateTimer("UpdateAverageFrameSecs", 300.0f); // Update average frame every 60 seconds
  turnOnLED();
  AverageFrameCreate(15); // Average frames for first 15 seconds to create initial average frame
  turnOffLED();

  CreateTimer("CheckWifi", 300.0f); // Check WiFi every 3000 seconds
  CreateTimer("PrintStats", 60.0f); // Print stats every 60 seconds
  CreateTimer("UploadData", 60.0f); // Upload data every 60 seconds

  String CurrentTime = String(WhatTimeIsItExactly().hour) + ":" + String(WhatTimeIsItExactly().minute) + ":" + String(WhatTimeIsItExactly().second);
  log_print(String("Setup complete at ") + CurrentTime);

  NameTheCSVFile();
  CreateCSVFile();

  addEventToQue("POWERED_ON");

  
}

void loop() {
 
    // Poll the remote serial interface for incoming data
    remote_serial_poll();

    // Handle photo capture requested from the web UI
    char snapPath[32];
    if (remote_take_photo_pending(snapPath, sizeof(snapPath))) {
      if (CameraSaveSnapToSD(snapPath)) {
        remote_register_photo(snapPath);
        log_print((String("Photo saved: ") + snapPath).c_str());
      } else {
        log_print("Photo save failed");
      }
    }

    if ( IsTimerElapsed("UpdateAverageFrameSecs") ) {
      log_print(String("UpdateAverageFrameSecs timer elapsed: ") + GetTimerCurrent("UpdateAverageFrameSecs"));
      AverageFrameCreate(10); // Average frames for 10 seconds to update average frame
      RestartTimer("UpdateAverageFrameSecs");
    }

    EnterExitDetector_v2_wAvg();

    if ( IsTimerElapsed("CheckWifi") ) {
      if (WiFi.status() != WL_CONNECTED) {
        log_print("WiFi disconnected, attempting reconnect...");
        wifi_connect(WIFI_SSID, WIFI_USER, WIFI_PASS);
      }
      RestartTimer("CheckWifi");
    }

    if ( IsTimerElapsed("UploadData") ) {
      log_print("UploadData timer elapsed: " + String(GetTimerCurrent("UploadData")));
      if (!LogQuedEvents()) {
        log_print("Failed to log qued events");
      } else {
        log_print("Qued events logged successfully");
      }
      RestartTimer("UploadData");
    }
}
