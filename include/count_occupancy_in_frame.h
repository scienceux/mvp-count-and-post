#ifndef COUNT_OCCUPANCY_IN_FRAME_H
#define COUNT_OCCUPANCY_IN_FRAME_H

#include "utilities_camera.h"

// CSV path defined in main.cpp
extern char global_csv_path[64];

// Analyze a frame against the average frame and return estimated occupancy
int CountOccupancyInFrame(const Frame& frame);

// Returns true/false if diff between the two frames is bigger than threshold
bool FrameHasMotion(uint8_t* prev_frame, uint8_t* current_frame, size_t len);

// Total pixel brightness for left and right halves of a frame
struct SplitFrame {
    int32_t leftBrightness;   // total brightness of left half
    int32_t rightBrightness;  // total brightness of right half
};

// Captures a frame and returns total pixel brightness for left and right halves
SplitFrame CameraGetSplitFrame();

// Detects enter/exit events based on changes in brightness between two frames
void EnterExitDetector(SplitFrame prev_frame, SplitFrame current_frame);

#endif // COUNT_OCCUPANCY_IN_FRAME_H
