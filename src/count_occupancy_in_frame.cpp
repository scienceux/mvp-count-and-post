// Load tools: SD, time, math, camera.
#include "count_occupancy_in_frame.h"
#include <Arduino.h>
#include <SD.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include "img_converters.h"
#include "utilities_debug.h"
#include "data_save.h"

// Put frame height and width here for easy access, based on camera config (VGA grayscale)
const int FW = 640;
const int FH = 480;

// Width of left and right 'halves' of the frame
const int SplitWidth = FW / 2;


SplitFrame CameraGetSplitFrame(Frame frameToSplit) {

    static int64_t temp_frame[2] = { 0 };  // int32 can hold it but int64 is safer
    temp_frame[0] = 0;
    temp_frame[1] = 0;

    if (!frameToSplit.copyOfbufferInMemory ) {
        log_print("CameraGetSplitFrame: null frame");
        return SplitFrame{ 0, 0 };
    }

    // convert frame into 2D array x/y coord of pixel brightness values (0 to 255)
    for (uint32_t i = 0; i < (FW * FH); i++) {         // step through all pixels in image
      
      // calculate x and y location of this pixel in the image
      // The camera buffer is a flat 1D array of pixels stored row by row.
      // e.g. for FW=640: pixel 0 is (x=0,y=0), pixel 639 is (x=639,y=0), pixel 640 is the start of row 2 (x=0,y=1).
      // % (modulo) gives the remainder after dividing by FW — that remainder is how far
      // along the current row we are, which is the x (column) position.
      const uint16_t x = i % FW;

      // gives how many whole rows we've passed, which is the y (row) position.
      const uint16_t y = floor(i / FW);

      // Which horizontal half of the frame is this pixel in?
      // SplitWidth = FW/2 = 320, so pixels with x < 320 → block_x = 0 (left half),
      // pixels with x >= 320 → block_x = 1 (right half).
      const uint8_t whichside_x = floor(x / SplitWidth); 

      const uint32_t pixelBrightness = ((uint8_t*)frameToSplit.copyOfbufferInMemory)[i];             // get the pixels brightness (0 to 255)


      temp_frame[whichside_x] += pixelBrightness;                 
    }

    // Give total brightness sane, readable values
    // raw values like: -605470922 | -1421458258
    // int32_t divisor = 1000000; // divisor to get brightness into a more manageable range (millions) for easier thresholding
    // temp_frame[0] /= divisor; // divide by 1000 to get smaller numbers that are easier to reason about and set thresholds for
    // temp_frame[1] /= divisor; // divide by 1000 to get smaller numbers that are easier to reason about and set thresholds for

    // Returns as .leftBrightness and .rightBrightness the total brightness of all pixels in the left and right halves of the frame, respectively.
    // as defined in .h file, SplitFrame struct has .leftBrightness and .rightBrightness fields for this purpose.
    return SplitFrame{ temp_frame[0], temp_frame[1] };           // return total brightness for left and right halves of the frame
}


//================================
// Simple Overall Motion
//================================
// Threshold for considering a pixel "changed" between two frames
const int kDiffThreshold = 10;

bool FrameHasMotion(uint8_t* prev_frame, uint8_t* current_frame, size_t len)
{
    if (!prev_frame || !current_frame) {
        log_print("FrameHasMotion: null frame");
        return false;
    }

    uint32_t changedCount = 0;

    // Every pixel: how much did it change?
    for (size_t i = 0; i < len; i++) {
        int diff = abs((int)current_frame[i] - (int)prev_frame[i]);

        // Loud enough change? Tally it.
        if (diff >= kDiffThreshold) {
            changedCount++;
        }
    }

    // Enough pixels moved? Someone's there.
	log_print(changedCount);
    return changedCount > 0;
}


bool enterStarted = false;
bool exitStarted = false;


