#ifndef COUNT_OCCUPANCY_IN_FRAME_H
#define COUNT_OCCUPANCY_IN_FRAME_H

#include "utilities_camera.h"

// CSV path defined in main.cpp
extern char global_csv_path[64];

// ================================================================
// GRID DIFF SETTINGS + DATA
// Keep all grid-diff-related pieces together here so the detector
// logic and debug visualization can share the exact same setup.
// ================================================================
const uint8_t GRID_DIFF_NUM_COLS       = 6;
const uint8_t GRID_DIFF_NUM_ROWS       = 5;
const uint8_t GRID_DIFF_NUM_QUADRANTS  = GRID_DIFF_NUM_COLS * GRID_DIFF_NUM_ROWS;

struct gridDiff {
    uint32_t quadrantDiff[GRID_DIFF_NUM_QUADRANTS] = { 0 };
    uint32_t averageQuadrantDiff = 0;
    bool valid = false;
};

// Analyze a frame against the average frame and return estimated occupancy
int CountOccupancyInFrame(const Frame& frame);

// Returns true/false if diff between the two frames is bigger than threshold
bool FrameHasMotion(uint8_t* prev_frame, uint8_t* current_frame, size_t len);

// Total pixel brightness for left and right halves of a frame
struct SplitFrame {
    int64_t leftBrightness;
    int64_t rightBrightness;
};


// Functions
//===================

// Captures a frame and returns total pixel brightness for left and right halves
SplitFrame CameraGetSplitFrame(Frame frameToSplit);

// Builds the current grid diff against the average frame using the shared detector logic.
gridDiff DivideFrameIntoGridAndDiff();

// DEBUG ONLY:
// Builds a JPEG image from the processed grid diff so the debug page background
// can use the same diff output without affecting normal counting performance.
bool BuildGridDiffDebugImageJpg(uint8_t** jpgBufOut, size_t* jpgLenOut);

// Detects enter/exit events based on changes in brightness between two frames
bool EnterExitDetector(SplitFrame prev_frame, SplitFrame current_frame);
bool EnterExitDetector_v2_wAvg();

#endif // COUNT_OCCUPANCY_IN_FRAME_H
