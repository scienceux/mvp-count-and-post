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


// Track most recent occupancy result so we can skip baseline updates when occupied.
static int g_lastOccupancyCount = 0;

int g_EntersCount = 0;
int g_ExitsCount = 0;


void setup() {
  Serial.begin(9600);
  delay(5000);

  setCpuFrequencyMhz(160);

  log_print(psramFound() ? "PSRAM: OK" : "PSRAM: NOT FOUND - camera may crash");

  bool ledOk = setupLED();
  if (ledOk) {
    log_print("LED setup successful.");
  } else {
    log_print("LED setup failed.");
  }
  turnOnLED(); // LED stays on during entire setup — turns off only on success; stays on if panic

  bool sdOk = setupSDCard();
  if (sdOk) {
    log_print("SD Card setup successful.");
  } else {
    log_print("SD Card setup failed.");
    // Fail and blink SOS pattern if SD card is not working, since it's critical for operation
    while (true) {
      blinkLED(0, "SOS");
      delay(1000);
    }
  }

  // Set global variables from SD's config.txt
  setConfigFromSD();




  log_print("Delay done. About to setup camera...");

  bool cameraOk = CameraSetup(CAMERA_FPS, g_deviceMode.c_str());
  if (cameraOk) {
    log_print("Camera setup successful.");
  } else {
    log_print("Camera setup failed -- halting.");
    while (true) {
      blinkLED(0, "SOS");
      delay(1000);
    }
  }

  log_print("All camera setup complete, about to create initial average frame...");

  CreateTimer("UpdateAverageFrameSecs", 300.0f); // Update average frame every 60 seconds
  AverageFrameCreate(15); // Average frames for first 15 seconds to create initial average frame

  CreateTimer("CheckWifi", 300.0f);
  CreateTimer("PrintStats", 60.0f);
  CreateTimer("UploadData", 40.0f);
  CreateTimer("IdleHeartbeat", 300.0f); // Log IDLE event every 5 minutes so we can confirm device is alive

    // WiFi after camera and average frame to avoid VSYNC overflow during camera init
  bool wifiOk = wifi_connect(g_wifiSsid, g_wifiUser, g_wifiPass, g_deviceName.c_str());
  if (wifiOk) {
    log_print("WiFi connected.");
    turn_on_remote_serial_monitoring();
    enable_remote_serial(true);
  } else {
    log_print("WiFi connection failed.");
  }

  bool clockOk = setupClock(g_wifiSsid.c_str(), g_wifiUser.c_str(), g_wifiPass.c_str());
  if (clockOk) {
    g_wifiSetTime = true;
    log_print("Clock synced.");
    TimeExact theTime = WhatTimeIsItExactly();
    log_print(String("Current time: ") + theTime.hour + ":" + theTime.minute + ":" + theTime.second);
  } else {
    log_print("Clock sync failed.");
  }

  // Name and create CSV after clock sync so the filename uses the correct time
  NameTheCSVFile();
  CreateCSVFile();

  String CurrentTime = String(WhatTimeIsItExactly().hour) + ":" + String(WhatTimeIsItExactly().minute) + ":" + String(WhatTimeIsItExactly().second);
  log_print(String("Setup complete at ") + CurrentTime);

  addEventToQue("POWERED_ON");

  turnOffLED(); // Setup completed successfully
}

void loop() {
 
    // Poll the remote serial interface for incoming data
    // remote_serial_poll();

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
        wifi_connect(g_wifiSsid, g_wifiUser, g_wifiPass);
      }
      // If clock was never synced (e.g. WiFi wasn't up during setup), retry now
      if (WiFi.status() == WL_CONNECTED && !g_wifiSetTime) {
        log_print("Attempting clock sync...");
        if (setupClock(g_wifiSsid.c_str(), g_wifiUser.c_str(), g_wifiPass.c_str())) {
          g_wifiSetTime = true;
          TimeExact t = WhatTimeIsItExactly();
          log_print(String("Clock synced after reconnect: ") + t.hour + ":" + t.minute + ":" + t.second);
          NameTheCSVFile();
          CreateCSVFile();
        }
      }
      RestartTimer("CheckWifi");
    }

    
    if ( IsTimerElapsed("IdleHeartbeat") ) {
      addEventToQue("IDLE");
      RestartTimer("IdleHeartbeat");
    }

    if (WiFi.status() == WL_CONNECTED) {    
      if ( IsTimerElapsed("UploadData") ) {
        log_print("UploadData timer elapsed: " + String(GetTimerCurrent("UploadData")));
        if (!LogQuedEvents()) {
          log_print("Failed to log qued events");
          if (WiFi.status() != WL_CONNECTED) {
            log_print("Maybe because no wifi");
          }

        } else {
          log_print("Qued events logged successfully");
        }
        RestartTimer("UploadData");
      }
    }
}
