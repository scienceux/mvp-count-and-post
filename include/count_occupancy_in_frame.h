#ifndef COUNT_OCCUPANCY_IN_FRAME_H
#define COUNT_OCCUPANCY_IN_FRAME_H

#include "utilities_camera.h"

// CSV path defined in main.cpp
extern char global_csv_path[64];

// Analyze a frame against the average frame and return estimated occupancy
int CountOccupancyInFrame(const Frame& frame, const char* averageFramePath);

#endif // COUNT_OCCUPANCY_IN_FRAME_H
