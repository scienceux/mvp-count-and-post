#include "count_occupancy_in_frame.h"
#include <Arduino.h>
#include <SD.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include "img_converters.h"

namespace {
// Any pixel that changes this much (0-255) is considered "different" from the average
const uint8_t kDiffThreshold = 20;
// Rough mapping from changed pixels to people (tuned for QQVGA; adjust later)
const uint32_t kPixelsPerPerson = 900;
const char* kLastDiffJpgPath = "/last_diff.jpg";

const char* kCsvHeader =
	"timestamp,epoch_valid,uptime_ms,width,height,mean_diff,mean_abs_diff,stddev_abs_diff,"
	"min_abs_diff,max_abs_diff,changed_count,changed_percent,occupancy";

// Make sure the CSV exists and has the correct header.
// If the existing file is not the right format, create a new one with a random suffix.
bool EnsureCsvReady(char* outPath, size_t outSize)
{
	if (!SD.exists(outPath)) {
		File f = SD.open(outPath, FILE_WRITE);
		if (!f) {
			return false;
		}
		f.println(kCsvHeader);
		f.close();
		return true;
	}

	File f = SD.open(outPath, FILE_READ);
	if (!f) {
		return false;
	}

	String firstLine = f.readStringUntil('\n');
	f.close();

	firstLine.trim();
	if (firstLine == kCsvHeader) {
		return true;
	}

	static bool seeded = false;
	if (!seeded) {
		randomSeed(micros());
		seeded = true;
	}

	unsigned long postfix = random(100000UL, 999999UL);
	snprintf(outPath, outSize, "/occupancy-%lu.csv", postfix);

	File nf = SD.open(outPath, FILE_WRITE);
	if (!nf) {
		return false;
	}
	nf.println(kCsvHeader);
	nf.close();
	return true;
}

// Load the saved average frame (JPEG) and convert it to a grayscale buffer.
// The grayscale buffer is used as the "background" reference for comparison.
bool LoadAverageFrameGray(const char* averageFramePath,
						  uint16_t width,
						  uint16_t height,
						  uint8_t** outGray)
{
	if (!averageFramePath || averageFramePath[0] == '\0') {
		return false;
	}

	const char* ext = strrchr(averageFramePath, '.');
	if (ext && strcmp(ext, ".raw") == 0) {
		File rf = SD.open(averageFramePath, FILE_READ);
		if (!rf) {
			return false;
		}

		size_t expected = (size_t)width * (size_t)height;
		if (rf.size() != expected) {
			rf.close();
			return false;
		}

		uint8_t* gray = (uint8_t*)malloc(expected);
		if (!gray) {
			rf.close();
			return false;
		}

		size_t readLen = rf.read(gray, expected);
		rf.close();
		if (readLen != expected) {
			free(gray);
			return false;
		}

		*outGray = gray;
		return true;
	}

	File f = SD.open(averageFramePath, FILE_READ);
	if (!f) {
		return false;
	}

	size_t len = f.size();
	if (len == 0) {
		f.close();
		return false;
	}

	uint8_t* jpgBuf = (uint8_t*)malloc(len);
	if (!jpgBuf) {
		f.close();
		return false;
	}

	size_t readLen = f.read(jpgBuf, len);
	f.close();
	if (readLen != len) {
		free(jpgBuf);
		return false;
	}

	size_t rgb565Len = (size_t)width * (size_t)height * 2;
	uint8_t* rgb565 = (uint8_t*)malloc(rgb565Len);
	if (!rgb565) {
		free(jpgBuf);
		return false;
	}

	bool ok = jpg2rgb565(jpgBuf, len, rgb565, JPG_SCALE_NONE);
	free(jpgBuf);
	if (!ok) {
		free(rgb565);
		return false;
	}

	uint8_t* gray = (uint8_t*)malloc((size_t)width * (size_t)height);
	if (!gray) {
		free(rgb565);
		return false;
	}

	for (size_t i = 0, j = 0; i < rgb565Len; i += 2, ++j) {
		uint16_t px = (uint16_t)rgb565[i] | ((uint16_t)rgb565[i + 1] << 8);
		uint8_t r5 = (uint8_t)((px >> 11) & 0x1F);
		uint8_t g6 = (uint8_t)((px >> 5) & 0x3F);
		uint8_t b5 = (uint8_t)(px & 0x1F);
		uint8_t r = (uint8_t)((r5 * 255) / 31);
		uint8_t g = (uint8_t)((g6 * 255) / 63);
		uint8_t b = (uint8_t)((b5 * 255) / 31);
		gray[j] = (uint8_t)((uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11) / 100;
	}

	free(rgb565);
	*outGray = gray;
	return true;
}
} // namespace

// Compare the current frame to the average frame and estimate how many people are present.
// The result and supporting stats are appended to the CSV for later analysis.
int CountOccupancyInFrame(const Frame& frame, const char* averageFramePath)
{
	if (!frame.valid || !frame.fb || frame.fb->format != PIXFORMAT_GRAYSCALE) {
		return 0;
	}

	if (!EnsureCsvReady(global_csv_path, sizeof(global_csv_path))) {
		return 0;
	}

	// Load the reference "empty" scene so we can see what's changed
	uint8_t* avgGray = nullptr;
	if (!LoadAverageFrameGray(averageFramePath, frame.fb->width, frame.fb->height, &avgGray)) {
		return 0;
	}

	const size_t pixelCount = (size_t)frame.fb->width * (size_t)frame.fb->height;
	const uint8_t* cur = frame.fb->buf;

	uint8_t* diffGray = (uint8_t*)malloc(pixelCount);

	int minAbs = 255;
	int maxAbs = 0;
	int64_t sumDiff = 0;
	int64_t sumAbs = 0;
	uint64_t sumAbsSq = 0;
	uint32_t changedCount = 0;

	// Walk every pixel and measure how different it is from the average image
	for (size_t i = 0; i < pixelCount; ++i) {
		int diff = (int)cur[i] - (int)avgGray[i];
		int absDiff = diff >= 0 ? diff : -diff;

		if (diffGray) {
			diffGray[i] = (uint8_t)absDiff;
		}

		sumDiff += diff;
		sumAbs += absDiff;
		sumAbsSq += (uint64_t)absDiff * (uint64_t)absDiff;

		if (absDiff < minAbs) minAbs = absDiff;
		if (absDiff > maxAbs) maxAbs = absDiff;
		if (absDiff >= kDiffThreshold) {
			changedCount++;
		}
	}

	free(avgGray);

	if (diffGray) {
		uint8_t* jpgBuf = nullptr;
		size_t jpgLen = 0;
		bool jpgOk = fmt2jpg(diffGray, pixelCount, frame.fb->width, frame.fb->height,
							PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpgLen);
		if (jpgOk && jpgBuf && jpgLen > 0) {
			SD.remove(kLastDiffJpgPath);
			File f = SD.open(kLastDiffJpgPath, FILE_WRITE);
			if (f) {
				f.write(jpgBuf, jpgLen);
				f.close();
			}
			free(jpgBuf);
		} else if (jpgBuf) {
			free(jpgBuf);
		}
		free(diffGray);
	}

	// Summarize the differences to produce simple, human-readable stats
	float meanDiff = (float)sumDiff / (float)pixelCount;
	float meanAbs = (float)sumAbs / (float)pixelCount;
	float meanAbsSq = (float)sumAbsSq / (float)pixelCount;
	float varianceAbs = meanAbsSq - (meanAbs * meanAbs);
	if (varianceAbs < 0.0f) varianceAbs = 0.0f;
	float stddevAbs = sqrtf(varianceAbs);

	float changedPercent = (pixelCount > 0)
		? (100.0f * (float)changedCount / (float)pixelCount)
		: 0.0f;

	// Convert changed pixels to an approximate number of people
	uint32_t occupancy = (uint32_t)((changedCount + (kPixelsPerPerson / 2)) / kPixelsPerPerson);

	// Build a timestamp for the CSV (RTC time if available, else uptime)
	time_t now = time(nullptr);
	struct tm tmNow;
	localtime_r(&now, &tmNow);
	bool epochValid = (tmNow.tm_year >= (2020 - 1900));

	char timestamp[32];
	if (epochValid) {
		snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
				 tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
				 tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
	} else {
		snprintf(timestamp, sizeof(timestamp), "uptime:%lu", (unsigned long)millis());
	}

	File out = SD.open(global_csv_path, FILE_APPEND);
	if (out) {
		out.print(timestamp);
		out.print(',');
		out.print(epochValid ? 1 : 0);
		out.print(',');
		out.print((unsigned long)millis());
		out.print(',');
		out.print(frame.fb->width);
		out.print(',');
		out.print(frame.fb->height);
		out.print(',');
		out.print(meanDiff, 3);
		out.print(',');
		out.print(meanAbs, 3);
		out.print(',');
		out.print(stddevAbs, 3);
		out.print(',');
		out.print(minAbs);
		out.print(',');
		out.print(maxAbs);
		out.print(',');
		out.print(changedCount);
		out.print(',');
		out.print(changedPercent, 3);
		out.print(',');
		out.println(occupancy);
		out.close();
	}

	return (int)occupancy;
}
