#ifndef UTILITIES_LED_H
#define UTILITIES_LED_H

#include <Arduino.h>

// Function declarations
bool setupLED();
void turnOnLED();
void turnOffLED();
void blinkLED(int howManyTimes = 0, const char* fastOrSlow = "normal");
void blinkLEDReset();
int ConvertFastSlowToMilliseconds(const char* fastOrSlow);

#endif