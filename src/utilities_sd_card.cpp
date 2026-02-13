#include <SD.h>
#include <SPI.h>
#include <Arduino.h>

// SD card pin definitions for Seeed XIAO ESP32-S3 (from your image)
#define SD_CS_PIN 21     // CS
#define SD_SCK_PIN 7     // SCK
#define SD_MISO_PIN 8    // MISO
#define SD_MOSI_PIN 9    // MOSI

bool setupSDCard() {
    // Initialize SPI with custom pins
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    // Initialize SD card
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card setup failed!");
        return false;
    }
    
    // Get card type
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SD card setup failed!");
        return false;
    }
    
    // Test write functionality
    File file = SD.open("/test.txt", FILE_WRITE);
    if (!file) {
        Serial.println("SD card setup failed!");
        return false;
    }
    
    file.println("SD card test successful!");
    file.printf("Timestamp: %lu\n", millis());
    file.close();
    
    // Test read functionality
    file = SD.open("/test.txt");
    if (!file) {
        Serial.println("SD card setup failed!");
        return false;
    }
    
    while (file.available()) {
        file.read(); // Read but don't print
    }
    file.close();
    
    Serial.println("SD card setup successful!");
    return true;
}