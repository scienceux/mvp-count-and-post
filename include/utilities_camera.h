#ifndef UTILITIES_CAMERA_H
#define UTILITIES_CAMERA_H

#include <Arduino.h>
#include "esp_camera.h"

// utilities_camera.h
bool CameraSetup(int targetFps);
int CameraGetTargetFps();
bool CameraSetFrameRotation(int degrees);

// Path to the most recently saved average frame file
extern char global_averageFrame_path[64];

// Simple container for a single captured frame
struct Frame {
	camera_fb_t* fb;      // Raw camera buffer from the driver
	camera_fb_t* raw_fb;  // Original buffer (unrotated)
	camera_fb_t* rotated_fb; // Rotated buffer (if any)
	uint8_t* rotated_buf; // Rotated pixel storage
	uint32_t capturedMs;  // Time (ms) when we grabbed it
	bool valid;           // True if capture succeeded
	bool rotated;         // True if fb points to rotated data
};

// Get the newest frame from the camera
Frame CameraGetLatestFrame();

// Give the frame buffer back to the camera driver
void CameraRelease(const Frame& frame);

// Save the most recent frame as a JPEG file to the SD card
bool SaveLastFrameJpeg(const Frame& frame);

// Video recording - saves frames to SD in folders every intervalMinutes
bool StartVideoRecording(int intervalMinutes);
void StopVideoRecording();
bool IsVideoRecording();
void VideoRecordingLoop();

#endif // UTILITIES_CAMERA_H
