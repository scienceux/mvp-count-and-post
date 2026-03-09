/*
 * CameraSetup - pass target FPS, returns success/fail
 * CameraGetTargetFps - returns configured FPS
 * CameraGetLatestFrame - captures and returns a grayscale frame
 * CameraRelease - pass frame to free its buffers
 * SaveLastFrameJpeg - pass frame, saves to /last_frame.jpg
 * StartVideoRecording - pass interval in minutes, starts saving MJPEG stream files to SD
 * StopVideoRecording - stops recording and closes current file
 * VideoRecordingLoop - call in loop(), captures frames at target FPS
 *
 * NOTE: This records raw MJPEG stream files (*.mjpg): JPEG frames concatenated back-to-back.
 * VLC can open these directly. A new file is created every intervalMinutes.
 */

// utilities_camera.cpp
#include "utilities_camera.h"
#include "esp_camera.h"
#include "utilities_led.h"
#include "img_converters.h"
#include <SD.h>
#include <stdlib.h>
#include "utilities_debug.h"


static File g_videoFile;
static bool g_videoOpen = false;
static uint32_t g_segment = 0;

// ---- camera pin config (XIAO ESP32S3 Sense) ----
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

static int g_targetFps = 4;
char global_averageFrame_path[64] = {0};

// PSRAM buffer holding the latest exponential average frame.
// Flat array of FW*FH uint8_t values (307,200 bytes for VGA).
// Each element is the averaged brightness (0=black, 255=white) of one pixel,
// stored row by row: index 0 = top-left (x=0,y=0), index FW-1 = top-right (x=639,y=0),
// If it's stored by row, why is it just [i] not [x][y]? Because it's a flat array, not a 2D array. 
//    So index i goes from 0 to FW*FH-1, and corresponds to pixel (x=i%FW, y=i/FW).
// index FW = start of row 2 (x=0,y=1), and so on.
// Updated in place by AverageFrameCreate(); read via CameraGetAverageFrame().
static uint8_t* g_avgFrame = nullptr;

static bool g_recording = false;
static unsigned long g_recordingStartMs = 0;
static unsigned long g_lastFrameCaptureMs = 0;
static unsigned long g_intervalMs = 15UL * 60UL * 1000UL; // default 15 minutes

static int g_currentSessionId = 0;
static int g_frameCount = 0;

static char g_currentVidPath[32] = {0};
static File g_vidFile;

static const char* kLastFrameJpgPath = "/last_frame.jpg";



bool CameraSetup(int targetFps, const char* DEVICE_MODE)
{
    enum CameraMode {
        MODE_GRAYSCALE,
        MODE_JPEG
    };

    CameraMode camMode =
        (strcmp(DEVICE_MODE, "VIDEO_FOR_TRAINING") == 0)
            ? MODE_JPEG
            : MODE_GRAYSCALE;

    g_targetFps = targetFps;

    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;

    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;

    // Grayscale for counting, or JPEG for training data?
    config.pixel_format = (camMode == MODE_JPEG) ? PIXFORMAT_JPEG : PIXFORMAT_GRAYSCALE;
    config.frame_size   = (camMode == MODE_JPEG) ? FRAMESIZE_VGA   : FRAMESIZE_VGA;
    config.jpeg_quality = (camMode == MODE_JPEG) ? 12              : 0;
    config.fb_count     = (camMode == MODE_JPEG) ? 2               : 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();

    // optional defaults
    sensor->set_gain_ctrl(sensor, 0); // Disable auto-gain because that f's with brightness
    sensor->set_agc_gain(sensor, 8);

    sensor->set_exposure_ctrl(sensor, 0);
    sensor->set_aec_value(sensor, 300); 

    sensor->set_whitebal(sensor, 0);
    sensor->set_awb_gain(sensor, 0);

    sensor->set_bpc(sensor, 0);              // disable bad pixel correction
    sensor->set_wpc(sensor, 0);              // disable white pixel correction    

    return true;
}

int CameraGetTargetFps()
{
    return g_targetFps;
}

