#ifndef AVERAGE_FRAME_H
#define AVERAGE_FRAME_H

#include "utilities_camera.h"

// Build or append to the average frame.
bool CameraCreateAvgFrame(int durationSeconds, const char* newOrAppend);

#endif // AVERAGE_FRAME_H