//================================
// v1 Enter/Exit (LEGACY — kept for reference, no longer called)
//================================
bool EnterExitDetector(SplitFrame prev_frame_split, SplitFrame current_frame_split) {
    uint32_t changeInBrightnessLeft  = abs(current_frame_split.leftBrightness  - prev_frame_split.leftBrightness);
    uint32_t changeInBrightnessRight = abs(current_frame_split.rightBrightness - prev_frame_split.rightBrightness);
    uint32_t threshold = 1000000;

    if ( !enterStarted && !exitStarted ) {
        if ( (changeInBrightnessLeft > changeInBrightnessRight) && (changeInBrightnessLeft > threshold) ) {
            enterStarted = true;
        } else if ( (changeInBrightnessRight > changeInBrightnessLeft) && (changeInBrightnessRight > threshold) ) {
            exitStarted = true;
        }
    }

    if ( enterStarted && (changeInBrightnessRight > threshold) ) {
        enterStarted = false;
        extern int g_EntersCount;
        g_EntersCount++;
        return true;
    } else if ( exitStarted && (changeInBrightnessLeft > threshold) ) {
        exitStarted = false;
        extern int g_ExitsCount;
        g_ExitsCount++;
        return true;
    }

    if ( (enterStarted || exitStarted) && (changeInBrightnessLeft < threshold) && (changeInBrightnessRight < threshold) ) {
        enterStarted = false;
        exitStarted = false;
        return true;
    }

    return false;
}


//================================
// DIVIDE & DIFF frame
// return gridDiff struct - total diff per quadrant, minus average frame, minus average diff
//================================
gridDiff DivideFrameIntoGridAndDiff() {
    gridDiff out;

    const uint16_t QUAD_W = FW / GRID_DIFF_NUM_COLS;
    const uint16_t QUAD_H = FH / GRID_DIFF_NUM_ROWS;

    // Use average frame as baseline
    const uint8_t* avgFrame = CameraGetAverageFrame();
    if (!avgFrame) {
        log_print("EnterExitDetector_v2: no average frame");
        return out;
    }

    // Get current frame
    Frame current_frame = CameraGetCopyOfLatestFrame();
    if (!current_frame.valid) {
        log_print("EnterExitDetector_v2: no current frame");
        return out;
    }

    const uint8_t* pixels = (uint8_t*)current_frame.copyOfbufferInMemory;

    // PIXEL WALK
    // Walk every pixel, accumulate abs diff into its quadrant bucket
    for (uint32_t i = 0; i < (uint32_t)(FW * FH); i++) {

        // Get X/Y pos
        const uint16_t x   = i % FW; // 0 to FW-1 (e.g., x=2 in second row would be i=642, 642 % 640 = 2)
        const uint16_t y   = i / FW;

        // QUADRANTS       
        // Which quadrant is this pixel in?
        // QUAD_W=160, so x=2 would be col=0 (first quadrant), x=162 would be col= (second quadrant)
        // min is to handle edge case of x=639 which would give col=3 (nonexistent) but should be col=2 (last quadrant)
        const uint8_t col = min((uint8_t)(x / QUAD_W), (uint8_t)(GRID_DIFF_NUM_COLS - 1)); 
        const uint8_t row = min((uint8_t)(y / QUAD_H), (uint8_t)(GRID_DIFF_NUM_ROWS - 1)); // QUAD_H=96, so y=50 would be row=0 (first row of quadrants), y=150 would be row=1 (second row of quadrants)
        
        // Which quadrant number (0 to 19 for 6x5 grid) is this pixel in?
        const uint8_t q   = row * GRID_DIFF_NUM_COLS + col; // e.g., row 1 * NUM_COLS = 6 + col 2 = quadrant 8 (third quadrant in second row)

        //*******
        // DIFF
        const uint32_t pixeldiff = (uint32_t)abs((int)pixels[i] - (int)avgFrame[i]); // e.g., if pixels[i]=140 and avgFrame[i]=100, abs(140 - 100) = 40, so pixeldiff = 40
        out.quadrantDiff[q] += pixeldiff;
    }

    //------------------------
    // BRIGHTNESS NORMALIZATION
    // Normalize each quadrant's diff by its average brightness so the score represents
    // percentage change rather than absolute change. A 20-point diff in a dark corner
    // (avg=40) scores higher than a 20-point diff near a bright window (avg=200).
    // Formula per quadrant: normalizedDiff = rawDiff / max(avgBrightness, B_MIN)
    // B_MIN prevents dark quadrants from blowing up on sensor noise.
    const uint32_t B_MIN = 10;
    const uint32_t* quadAvgBrightness = CameraGetQuadrantAvgBrightness();

    if (quadAvgBrightness) {
        for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
            uint32_t divisor = quadAvgBrightness[q];
            if (divisor < B_MIN) divisor = B_MIN;
            out.quadrantDiff[q] = out.quadrantDiff[q] / divisor;
        }
    }

    //------------------------
    // SCENE-WIDE SUPPRESSION
    // Get average quadrant diff across all quadrants, then subtract it from each quadrant.
    // This helps suppress scene-wide changes (for example, overall brightness shifting everywhere at once).
    uint64_t totalQuadrantDiff = 0;
    for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
        totalQuadrantDiff += out.quadrantDiff[q];
    }

    out.averageQuadrantDiff = (uint32_t)(totalQuadrantDiff / GRID_DIFF_NUM_QUADRANTS);

    for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
        if (out.quadrantDiff[q] > out.averageQuadrantDiff) {
            out.quadrantDiff[q] -= out.averageQuadrantDiff;
        } else {
            out.quadrantDiff[q] = 0;
        }
    }
    //------------------------


    free(current_frame.copyOfbufferInMemory);

    out.valid = true;

    return out;
}


