#ifndef UTILITIES_CAMERA_H
#define UTILITIES_CAMERA_H

#include <Arduino.h>
#include "esp_camera.h"

// utilities_camera.h
bool CameraSetup(int targetFps, const char* deviceMode);
int CameraGetTargetFps();

// Path to the most recently saved average frame file
extern char global_averageFrame_path[64];

// Simple container for a single captured frame
struct Frame {
	uint32_t* copyOfbufferInMemory;      // Raw camera buffer from the driver
	uint32_t timecaptured;  // Time (ms) when we grabbed it
	bool valid;           // True if capture succeeded
};

// Get the newest frame from the camera
Frame CameraGetCopyOfLatestFrame();

// Give the frame buffer back to the camera driver
void CameraRelease(const Frame& frame);

// Grab a live frame, convert to JPEG, and write to SD card.
// path should be an SD-absolute path like "/snap_003.jpg".
bool CameraSaveSnapToSD(const char* path);

// Save the most recent frame as a JPEG file to the SD card
// bool SaveLastFrameJpeg(const Frame& frame);



// Capture and exponentially average frames for numSecondsToAverage seconds,
// then save the result as a JPEG to /average-frames/ on the SD card.
bool AverageFrameCreate(int numSecondsToAverage);

// Returns pointer to the global PSRAM average frame buffer (FW*FH bytes), or nullptr if not yet created.
const uint8_t* CameraGetAverageFrame();

extern const int FW;
extern const int FH;

#endif // UTILITIES_CAMERA_H
