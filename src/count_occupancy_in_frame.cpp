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


SplitFrame CameraGetSplitFrame() {

    static int32_t temp_frame[2] = { 0 };
    temp_frame[0] = 0;
    temp_frame[1] = 0;

    // capture image from camera
    camera_fb_t *frame_buffer = esp_camera_fb_get();          // capture frame from camera

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

      const int32_t pixelBrightness = frame_buffer->buf[i];             // get the pixels brightness (0 to 255)


      temp_frame[whichside_x] += pixelBrightness;                 
    }

    esp_camera_fb_return(frame_buffer);                       // return frame so memory can be released


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

void EnterExitDetector(SplitFrame prev_frame, SplitFrame current_frame) {
    uint32_t changeInBrightnessLeft  = abs(current_frame.leftBrightness  - prev_frame.leftBrightness);
    uint32_t changeInBrightnessRight = abs(current_frame.rightBrightness - prev_frame.rightBrightness);
    uint32_t threshold = 100000; // TODO - figure out good threshold for this

    if ( (changeInBrightnessLeft > changeInBrightnessRight) && (changeInBrightnessLeft > threshold) ) {
        log_print("Enter detected!");
    } else if ( (changeInBrightnessRight > changeInBrightnessLeft) && (changeInBrightnessRight > threshold) ) {
        log_print("Exit detected!");
    }

    // // I think this initializes arrays containing pixels at different coordinates in each frame.
    // uint16_t prev_frame[FH][FW] = { 0 };      
    // uint16_t current_frame[FH][FW] = { 0 };   

    // // For the number of pixels high this is, loop through each pixel of the width.
    // for (int y = 0; y < FH; y++) {
    //     for (int x = 0; x < FW; x++) {
    //         uint16_t current = current_frame[y][x];
    //         uint16_t prev = prev_frame[y][x];
    //         uint16_t pChange = abs(current - prev);          // modified code Feb20 - gives blocks average pixels variation in range 0 to 255

    //         if (pChange >= tThreshold) {                     // if change in block is enough to qualify as changed
    //           if (block_active(x,y)) changes += 1;           // if detection mask is enabled for this block increment changed block count
    //         }
    //     }
    // }        

}