//================================
// DEBUG ONLY
// Build a blocky grayscale image from the processed grid diff so the debug page
// background uses the same data as DivideFrameIntoGridAndDiff().
//================================
bool BuildGridDiffDebugImageJpg(uint8_t** jpgBufOut, size_t* jpgLenOut) {
    if (!jpgBufOut || !jpgLenOut) {
        log_print("BuildGridDiffDebugImageJpg: null output pointer");
        return false;
    }

    *jpgBufOut = nullptr;
    *jpgLenOut = 0;

    gridDiff diff = DivideFrameIntoGridAndDiff();
    if (!diff.valid) {
        log_print("BuildGridDiffDebugImageJpg: invalid grid diff");
        return false;
    }

    uint8_t* debugFrame = (uint8_t*)malloc((size_t)FW * FH);
    if (!debugFrame) {
        log_print("BuildGridDiffDebugImageJpg: malloc failed");
        return false;
    }

    const uint16_t QUAD_W = FW / GRID_DIFF_NUM_COLS;
    const uint16_t QUAD_H = FH / GRID_DIFF_NUM_ROWS;

    uint32_t maxQuadrantDiff = 0;
    for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
        if (diff.quadrantDiff[q] > maxQuadrantDiff) {
            maxQuadrantDiff = diff.quadrantDiff[q];
        }
    }

    for (uint32_t i = 0; i < (uint32_t)(FW * FH); i++) {
        const uint16_t x = i % FW;
        const uint16_t y = i / FW;

        const uint8_t col = min((uint8_t)(x / QUAD_W), (uint8_t)(GRID_DIFF_NUM_COLS - 1));
        const uint8_t row = min((uint8_t)(y / QUAD_H), (uint8_t)(GRID_DIFF_NUM_ROWS - 1));
        const uint8_t q   = row * GRID_DIFF_NUM_COLS + col;

        if (maxQuadrantDiff == 0) {
            debugFrame[i] = 0;
        } else {
            debugFrame[i] = (uint8_t)((diff.quadrantDiff[q] * 255ULL) / maxQuadrantDiff);
        }
    }

    bool ok = fmt2jpg(
        debugFrame,
        (size_t)FW * FH,
        FW,
        FH,
        PIXFORMAT_GRAYSCALE,
        80,
        jpgBufOut,
        jpgLenOut
    );

    free(debugFrame);

    if (!ok) {
        if (*jpgBufOut) {
            free(*jpgBufOut);
            *jpgBufOut = nullptr;
        }
        *jpgLenOut = 0;
        log_print("BuildGridDiffDebugImageJpg: fmt2jpg failed");
        return false;
    }

    return true;
}


//================================
// v3 Enter/Exit Detection — grid history + weighted center column tracking
//
// Instead of collapsing 30 quadrants into two numbers (left/right), this tracks
// the actual motion path through the grid over time by storing recent grid snapshots
// and computing a weighted center column each frame.
//
// If the center column moves from left to right over several frames = ENTER.
// If it moves right to left = EXIT.
//================================

// -- Tuning knobs --
// Spike threshold: since diffs are now brightness-normalized (divided by per-quadrant avg),
// the values are much smaller than the old raw diffs. Each quadrant has ~15,360 pixels
// (106*96 for most quadrants). A normalized diff of ~2 per pixel across half the quadrant
// is a reasonable motion signal.
static const uint32_t SPIKE_THRESHOLD           = 1500;  // normalized diff sum per quadrant to count as spiking
static const uint8_t  MIN_SPIKING_QUADRANTS     = 2;     // need at least this many spiking quadrants in a frame to consider it "motion"
static const uint32_t COOLDOWN_MS               = 800;   // ignore new events for this long after a detection
static const uint32_t MAX_HISTORY_AGE_MS        = 2000;  // discard history entries older than this (guards against stalls from SD writes, avg frame recompute, etc.)

