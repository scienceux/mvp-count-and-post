/*
 * CameraSetup - pass target FPS, returns success/fail
 * CameraGetTargetFps - returns configured FPS
 * CameraSetFrameRotation - pass degrees (0/90/180/270), returns success/fail
 * CameraGetLatestFrame - captures and returns a grayscale frame
 * CameraRelease - pass frame to free its buffers
 * SaveLastFrameJpeg - pass frame, saves to /last_frame.jpg
 * StartVideoRecording - pass interval in minutes, starts saving frames to SD
 * StopVideoRecording - stops recording and closes current folder
 * VideoRecordingLoop - call in loop(), captures frames at target FPS
 */

// utilities_camera.cpp
#include "utilities_camera.h"
#include "esp_camera.h"
#include "utilities_led.h"
#include "img_converters.h"
#include <SD.h>
#include <stdlib.h>

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
static unsigned long g_intervalMs = 5 * 60 * 1000; // 5 minutes default
static int g_currentSessionId = 0;
static int g_frameCount = 0;
static char g_currentAviPath[32] = {0};
static File g_aviFile;
static unsigned long g_aviFrameDataSize = 0;

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
    } else {
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

static bool SaveLastFrameJpegInternal(const Frame& frame)
{
    if (!frame.valid || !frame.fb || frame.fb->format != PIXFORMAT_GRAYSCALE) {
        return false;
    }

    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;
    bool jpgOk = fmt2jpg(frame.fb->buf, frame.fb->len, frame.fb->width, frame.fb->height,
                         PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);

    if (jpgOk && jpgBuf && jpgLen > 0) {
        SD.remove(kLastFrameJpgPath);
        File f = SD.open(kLastFrameJpgPath, FILE_WRITE);
        if (f) {
            f.write(jpgBuf, jpgLen);
            f.close();
        }
        free(jpgBuf);
        return true;
    }

    if (jpgBuf) {
        free(jpgBuf);
    }
    return false;
}

// AVI header for MJPEG, filled in when closing file
static void WriteAviHeader(File& f, int width, int height, int fps, int frameCount, unsigned long totalFrameSize)
{
    uint32_t usPerFrame = 1000000 / fps;
    uint32_t aviSize = totalFrameSize + 12 + 56 + 124 + 16 + frameCount * 8;
    
    // RIFF header
    f.seek(0);
    f.write((const uint8_t*)"RIFF", 4);
    uint32_t riffSize = aviSize - 8;
    f.write((uint8_t*)&riffSize, 4);
    f.write((const uint8_t*)"AVI ", 4);
    
    // hdrl LIST
    f.write((const uint8_t*)"LIST", 4);
    uint32_t hdrlSize = 192;
    f.write((uint8_t*)&hdrlSize, 4);
    f.write((const uint8_t*)"hdrl", 4);
    
    // avih chunk
    f.write((const uint8_t*)"avih", 4);
    uint32_t avihSize = 56;
    f.write((uint8_t*)&avihSize, 4);
    f.write((uint8_t*)&usPerFrame, 4);       // microseconds per frame
    uint32_t maxBytesPerSec = width * height * fps;
    f.write((uint8_t*)&maxBytesPerSec, 4);
    uint32_t padding = 0;
    f.write((uint8_t*)&padding, 4);          // padding granularity
    uint32_t flags = 16;                      // AVIF_HASINDEX
    f.write((uint8_t*)&flags, 4);
    f.write((uint8_t*)&frameCount, 4);       // total frames
    f.write((uint8_t*)&padding, 4);          // initial frames
    uint32_t streams = 1;
    f.write((uint8_t*)&streams, 4);
    uint32_t bufSize = width * height;
    f.write((uint8_t*)&bufSize, 4);
    f.write((uint8_t*)&width, 4);
    f.write((uint8_t*)&height, 4);
    for (int i = 0; i < 4; i++) f.write((uint8_t*)&padding, 4); // reserved
    
    // strl LIST
    f.write((const uint8_t*)"LIST", 4);
    uint32_t strlSize = 116;
    f.write((uint8_t*)&strlSize, 4);
    f.write((const uint8_t*)"strl", 4);
    
    // strh chunk
    f.write((const uint8_t*)"strh", 4);
    uint32_t strhSize = 56;
    f.write((uint8_t*)&strhSize, 4);
    f.write((const uint8_t*)"vids", 4);
    f.write((const uint8_t*)"MJPG", 4);
    f.write((uint8_t*)&padding, 4);          // flags
    uint16_t priority = 0;
    f.write((uint8_t*)&priority, 2);
    f.write((uint8_t*)&priority, 2);         // language
    f.write((uint8_t*)&padding, 4);          // initial frames
    uint32_t scale = 1;
    f.write((uint8_t*)&scale, 4);
    uint32_t rate = fps;
    f.write((uint8_t*)&rate, 4);
    f.write((uint8_t*)&padding, 4);          // start
    f.write((uint8_t*)&frameCount, 4);       // length
    f.write((uint8_t*)&bufSize, 4);          // suggested buffer
    uint32_t quality = 10000;
    f.write((uint8_t*)&quality, 4);
    f.write((uint8_t*)&padding, 4);          // sample size
    int16_t frame[4] = {0, 0, (int16_t)width, (int16_t)height};
    f.write((uint8_t*)frame, 8);
    
    // strf chunk
    f.write((const uint8_t*)"strf", 4);
    uint32_t strfSize = 40;
    f.write((uint8_t*)&strfSize, 4);
    f.write((uint8_t*)&strfSize, 4);         // biSize
    f.write((uint8_t*)&width, 4);
    f.write((uint8_t*)&height, 4);
    uint16_t planes = 1;
    f.write((uint8_t*)&planes, 2);
    uint16_t bitCount = 24;
    f.write((uint8_t*)&bitCount, 2);
    f.write((const uint8_t*)"MJPG", 4);      // compression
    uint32_t imgSize = width * height * 3;
    f.write((uint8_t*)&imgSize, 4);
    for (int i = 0; i < 4; i++) f.write((uint8_t*)&padding, 4); // remaining fields
}

static void StartNewAviFile()
{
    g_currentSessionId++;
    g_frameCount = 0;
    g_aviFrameDataSize = 0;
    snprintf(g_currentAviPath, sizeof(g_currentAviPath), "/vid_%04d.avi", g_currentSessionId);
    
    g_aviFile = SD.open(g_currentAviPath, FILE_WRITE);
    if (g_aviFile)
    {
        // Write placeholder header (256 bytes), will rewrite when closing
        uint8_t placeholder[256] = {0};
        g_aviFile.write(placeholder, 256);
        
        // movi LIST header
        g_aviFile.write((const uint8_t*)"LIST", 4);
        uint32_t moviSizePlaceholder = 0;
        g_aviFile.write((uint8_t*)&moviSizePlaceholder, 4);
        g_aviFile.write((const uint8_t*)"movi", 4);
    }
}

static void CloseCurrentAviFile()
{
    if (!g_aviFile) return;
    
    // Update movi LIST size
    uint32_t moviSize = g_aviFrameDataSize + 4 + g_frameCount * 8;
    g_aviFile.seek(260);
    g_aviFile.write((uint8_t*)&moviSize, 4);
    
    // Write final header
    WriteAviHeader(g_aviFile, 320, 240, g_targetFps, g_frameCount, g_aviFrameDataSize);
    
    g_aviFile.close();
}

bool CameraSetup(int targetFps)
{
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

    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size   = FRAMESIZE_QVGA;    // 320x240 for training data
    config.jpeg_quality = 12;
    config.fb_count     = 2;

    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK) {
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();

    // optional defaults
    s->set_gain_ctrl(s, 0);
    s->set_exposure_ctrl(s, 0);
    s->set_whitebal(s, 0);
    s->set_awb_gain(s, 0);

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

Frame CameraGetLatestFrame()
{

    // Serial.println("📸"); // Careful. This will print 4 times a second if you let it

    // Start with an "empty" frame so we always return something safe
    Frame out;
    out.fb = nullptr;
    out.raw_fb = nullptr;
    out.rotated_fb = nullptr;
    out.rotated_buf = nullptr;
    out.capturedMs = 0;
    out.valid = false;
    out.rotated = false;

    // Ask the camera for the newest picture it has
    camera_fb_t* fb = esp_camera_fb_get();

    // If the camera couldn't give us a picture, return the empty frame
    if (!fb) {
        blinkLED(2, "SOS"); // Indicate error
        return out;
    }

    // We did get a picture, so fill in the details
    out.fb = fb;
    out.raw_fb = fb;
    out.capturedMs = millis(); // "now" in milliseconds since boot
    out.valid = true;

    if (g_rotationDegrees != 0 && fb->format == PIXFORMAT_GRAYSCALE) {
        camera_fb_t* rotated_fb = nullptr;
        uint8_t* rotated_buf = nullptr;
        if (RotateGrayFrame(fb, g_rotationDegrees, &rotated_fb, &rotated_buf)) {
            out.fb = rotated_fb;
            out.rotated_fb = rotated_fb;
            out.rotated_buf = rotated_buf;
            out.rotated = true;
        }
    }

    // Give the frame back to the caller to use
    return out;
}

void CameraRelease(const Frame& frame)
{
    // If the frame is valid, return its buffer to the camera driver
    if (frame.valid) {
        if (frame.rotated) {
            if (frame.rotated_buf) {
                free(frame.rotated_buf);
            }
            if (frame.rotated_fb) {
                free(frame.rotated_fb);
            }
        }

        if (frame.raw_fb) {
            esp_camera_fb_return(frame.raw_fb);
        } else if (frame.fb) {
            esp_camera_fb_return(frame.fb);
        }
        // Serial.println("Camera buffer returned to be ready to take photos again."); // Careful. This will print 4 times a second if you let it
    }
}

bool SaveLastFrameJpeg(const Frame& frame)
{
    return SaveLastFrameJpegInternal(frame);
}

bool StartVideoRecording(int intervalMinutes)
{
    if (g_recording) return false;
    
    g_intervalMs = (unsigned long)intervalMinutes * 60UL * 1000UL;
    g_recordingStartMs = millis();
    g_lastFrameCaptureMs = 0;
    g_recording = true;
    
    StartNewAviFile();
    
    return true;
}

void StopVideoRecording()
{
    if (g_recording)
    {
        CloseCurrentAviFile();
    }
    g_recording = false;
}

// ...existing code...

void VideoRecordingLoop()
{
    if (!g_recording) return;
    
    unsigned long now = millis();
    unsigned long frameIntervalMs = 1000 / g_targetFps;
    
    // Check if it's time for a new frame
    if (now - g_lastFrameCaptureMs < frameIntervalMs) return;
    g_lastFrameCaptureMs = now;
    
    // Check if interval elapsed, start new file
    if (now - g_recordingStartMs >= g_intervalMs)
    {
        CloseCurrentAviFile();
        StartNewAviFile();
        g_recordingStartMs = now;
    }
    
    // Capture frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return;
    
    // Convert to JPEG
    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;
    bool converted = frame2jpg(fb, 80, &jpgBuf, &jpgLen);
    
    if (converted && jpgBuf && g_aviFile)
    {
        // Write frame chunk: "00dc" + size + data (padded to even)
        g_aviFile.write((const uint8_t*)"00dc", 4);
        uint32_t chunkSize = jpgLen;
        g_aviFile.write((uint8_t*)&chunkSize, 4);
        g_aviFile.write(jpgBuf, jpgLen);
        
        // Pad to even byte boundary
        if (jpgLen % 2 == 1)
        {
            uint8_t pad = 0;
            g_aviFile.write(&pad, 1);
            g_aviFrameDataSize++;
        }
        
        g_aviFrameDataSize += 8 + jpgLen;
        g_frameCount++;
        
        free(jpgBuf);
    }
    
    esp_camera_fb_return(fb);
}
