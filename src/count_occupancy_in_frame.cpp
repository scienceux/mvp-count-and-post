// Load tools: SD, time, math, camera.
#include "count_occupancy_in_frame.h"
#include <Arduino.h>
#include <SD.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include "img_converters.h"
#include "utilities_debug.h"

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

bool EnterExitDetector(SplitFrame prev_frame_split, SplitFrame current_frame_split) {
    uint32_t changeInBrightnessLeft  = abs(current_frame_split.leftBrightness  - prev_frame_split.leftBrightness);
    uint32_t changeInBrightnessRight = abs(current_frame_split.rightBrightness - prev_frame_split.rightBrightness);
    uint32_t threshold = 1000000; // Total brightness can reach into the tens of millions, so this threshold is in the millions.
    // log_print("Change in brightness:    " + String(changeInBrightnessLeft)    + " | " + String(changeInBrightnessRight));

    if ( (changeInBrightnessLeft > changeInBrightnessRight) && (changeInBrightnessLeft > threshold) ) {
        log_print("Enter");
        return true;
    } else if ( (changeInBrightnessRight > changeInBrightnessLeft) && (changeInBrightnessRight > threshold) ) {
        log_print("Exit");
        return true;
    }

    return false;  

}