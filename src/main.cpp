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

  
  turnOffLED();

}

void loop() {
 
      //================================
      // OCCUPANCY or ENTEREXIT mode
      //================================     
    // Poll the remote serial interface for incoming data
    remote_serial_poll();

    // Save copy of one frame to memory, then release it so camera can reuse the buffer (and we don't run out of memory)
    Frame prev_frame = CameraGetCopyOfLatestFrame();
    delay(100);
    Frame current_frame = CameraGetCopyOfLatestFrame();

    SplitFrame prev_frame_split = CameraGetSplitFrame(prev_frame);
    SplitFrame current_frame_split = CameraGetSplitFrame(current_frame);

    if ( EnterExitDetector(prev_frame_split, current_frame_split) ) {
      // SaveLastFrameJpeg(current_frame);  
      // log_print("Prev:    " + String(prev_frame_split.leftBrightness)    + " | " + String(prev_frame_split.rightBrightness));
      // log_print("Current: " + String(current_frame_split.leftBrightness) + " | " + String(current_frame_split.rightBrightness));
    }

    free(prev_frame.copyOfbufferInMemory);
    free(current_frame.copyOfbufferInMemory);

}
