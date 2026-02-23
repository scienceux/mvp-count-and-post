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
// #ifndef LED_BUILTIN
// #define LED_BUILTIN 21
// #endif


// CONFIG
//==================================================
// ---- adjust these as needed ----
// ENTEREXIT, OCCUPANCY, VIDEO_FOR_TRAINING (for training)
const char* DEVICE_MODE = "ENTEREXIT"; 
const int CAMERA_FPS = 4;

// Video mode
const char* VIDEO_SAVE_FOLDER = "/videos"; // SD card folder to save videos (for VIDEO_FOR_TRAINING mode)
const float HOW_OFTEN_SAVE_VIDEO_FROM_JPG = 60.0f; // seconds - how often to save a new video file from JPG frames in VIDEO_FOR_TRAINING mode


// Occupancy counting mode
const float OCCUPANCY_EVERY_SECS = 30.0f;
char global_csv_path[64] = "/occupancy.csv";


// WiFi config (STA)
const char* WIFI_SSID = "posterbuddy";
const char* WIFI_USER = ""; // unused for WPA2-PSK
const char* WIFI_PASS = "money4connie";
//==================================================




// Track most recent occupancy result so we can skip baseline updates when occupied.
static int g_lastOccupancyCount = 0;


void setup() {
  // Initialize serial communication for debugging (optional)
  Serial.begin(9600);
  delay(5000);

  log_print(DEVICE_MODE);
  
  // What mode?
  //================================
  if (strcmp(DEVICE_MODE, "VIDEO_FOR_TRAINING") == 0) {
    CreateTimer("SaveMJpeg", HOW_OFTEN_SAVE_VIDEO_FROM_JPG);
    StartNewVideo(VIDEO_SAVE_FOLDER);

  } else if (strcmp(DEVICE_MODE, "OCCUPANCY") == 0) {
    CreateTimer("OccupancyEverySecs", OCCUPANCY_EVERY_SECS);
    CreateTimer("UpdateAverageFrameSecs", 300.0f); // Update average frame every 5 minutes
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

  bool cameraOk = CameraSetup(CAMERA_FPS, DEVICE_MODE);
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

}

void loop() {

 

    // Poll the remote serial interface for incoming data
    remote_serial_poll();    

    if ( strcmp(DEVICE_MODE, "VIDEO_FOR_TRAINING") == 0 ) {
      // Whatever the mode, you're gonna want the frame and the time
      TimeExact exactTime = WhatTimeIsItExactly();
      Frame frame = CameraGetLatestFrame();         

      //================================================================
      // VIDEO mode
      // In this mode, we save a new video file every HOW_OFTEN_SAVE_VIDEO_FROM_JPG seconds by capturing a JPG frame and appending it to the MJPEG stream file. 
      // StartVideoRecording(1);  // new video file every 1 minute      
      //================================================================
      if (GetTimerCurrent("SaveMJpeg") >= GetTimerLimitSeconds("SaveMJpeg")) {
        CloseOffVideo();
        StartNewVideo(VIDEO_SAVE_FOLDER);
        RestartTimer("SaveMJpeg");
      } else {
        AddToVideo();
      }

      return;

    } else {
      //================================
      // OCCUPANCY or ENTEREXIT mode
      //================================     

      if ( strcmp(DEVICE_MODE, "ENTEREXIT") == 0 ) {
        
        // Capture frame 1 and save to memory
        camera_fb_t* frame1stFB = esp_camera_fb_get();     
        uint8_t* frame1stData = (uint8_t*)malloc(frame1stFB->len);
        memcpy(frame1stData, frame1stFB->buf, frame1stFB->len);
        size_t frame1stLen = frame1stFB->len;
        esp_camera_fb_return(frame1stFB);    

        // Capture frame 2 and save to memory
        camera_fb_t* frame2ndFB = esp_camera_fb_get();     
        uint8_t* frame2ndData = (uint8_t*)malloc(frame2ndFB->len);
        memcpy(frame2ndData, frame2ndFB->buf, frame2ndFB->len);
        size_t frame2ndLen = frame2ndFB->len;
        esp_camera_fb_return(frame2ndFB);          
        
          if (FrameHasMotion(frame1stData, frame2ndData, frame1stLen)) {
            log_print("Motion detected!");
          }            
        
        free(frame1stData);
        frame1stData = nullptr;
        free(frame2ndData);
        frame2ndData = nullptr;

      }

    }

}