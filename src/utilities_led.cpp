#include "utilities_led.h"
#include <string.h>

// -----------------------------
// NEW: persistent state (file-scope) so we can reset between modes
// -----------------------------
static int blinkCount = 0;
static bool ledState = false;
static unsigned long lastTime = 0;
static bool sequenceActive = true;
static int currentDelay = 1000;
static bool delaySet = false;
static bool isSOSMode = false;
static int sosStep = 0;
static char lastMode[16] = "";   // tracks last fastOrSlow string

// NEW: reset helper (declare in utilities_led.h too)
void blinkLEDReset() {
  blinkCount = 0;
  ledState = false;
  lastTime = 0;
  sequenceActive = true;
  currentDelay = 1000;
  delaySet = false;
  isSOSMode = false;
  sosStep = 0;
  lastMode[0] = '\0';
  turnOffLED();
}

// Function definitions
bool setupLED() {
  // Set LED pin as output
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.print("LED initialized on pin: ");
  Serial.println(LED_BUILTIN);

  return true;
}

// High/low are backwards on the Seeed XIAO ESP32-S3. Low is on.
void turnOnLED() {
  digitalWrite(LED_BUILTIN, LOW);
  // Serial.println("LED ON");
}

void turnOffLED() {
  digitalWrite(LED_BUILTIN, HIGH);
  // Serial.println("LED OFF");
}

int ConvertFastSlowToMilliseconds(const char* fastOrSlow) {
  if (strcmp(fastOrSlow, "fast") == 0) {
    return 200;  // Fast blink - 200ms
  } else if (strcmp(fastOrSlow, "slow") == 0) {
    return 2000; // Slow blink - 2 seconds
  } else if (strcmp(fastOrSlow, "SOS") == 0) {
    return 200;  // Base unit for SOS timing
  } else {
    return 1000; // Default - 1 second
  }
}

void blinkLED(int howManyTimes, const char* fastOrSlow) {

  // -----------------------------
  // NEW: if mode changes (fast -> SOS, etc.), reset state machine
  // -----------------------------
  if (fastOrSlow == nullptr) fastOrSlow = "";
  if (strcmp(lastMode, fastOrSlow) != 0) {
    // reset everything except lastMode, then store new mode
    blinkCount = 0;
    ledState = false;
    lastTime = 0;
    sequenceActive = true;
    currentDelay = 1000;
    delaySet = false;
    isSOSMode = false;
    sosStep = 0;

    strncpy(lastMode, fastOrSlow, sizeof(lastMode) - 1);
    lastMode[sizeof(lastMode) - 1] = '\0';
    turnOffLED();
  }

  // Set delay and mode once at the start (per mode)
  if (!delaySet) {
    currentDelay = ConvertFastSlowToMilliseconds(fastOrSlow);
    isSOSMode = (strcmp(fastOrSlow, "SOS") == 0);
    delaySet = true;
  }

  // SOS Mode handling
  if (isSOSMode) {
    if (millis() - lastTime >= (unsigned long)currentDelay) {
      // SOS pattern: 3 short, 3 long, 3 short, pause
      // Steps: 0-5=dots, 6-11=dashes, 12-17=dots, 18=long pause

      if (sosStep < 6) {
        // First 3 dots (short blinks)
        if (sosStep % 2 == 0) {
          turnOnLED();
          currentDelay = 200; // Short on
        } else {
          turnOffLED();
          currentDelay = 200; // Short off
        }
      } else if (sosStep < 12) {
        // 3 dashes (long blinks)
        if (sosStep % 2 == 0) {
          turnOnLED();
          currentDelay = 600; // Long on
        } else {
          turnOffLED();
          currentDelay = 200; // Short off
        }
      } else if (sosStep < 18) {
        // Last 3 dots (short blinks)
        if (sosStep % 2 == 0) {
          turnOnLED();
          currentDelay = 200; // Short on
        } else {
          turnOffLED();
          currentDelay = 200; // Short off
        }
      } else {
        // Long pause between SOS sequences
        turnOffLED();
        currentDelay = 1400; // Long pause
        sosStep = -1; // Reset to start over
      }

      sosStep++;
      lastTime = millis();
    }
    return; // Exit early for SOS mode
  }

  // Regular blinking mode
  // If howManyTimes is 0 (or default), blink indefinitely
  if (howManyTimes > 0) {
    // Check if we've completed all blinks (each blink = on + off = 2 state changes)
    if (blinkCount >= howManyTimes * 2) {
      if (sequenceActive) {
        turnOffLED();  // Ensure LED is off when sequence ends
        Serial.println("Blink sequence complete!");
        sequenceActive = false;
      }
      return;  // Stop blinking
    }
  }

  // Non-blocking timing check
  if (millis() - lastTime >= (unsigned long)currentDelay) {
    if (ledState) {
      turnOffLED();
    } else {
      turnOnLED();
    }

    ledState = !ledState;
    blinkCount++;
    lastTime = millis();
  }
}
