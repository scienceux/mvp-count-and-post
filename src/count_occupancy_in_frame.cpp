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
// Simple Enter/Exit Detection Based on Left vs Right Brightness Changes
// Call these first in main to create the SplitFrame
    // Save copy of one frame to memory, then release it so camera can reuse the buffer (and we don't run out of memory)
    // Frame prev_frame = CameraGetCopyOfLatestFrame();
    // delay(100);
    // Frame current_frame = CameraGetCopyOfLatestFrame();
//    SplitFrame prev_frame_split = CameraGetSplitFrame(prev_frame);
//    SplitFrame current_frame_split = CameraGetSplitFrame(current_frame);

// then:
    // free(prev_frame.copyOfbufferInMemory);
    // free(current_frame.copyOfbufferInMemory);
//================================
bool EnterExitDetector(SplitFrame prev_frame_split, SplitFrame current_frame_split) {
    uint32_t changeInBrightnessLeft  = abs(current_frame_split.leftBrightness  - prev_frame_split.leftBrightness);
    uint32_t changeInBrightnessRight = abs(current_frame_split.rightBrightness - prev_frame_split.rightBrightness);
    uint32_t threshold = 1000000; // Total brightness can reach into the tens of millions, so this threshold is in the millions.
    // log_print("Change in brightness:    " + String(changeInBrightnessLeft)    + " | " + String(changeInBrightnessRight));

    if ( !enterStarted && !exitStarted ) {
        if ( (changeInBrightnessLeft > changeInBrightnessRight) && (changeInBrightnessLeft > threshold) ) {
            enterStarted = true;
            log_print("Enter started");
        } else if ( (changeInBrightnessRight > changeInBrightnessLeft) && (changeInBrightnessRight > threshold) ) {
            exitStarted = true;
            log_print("Exit started");
        }
    }

    // Then they cross into other half of frame, so we know they fully entered or exited
    if ( enterStarted && (changeInBrightnessRight > threshold) ) {
        enterStarted = false;
        log_print("Enter");
        extern int g_EntersCount;
        g_EntersCount++;
        return true;
    } else if ( exitStarted && (changeInBrightnessLeft > threshold) ) {
        exitStarted = false;
        log_print("Exit");
        extern int g_ExitsCount;
        g_ExitsCount++;
        return true;
    }

    if ( (enterStarted || exitStarted) && (changeInBrightnessLeft < threshold) && (changeInBrightnessRight < threshold) ) {
        enterStarted = false;
        exitStarted = false;
        log_print("Reset");
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
    // Extra DIFF
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
// v2 Enter/Exit Detection using quadrant grid + average frame diff
//================================
bool EnterExitDetector_v2_wAvg() {
    // log_print("counting occupancy in frame...");

    const uint32_t howMuchDiffCountsAsASpike           = 200000;  // total abs diff per quadrant to count as a spike
    const uint8_t  howManyQuadrantsSpikeCountsAsMotion = 2;       // need at least 2 spiking quadrants on a side to count

    gridDiff currentDiff = DivideFrameIntoGridAndDiff();
    if (!currentDiff.valid) {
        return false;
    }

    // Count how many quadrants spiked on each side
    uint8_t spikingLeft  = 0;
    uint8_t spikingRight = 0;

    for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
        if (currentDiff.quadrantDiff[q] > howMuchDiffCountsAsASpike) {
            const uint8_t col = q % GRID_DIFF_NUM_COLS;
            if (col < GRID_DIFF_NUM_COLS / 2) spikingLeft++;
            else                    spikingRight++;
        }
    }

    // log_print("Spiking L: " + String(spikingLeft) + " R: " + String(spikingRight));

    // --- State machine (same logic as v1, now driven by quadrant spike counts) ---

    if (!enterStarted && !exitStarted) {
        if (spikingLeft >= howManyQuadrantsSpikeCountsAsMotion && spikingLeft > spikingRight) {
            enterStarted = true;
            log_print("Enter started");
            return false;
        } else if (spikingRight >= howManyQuadrantsSpikeCountsAsMotion && spikingRight > spikingLeft) {
            exitStarted = true;
            log_print("Exit started");
            return false;
        }
    }

    // Confirmed crossing: started on one side, now the other side spikes
    if (enterStarted && spikingRight >= howManyQuadrantsSpikeCountsAsMotion) {
        enterStarted = false;

        //**********
        // ENTER!
        //**********wa
        log_print("Enter");
        extern int g_EntersCount;
        g_EntersCount++;
        SaveEvent("ENTER");
        

        
        //_________________________   
        // FOR DEBUG: Save frame diff (not frame) as diffred frame to SD in /enters folder for later review and threshold tuning
        // Get diff frame (current frame - average frame)
        // uint8_t* diffFrame = (uint8_t*)malloc(FW * FH);
        // for (uint32_t i = 0; i < (FW * FH); i++) {
        //     diffFrame[i] = (uint8_t)abs((int)pixels[i] - (int)avgFrame[i]);
        // }
        // Save diff frame as JPEG to SD card
        // char path[64];
        // sprintf(path, "/enters/last_enter.jpg", millis());
        // uint8_t* jpgBuf = nullptr;
        // size_t jpgLen = 0;
        // bool ok = fmt2jpg(diffFrame, (size_t)FW * FH, FW, FH, PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);
        // if (ok && jpgBuf) {
        //     File f = SD.open(path, FILE_WRITE);
        //     if (f) {
        //         f.write(jpgBuf, jpgLen);
        //         f.close();
        //     }
        //     free(jpgBuf);
        // }
        // free(diffFrame);
        //_________________________   



        return true;
    } else if (exitStarted && spikingLeft >= howManyQuadrantsSpikeCountsAsMotion) {

        //**********
        // EXIT!
        //**********
        exitStarted = false;
        log_print("Exit confirmed");
        extern int g_ExitsCount;
        g_ExitsCount++;
        SaveEvent("EXIT");
        

        //===================================        
        // FOR DEBUG: Save frame diff (not frame) as diffred frame to SD in /exits folder for later review and threshold tuning
        // Get diff frame (current frame - average frame)
        // uint8_t* diffFrame = (uint8_t*)malloc(FW * FH);
        // for (uint32_t i = 0; i < (FW * FH); i++) {
        //     diffFrame[i] = (uint8_t)abs((int)pixels[i] - (int)avgFrame[i]);
        // }
        // // Save diff frame as JPEG to SD card
        // char path[64];
        // sprintf(path, "/exits/exit_diff_%lu.jpg", millis());
        // uint8_t* jpgBuf = nullptr;
        // size_t jpgLen = 0;
        // bool ok = fmt2jpg(diffFrame, (size_t)FW * FH, FW, FH, PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);
        // if (ok && jpgBuf) {
        //     File f = SD.open(path, FILE_WRITE);
        //     if (f) {
        //         f.write(jpgBuf, jpgLen);
        //         f.close();
        //     }
        //     free(jpgBuf);
        // }
        // free(diffFrame);
        //=================================== 
        
        return true;
    }

    // Reset if motion died out before completing a crossing
    if ((enterStarted || exitStarted) && spikingLeft < howManyQuadrantsSpikeCountsAsMotion && spikingRight < howManyQuadrantsSpikeCountsAsMotion) {
        enterStarted = false;
        exitStarted  = false;
        log_print("Reset");
        return false;
    }

    return false;
}