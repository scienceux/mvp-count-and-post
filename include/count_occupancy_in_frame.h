#ifndef COUNT_OCCUPANCY_IN_FRAME_H
#define COUNT_OCCUPANCY_IN_FRAME_H

#include "utilities_camera.h"

// CSV path defined in main.cpp
extern char global_csv_path[64];

// Analyze a frame against the average frame and return estimated occupancy
int CountOccupancyInFrame(const Frame& frame);

// Returns true/false if diff between the two frames is bigger than threshold
bool FrameHasMotion(uint8_t* frame1st, uint8_t* frame2nd, size_t len);

#endif // COUNT_OCCUPANCY_IN_FRAME_H