Frame CameraGetCopyOfLatestFrame()
{
    Frame out;
    out.copyOfbufferInMemory = nullptr;
    out.timecaptured = 0;
    out.valid = false;

    // Pull eyeball out of socket to get latest frame
    camera_fb_t* fb = esp_camera_fb_get();

    // Pave a parking space in memory for our (numberic) pixel data of size fb->len
    out.copyOfbufferInMemory = (uint32_t*)malloc(fb->len);

    // Park actual pixel data into our parking space
    memcpy(out.copyOfbufferInMemory, fb->buf, fb->len);

    // Give it our standard metadata
    out.timecaptured = millis();
    out.valid = true;

    // Put eyeball back in socket to free driver buffers
    esp_camera_fb_return(fb);

    return out;
}



void CameraRelease(const Frame& frame)
{
    if (frame.valid) {
        esp_camera_fb_return((camera_fb_t*)frame.copyOfbufferInMemory);
    }
}

bool CameraSaveSnapToSD(const char* path)
{
    if (!path) return false;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return false;

    uint8_t* jpgBuf = nullptr;
    size_t   jpgLen = 0;
    bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height,
                      PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);
    esp_camera_fb_return(fb);

    if (!ok || !jpgBuf) return false;

    File f = SD.open(path, FILE_WRITE);
    if (!f) { free(jpgBuf); return false; }
    f.write(jpgBuf, jpgLen);
    f.close();
    free(jpgBuf);
    return true;
}



const uint8_t* CameraGetAverageFrame() {
    return g_avgFrame;
}


// Capture and exponentially average frames for numSecondsToAverage seconds,
// then save the result as a JPEG to /average-frames/ on the SD card.
// Update g_avgFrame in-place in PSRAM, which we can read later via CameraGetAverageFrame() for motion detection.
bool AverageFrameCreate(int numSecondsToAverage) {
    turnOnLED();

    const uint32_t numMillisToAverage = (uint32_t)numSecondsToAverage * 1000UL;
    const uint32_t start = millis();

    log_print(String("Averaging frames for ") + numSecondsToAverage + " seconds...");

    // Check for average-frames folder on SD card, create if it doesn't exist
    if (!SD.exists("/average-frames")) {
        SD.mkdir("/average-frames");
    }

    const float alpha = 0.05f;
    const size_t NPIX = (size_t)FW * (size_t)FH;

    // Allocate the global PSRAM buffer once; reuse it on subsequent calls
    if (!g_avgFrame) {
        g_avgFrame = (uint8_t*)ps_malloc(NPIX);
        if (!g_avgFrame) {
            log_print("Failed to allocate PSRAM for average frame");
            turnOffLED();
            return false;
        }
        memset(g_avgFrame, 0, NPIX);
    }

    while ((millis() - start) < numMillisToAverage) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { turnOffLED(); return false; }

        // IMPORTANT: assumes fb->format == PIXFORMAT_GRAYSCALE and fb->len == NPIX
        for (size_t i = 0; i < NPIX; i++) {
            const uint8_t cur = fb->buf[i];

            //**********
            // The Average Frame
            // "New average = old average + fraction of (new value - old average)"
            // Moves toward new value slowly, making it stable against sudden brightness changes
            // Updates average frame in-place in PSRAM so we don't have to copy it around or use more memory
            //**********
            const float updated = (float)g_avgFrame[i] + alpha * ((float)cur - (float)g_avgFrame[i]);
            g_avgFrame[i] = (uint8_t)(updated + 0.5f);
        }

        esp_camera_fb_return(fb);
        delay(100);
    }

    turnOffLED();

    // Save average frame to SD card for debugging/visualization
    uint8_t* jpgBuf = nullptr;
    size_t   jpgLen = 0;
    bool ok = fmt2jpg(g_avgFrame, NPIX, FW, FH, PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);
    if (ok && jpgBuf) {
        uint32_t timestamp = millis();
        char filename[64];
        snprintf(filename, sizeof(filename), "/average-frames/last_average_frame.jpg", timestamp);
        File f = SD.open(filename, FILE_WRITE);
        if (f) {
            f.write(jpgBuf, jpgLen);
            f.close();
        }
        free(jpgBuf);
    }

    return true;
}
