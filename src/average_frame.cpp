// average_frame.cpp
#include "average_frame.h"
#include "esp_camera.h"
#include "img_converters.h"
#include <SD.h>
#include "utilities_led.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>

namespace
{
    const char* kAvgFrameRawPath = "/avg_frame_current.raw";
    const char* kAvgFrameJpgPath = "/avg_frame_current.jpg";
    const char* kAvgFrameCountPath = "/avg_frame_current.count";
    const char* kAvgFrameBackupDir = "/avg_frame_backup";

    bool HasValidTime(struct tm* t)
    {
        return t && (t->tm_year + 1900) >= 2020;
    }

    void BackupIfExists(const char* currentPath)
    {
        if (!SD.exists(currentPath)) return;

        SD.mkdir(kAvgFrameBackupDir);

        char backupPath[96] = {0};
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);

        if (HasValidTime(t)) {
            snprintf(backupPath, sizeof(backupPath),
                     "%s/avg_%04d%02d%02d_%02d%02d%02d%s",
                     kAvgFrameBackupDir,
                     t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                     t->tm_hour, t->tm_min, t->tm_sec,
                     strstr(currentPath, ".raw") ? ".raw" : ".jpg");
        } else {
            snprintf(backupPath, sizeof(backupPath),
                     "%s/avg_%lu%s",
                     kAvgFrameBackupDir,
                     static_cast<unsigned long>(millis()),
                     strstr(currentPath, ".raw") ? ".raw" : ".jpg");
        }

        SD.rename(currentPath, backupPath);
    }

    bool IsAppendMode(const char* newOrAppend)
    {
        return newOrAppend && strcmp(newOrAppend, "append") == 0;
    }
}

bool CameraCreateAvgFrame(int durationSeconds, const char* newOrAppend)
{
    const int targetFps = CameraGetTargetFps();
    if (targetFps <= 0 || durationSeconds <= 0) {
        return false;
    }

    turnOnLED();

    const int totalFrames = targetFps * durationSeconds;
    const uint32_t frameIntervalMs = 1000 / targetFps;

    const bool appendRequested = IsAppendMode(newOrAppend);

    Frame frame = CameraGetLatestFrame();
    if (!frame.valid || !frame.fb || frame.fb->format != PIXFORMAT_GRAYSCALE) {
        CameraRelease(frame);
        turnOffLED();
        return false;
    }

    const size_t frameLen = frame.fb->len;
    const int width = frame.fb->width;
    const int height = frame.fb->height;

    // Accumulator for per-pixel sums across frames
    uint32_t* sum = (uint32_t*)calloc(frameLen, sizeof(uint32_t));
    if (!sum) {
        CameraRelease(frame);
        turnOffLED();
        return false;
    }

    int previousCount = 0;
    uint8_t* previousAvg = nullptr;
    bool isAppend = false;

    if (appendRequested && SD.exists(kAvgFrameRawPath) && SD.exists(kAvgFrameCountPath)) {
        File countFile = SD.open(kAvgFrameCountPath, FILE_READ);
        if (countFile) {
            char countBuf[16] = {0};
            size_t n = countFile.readBytes(countBuf, sizeof(countBuf) - 1);
            countFile.close();
            if (n > 0) {
                previousCount = atoi(countBuf);
            }
        }

        if (previousCount > 0) {
            File prevFile = SD.open(kAvgFrameRawPath, FILE_READ);
            if (prevFile && prevFile.size() == frameLen) {
                previousAvg = (uint8_t*)malloc(frameLen);
                if (previousAvg) {
                    size_t readLen = prevFile.read(previousAvg, frameLen);
                    if (readLen == frameLen) {
                        isAppend = true;
                    } else {
                        free(previousAvg);
                        previousAvg = nullptr;
                    }
                }
            }
            if (prevFile) {
                prevFile.close();
            }
        }
    }

    int captured = 0;

    for (int i = 0; i < totalFrames; ++i) {
        uint32_t t0 = millis();

        if (i != 0) {
            frame = CameraGetLatestFrame();
            if (!frame.valid || !frame.fb || frame.fb->format != PIXFORMAT_GRAYSCALE || frame.fb->len != frameLen) {
                CameraRelease(frame);
                free(sum);
                turnOffLED();
                return false;
            }
        }

        // Add this frame into the running sum
        for (size_t p = 0; p < frameLen; ++p) {
            sum[p] += frame.fb->buf[p];
        }
        captured++;

        CameraRelease(frame);

        uint32_t dt = millis() - t0;
        if (dt < frameIntervalMs) {
            delay(frameIntervalMs - dt);
        }
    }

    // Compute average pixel value per position
    uint8_t* avg = (uint8_t*)malloc(frameLen);
    if (!avg) {
        free(previousAvg);
        free(sum);
        turnOffLED();
        return false;
    }

    const int totalCount = captured + (isAppend ? previousCount : 0);
    if (totalCount <= 0) {
        free(previousAvg);
        free(sum);
        free(avg);
        turnOffLED();
        return false;
    }

    for (size_t p = 0; p < frameLen; ++p) {
        if (isAppend && previousAvg) {
            const uint32_t combined = (uint32_t)previousAvg[p] * previousCount + sum[p];
            avg[p] = (uint8_t)(combined / totalCount);
        } else {
            avg[p] = (uint8_t)(sum[p] / captured);
        }
    }
    free(previousAvg);
    free(sum);

    if (!isAppend) {
        BackupIfExists(kAvgFrameRawPath);
        BackupIfExists(kAvgFrameJpgPath);
    }

    SD.remove(kAvgFrameRawPath);
    SD.remove(kAvgFrameJpgPath);

    // Save raw grayscale frame for processing
    File rawFile = SD.open(kAvgFrameRawPath, FILE_WRITE);
    if (!rawFile) {
        free(avg);
        turnOffLED();
        return false;
    }
    rawFile.write(avg, frameLen);
    rawFile.close();

    // Encode averaged grayscale frame to JPEG for viewing
    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;
    bool jpgOk = fmt2jpg(avg, frameLen, width, height, PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);
    free(avg);

    if (jpgOk && jpgBuf && jpgLen > 0) {
        File f = SD.open(kAvgFrameJpgPath, FILE_WRITE);
        if (f) {
            f.write(jpgBuf, jpgLen);
            f.close();
        }
        free(jpgBuf);
    } else if (jpgBuf) {
        free(jpgBuf);
    }

    SD.remove(kAvgFrameCountPath);
    File countFile = SD.open(kAvgFrameCountPath, FILE_WRITE);
    if (countFile) {
        countFile.print(totalCount);
        countFile.close();
    }

    // Expose the current raw path globally for occupancy diffing.
    strncpy(global_averageFrame_path, kAvgFrameRawPath, sizeof(global_averageFrame_path) - 1);
    global_averageFrame_path[sizeof(global_averageFrame_path) - 1] = '\0';

    turnOffLED();
    return true;
}
