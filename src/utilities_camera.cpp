/*
 * CameraSetup - pass target FPS, returns success/fail
 * CameraGetTargetFps - returns configured FPS
 * CameraSetFrameRotation - pass degrees (0/90/180/270), returns success/fail
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
static int g_rotationDegrees = 0;
char global_averageFrame_path[64] = {0};

static bool g_recording = false;
static unsigned long g_recordingStartMs = 0;
static unsigned long g_lastFrameCaptureMs = 0;
static unsigned long g_intervalMs = 15UL * 60UL * 1000UL; // default 15 minutes

static int g_currentSessionId = 0;
static int g_frameCount = 0;

static char g_currentVidPath[32] = {0};
static File g_vidFile;

static const char* kLastFrameJpgPath = "/last_frame.jpg";


static bool RotateGrayFrame(const camera_fb_t* src, int degrees, camera_fb_t** outFb, uint8_t** outBuf)
{
    if (!src || !outFb || !outBuf) return false;
    if (src->format != PIXFORMAT_GRAYSCALE) return false;

    int norm = degrees % 360;
    if (norm < 0) norm += 360;
    if (!(norm == 90 || norm == 180 || norm == 270)) return false;

    const int w = (int)src->width;
    const int h = (int)src->height;
    const size_t pixelCount = (size_t)w * (size_t)h;

    const int newW = (norm == 90 || norm == 270) ? h : w;
    const int newH = (norm == 90 || norm == 270) ? w : h;

    uint8_t* dst = (uint8_t*)malloc(pixelCount);
    if (!dst) return false;

    const uint8_t* srcBuf = src->buf;

    if (norm == 90) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int srcIdx = y * w + x;
                const int dstX = h - 1 - y;
                const int dstY = x;
                const int dstIdx = dstY * newW + dstX;
                dst[dstIdx] = srcBuf[srcIdx];
            }
        }
    } else if (norm == 180) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int srcIdx = y * w + x;
                const int dstX = w - 1 - x;
                const int dstY = h - 1 - y;
                const int dstIdx = dstY * newW + dstX;
                dst[dstIdx] = srcBuf[srcIdx];
            }
        }
    } else { // 270
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int srcIdx = y * w + x;
                const int dstX = y;
                const int dstY = w - 1 - x;
                const int dstIdx = dstY * newW + dstX;
                dst[dstIdx] = srcBuf[srcIdx];
            }
        }
    }

    camera_fb_t* rotated = (camera_fb_t*)calloc(1, sizeof(camera_fb_t));
    if (!rotated) {
        free(dst);
        return false;
    }

    rotated->buf = dst;
    rotated->len = pixelCount;
    rotated->width = newW;
    rotated->height = newH;
    rotated->format = src->format;

    *outFb = rotated;
    *outBuf = dst;
    return true;
}



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

bool CameraSetFrameRotation(int degrees)
{
    int norm = degrees % 360;
    if (norm < 0) norm += 360;
    if (norm == 0 || norm == 90 || norm == 180 || norm == 270) {
        g_rotationDegrees = norm;
        return true;
    }
    return false;
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

// bool SaveLastFrameJpeg(const Frame& frame)
// {
//     if (!frame.valid || !frame.copyOfbufferInMemory) return false;

//     uint8_t* jpgBuf = nullptr;
//     size_t   jpgLen = 0;

//     // Convert grayscale buffer to JPEG in memory
//     bool ok = fmt2jpg((uint8_t*)frame.copyOfbufferInMemory, frame.fb->len,
//                       frame.fb->width, frame.fb->height,
//                       PIXFORMAT_GRAYSCALE, 80,
//                       &jpgBuf, &jpgLen);
//     if (!ok || !jpgBuf) return false;

//     // Write to SD card, overwriting any previous file
//     File f = SD.open(kLastFrameJpgPath, FILE_WRITE);
//     if (!f) { free(jpgBuf); return false; }
//     f.write(jpgBuf, jpgLen);
//     f.close();

//     free(jpgBuf);
//     return true;
// }

bool StartNewVideo(const char* folder)
{
    if (g_videoOpen) CloseOffVideo();

    if (!SD.exists(folder)) SD.mkdir(folder);

    char path[96];
    snprintf(path, sizeof(path),
             "%s/segment_%06lu.mjpg",
             folder,
             (unsigned long)g_segment++);

    g_videoFile = SD.open(path, FILE_WRITE);
    if (!g_videoFile) return false;

    g_videoOpen = true;
    return true;
}

// After StartnewVideo, append raw MJPEG frames to it as they come in. VLC can open these directly. 
void AddToVideo()
{
    if (!g_videoOpen) return;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return;

    g_videoFile.write(fb->buf, fb->len);

    esp_camera_fb_return(fb);
}

void CloseOffVideo()
{
    if (!g_videoOpen) return;

    g_videoFile.flush();
    g_videoFile.close();
    g_videoOpen = false;
}