#include "utilities_wifi.h"
#include <Arduino.h>
#include <WiFi.h>

bool wifi_connect(const char* ssid, const char* user, const char* pass)
{
  (void)user;

  if (!ssid || !pass) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WiFi connect failed.");
  return false;
}