// -- Grid history ring buffer --
static const uint8_t  HISTORY_LEN               = 5;     // number of frames of history to keep (~1.25s at 4fps)

static float    g_centerColHistory[HISTORY_LEN];
static bool     g_motionActiveHistory[HISTORY_LEN];
static uint32_t g_historyTimestamps[HISTORY_LEN];
static uint8_t  g_historyHead    = 0;
static uint8_t  g_historyCount   = 0;
static uint32_t g_lastEventMs    = 0;

bool EnterExitDetector_v2_wAvg() {

    gridDiff currentDiff = DivideFrameIntoGridAndDiff();
    if (!currentDiff.valid) {
        return false;
    }

    // Find spiking quadrants and compute weighted center column.
    // Weight = quadrant diff value, position = column index.
    uint8_t  spikingCount = 0;
    uint64_t weightedColSum = 0;
    uint64_t totalWeight    = 0;

    for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
        if (currentDiff.quadrantDiff[q] > SPIKE_THRESHOLD) {
            spikingCount++;
            const uint8_t col = q % GRID_DIFF_NUM_COLS;
            weightedColSum += (uint64_t)col * currentDiff.quadrantDiff[q];
            totalWeight    += currentDiff.quadrantDiff[q];
        }
    }

    bool motionActive = (spikingCount >= MIN_SPIKING_QUADRANTS && totalWeight > 0);
    float centerCol   = motionActive ? (float)weightedColSum / (float)totalWeight : -1.0f;

    // Push into ring buffer
    g_centerColHistory[g_historyHead]     = centerCol;
    g_motionActiveHistory[g_historyHead]  = motionActive;
    g_historyTimestamps[g_historyHead]    = millis();
    g_historyHead = (g_historyHead + 1) % HISTORY_LEN;
    if (g_historyCount < HISTORY_LEN) g_historyCount++;

    // Need at least 3 frames of history to detect a crossing
    if (g_historyCount < 3) return false;

    // Cooldown — don't double-count
    if ((millis() - g_lastEventMs) < COOLDOWN_MS) return false;

    // Find the oldest and newest frames in history that have active motion.
    // Walk the ring buffer from oldest to newest, skipping stale entries.
    const uint32_t now = millis();
    float firstActiveCol = -1.0f;
    float lastActiveCol  = -1.0f;
    uint8_t activeFrames = 0;

    for (uint8_t i = 0; i < g_historyCount; i++) {
        uint8_t idx = (g_historyHead + HISTORY_LEN - g_historyCount + i) % HISTORY_LEN;
        if ((now - g_historyTimestamps[idx]) > MAX_HISTORY_AGE_MS) continue;
        if (g_motionActiveHistory[idx]) {
            if (firstActiveCol < 0.0f) firstActiveCol = g_centerColHistory[idx];
            lastActiveCol = g_centerColHistory[idx];
            activeFrames++;
        }
    }

    // Need motion in at least 2 frames to establish direction
    if (activeFrames < 2) return false;

    float colShift = lastActiveCol - firstActiveCol;

    // The grid has columns 0-5. A full crossing is ~5 columns.
    // Require the center to shift by at least ~2 columns to count as a real crossing.
    const float MIN_COL_SHIFT = 1.8f;

    if (colShift > MIN_COL_SHIFT) {
        // Motion went left → right = ENTER
        g_lastEventMs = millis();

        // Clear history so we don't re-detect the same motion
        g_historyCount = 0;
        g_historyHead  = 0;

        log_print("Enter");
        extern int g_EntersCount;
        g_EntersCount++;
        SaveEvent("ENTER");
        return true;

    } else if (colShift < -MIN_COL_SHIFT) {
        // Motion went right → left = EXIT
        g_lastEventMs = millis();

        g_historyCount = 0;
        g_historyHead  = 0;

        log_print("Exit");
        extern int g_ExitsCount;
        g_ExitsCount++;
        SaveEvent("EXIT");
        return true;
    }

    return false;
}