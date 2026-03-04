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
// v2 Enter/Exit Detection using quadrant grid + average frame diff
//================================
bool EnterExitDetector_v2_wAvg() {
    // log_print("counting occupancy in frame...");

    const uint8_t  NUM_COLS                            = 4;
    const uint8_t  NUM_ROWS                            = 3;
    const uint8_t  howManyQuadrants                    = NUM_COLS * NUM_ROWS;  // 12
    const uint32_t howMuchDiffCountsAsASpike           = 200000;  // total abs diff per quadrant to count as a spike
    const uint8_t  howManyQuadrantsSpikeCountsAsMotion = 2;       // need at least 1 spiking quadrant on a side to count

    const uint16_t QUAD_W = FW / NUM_COLS;  // 160px wide per quadrant
    const uint16_t QUAD_H = FH / NUM_ROWS;  // 160px tall per quadrant

    uint32_t quadrantDiff[12] = { 0 };  // total abs diff vs average frame, per quadrant

    // Use average frame as baseline
    const uint8_t* avgFrame = CameraGetAverageFrame();
    if (!avgFrame) {
        log_print("EnterExitDetector_v2: no average frame");
        return false;
    }

    // Get current frame
    Frame current_frame = CameraGetCopyOfLatestFrame();
    if (!current_frame.valid) {
        log_print("EnterExitDetector_v2: no current frame");
        return false;
    }

    const uint8_t* pixels = (uint8_t*)current_frame.copyOfbufferInMemory;

    // Walk every pixel, accumulate abs diff into its quadrant bucket
    for (uint32_t i = 0; i < (uint32_t)(FW * FH); i++) {
        const uint16_t x   = i % FW;
        const uint16_t y   = i / FW;

        const uint8_t col = x / QUAD_W;
        const uint8_t row = y / QUAD_H;
        const uint8_t q   = row * NUM_COLS + col;

        const uint32_t diff = (uint32_t)abs((int)pixels[i] - (int)avgFrame[i]);
        quadrantDiff[q] += diff;
    }

    free(current_frame.copyOfbufferInMemory);

    // Count how many quadrants spiked on each side
    uint8_t spikingLeft  = 0;
    uint8_t spikingRight = 0;

    for (uint8_t q = 0; q < howManyQuadrants; q++) {
        if (quadrantDiff[q] > howMuchDiffCountsAsASpike) {
            const uint8_t col = q % NUM_COLS;
            if (col < NUM_COLS / 2) spikingLeft++;
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
        //**********
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
        // // Save diff frame as JPEG to SD card
        // char path[64];
        // sprintf(path, "/enters/enter_diff_%lu.jpg", millis());
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
        log_print("Reset - motion died");
        return false;
    }

    return false;
}