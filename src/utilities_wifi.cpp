#include "utilities_wifi.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

// Helper function to convert WiFi status to descriptive text
const char* wifi_status_to_string(wl_status_t status)
{
  switch (status) {
    case WL_IDLE_STATUS:     return "IDLE_STATUS";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

// Helper function to check if network exists and get signal strength
int wifi_scan_for_network(const char* ssid)
{
  Serial.println("Scanning for networks...");
  int networkCount = WiFi.scanNetworks();
  
  if (networkCount == 0) {
    Serial.println("No networks found!");
    return -999; // No networks at all
  }
  
  Serial.printf("Found %d networks:\n", networkCount);
  
  for (int i = 0; i < networkCount; i++) {
    String currentSSID = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    wifi_auth_mode_t authMode = WiFi.encryptionType(i);
    
    Serial.printf("  %d: %s (RSSI: %ddBm, Auth: %d)\n", 
                  i, currentSSID.c_str(), rssi, authMode);
    
    if (currentSSID == String(ssid)) {
      Serial.printf("Target network '%s' found with signal strength: %ddBm\n", ssid, rssi);
      return rssi; // Return signal strength
    }
  }
  
  Serial.printf("Target network '%s' not found in scan results!\n", ssid);
  return -998; // Network not found
}

bool wifi_connect(const String& ssid, const String& user, const String& pass, const char* hostname)
{
  (void)user;

  if (ssid.length() == 0 || pass.length() == 0) {
    Serial.println("WiFi ERROR: SSID or password is null");
    return false;
  }

  Serial.printf("WiFi: Attempting to connect to '%s'...\n", ssid.c_str());

  // First, scan to see if the network is available
  int signalStrength = wifi_scan_for_network(ssid.c_str());
  
  if (signalStrength == -999) {
    Serial.println("WiFi ERROR: No networks detected - check antenna or move closer to router");
    return false;
  }
  
  if (signalStrength == -998) {
    Serial.println("WiFi ERROR: Target network not found - check SSID spelling or network availability");
    return false;
  }
  
  if (signalStrength < -80) {
    Serial.printf("WiFi WARNING: Weak signal (%ddBm) - connection may be unstable\n", signalStrength);
  } else if (signalStrength < -60) {
    Serial.printf("WiFi: Fair signal strength (%ddBm)\n", signalStrength);
  } else {
    Serial.printf("WiFi: Good signal strength (%ddBm)\n", signalStrength);
  }

  // Set WiFi mode and begin connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  
  Serial.println("WiFi: Connection attempt started...");

  const unsigned long start = millis();
  wl_status_t lastStatus = WL_IDLE_STATUS;
  
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    wl_status_t currentStatus = WiFi.status();
    
    // Print status changes for debugging
    if (currentStatus != lastStatus) {
      Serial.printf("WiFi status: %s\n", wifi_status_to_string(currentStatus));
      lastStatus = currentStatus;
    }
    
    delay(250);
  }

  wl_status_t finalStatus = WiFi.status();
  
  if (finalStatus == WL_CONNECTED) {
    Serial.printf("WiFi SUCCESS: Connected to '%s'\n", ssid);
    Serial.printf("  IP address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Signal strength: %ddBm\n", WiFi.RSSI());
    Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
    if (MDNS.begin(hostname)) {
      Serial.printf("  mDNS: http://%s.local\n", hostname);
    } else {
      Serial.println("  mDNS: failed to start");
    }
    return true;
  }

  // Detailed failure analysis
  Serial.printf("WiFi FAILED: Final status = %s\n", wifi_status_to_string(finalStatus));
  
  switch (finalStatus) {
    case WL_NO_SSID_AVAIL:
      Serial.println("  Cause: Network disappeared during connection (moved out of range?)");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("  Cause: Generic connection failure (check router settings or password)");
      break;
    case WL_DISCONNECTED:
      Serial.println("  Cause: Disconnected (timeout or router rejected connection)");
      if (signalStrength < -75) {
        Serial.println("  Likely reason: Signal too weak for stable connection");
      } else {
        Serial.println("  Likely reason: Router may be overloaded or password incorrect");
      }
      break;
    default:
      Serial.printf("  Unknown failure mode - check router logs and WiFi settings\n");
      break;
  }
  
  return false;
}