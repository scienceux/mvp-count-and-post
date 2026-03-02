#include "utilities_debug.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>
#include <SD.h>

extern int g_EntersCount;
extern int g_ExitsCount;

namespace
{
  WebServer g_httpServer(80);
  bool g_serverStarted = false;
  bool g_remoteEnabled = false;

  constexpr size_t kMaxLines = 120;
  String g_logLines[kMaxLines];
  size_t g_logCount = 0;
  size_t g_logIndex = 0;
  String g_currentLine;

  // --- Photo capture ---
  bool g_takePhotoRequested = false;
  constexpr size_t kMaxPhotos = 20;
  String g_photoPaths[kMaxPhotos];
  size_t g_photoCount = 0;

  void append_line(const String& line)
  {
    g_logLines[g_logIndex] = line;
    g_logIndex = (g_logIndex + 1) % kMaxLines;
    if (g_logCount < kMaxLines) {
      ++g_logCount;
    }
  }

  String build_log_text()
  {
    String out;
    out.reserve(4096);

    const size_t start = (g_logCount < kMaxLines) ? 0 : g_logIndex;
    for (size_t i = 0; i < g_logCount; ++i) {
      const size_t idx = (start + i) % kMaxLines;
      out += g_logLines[idx];
      out += "\n";
    }

    if (g_currentLine.length() > 0) {
      out += g_currentLine;
    }

    return out;
  }

  // Returns log lines as HTML divs, newest first.
  String build_log_html()
  {
    String out;
    out.reserve(4096);

    // Current (incomplete) line is the very latest — show it first.
    if (g_currentLine.length() > 0) {
      out += "<div>";
      out += g_currentLine;
      out += "</div>";
    }

    // Iterate completed lines newest-first.
    for (size_t i = 0; i < g_logCount; ++i) {
      const size_t idx = (g_logIndex + kMaxLines - 1 - i) % kMaxLines;
      out += "<div>";
      out += g_logLines[idx];
      out += "</div>";
    }

    return out;
  }

  // Serve a snapped photo from SD by path query param.
  void handle_photo()
  {
    String name = g_httpServer.arg("name");
    if (name.length() == 0 || name.indexOf("..") >= 0) {
      g_httpServer.send(400, "text/plain", "bad name");
      return;
    }
    String path = "/" + name;
    if (!SD.exists(path.c_str())) {
      g_httpServer.send(404, "text/plain", "not found");
      return;
    }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
      g_httpServer.send(500, "text/plain", "open failed");
      return;
    }
    g_httpServer.streamFile(f, "image/jpeg");
    f.close();
  }

  // Button press: flag a capture and redirect back.
  void handle_take_photo()
  {
    g_takePhotoRequested = true;
    g_httpServer.sendHeader("Location", "/", true);
    g_httpServer.send(302, "text/plain", "");
  }

  const char* resolve_image_path(const String& name)
  {
    if (name == "average") return "/avg_frame_current.jpg";
    if (name == "last_frame") return "/last_frame.jpg";
    if (name == "last_diff") return "/last_diff.jpg";
    return nullptr;
  }

  void handle_image()
  {
    String name = g_httpServer.arg("name");
    const char* path = resolve_image_path(name);
    if (!path) {
      g_httpServer.send(400, "text/plain", "missing or invalid image name");
      return;
    }

    if (!SD.exists(path)) {
      g_httpServer.send(404, "text/plain", "image not found");
      return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
      g_httpServer.send(500, "text/plain", "failed to open image");
      return;
    }

    g_httpServer.streamFile(f, "image/jpeg");
    f.close();
  }

  void handle_root()
  {
    String html;
    html.reserve(4096);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='2'>";
    html += "<title>Remote Log</title>";
    html += "<style>body{font-family:monospace;padding:12px;background:#0b0b0b;color:#e7e7e7;}";
    html += ".log-box{height:480px;overflow-y:auto;background:#111;padding:8px 10px;border:1px solid #333;}";
    html += ".log-box div{white-space:pre-wrap;word-break:break-word;line-height:1.4;}";
    html += "img{height:400px;width:auto;display:block;margin:8px 0;border:1px solid #333;}";
    html += ".label{margin-top:14px;font-weight:bold;}</style></head><body>";

    const unsigned long cacheBust = millis();
    // html += "<h3>Diagnostics</h3>";
    // html += "<div class='label'>Average frame</div>";
    // html += "<img src='/image?name=average&t=" + String(cacheBust) + "'>";
    // html += "<div class='label'>Last frame</div>";
    // html += "<img src='/image?name=last_frame&t=" + String(cacheBust) + "'>";
    // html += "<div class='label'>Last diff</div>";
    // html += "<img src='/image?name=last_diff&t=" + String(cacheBust) + "'>";

    html += "<h3>";
    html += "Enters: " + String(g_EntersCount) + " | Exits: " + String(g_ExitsCount) + "</h3>";
    html += "<div class='log-box'>";
    html += build_log_html();
    html += "</div>";

    // Take Photo button
    html += "<form method='POST' action='/take_photo' style='margin:14px 0;'>";
    html += "<button type='submit' style='padding:8px 18px;font-size:1em;cursor:pointer;";
    html += "background:#1a6aff;color:#fff;border:none;border-radius:4px;font-family:monospace;'>";
    html += "Take Photo</button></form>";

    // Photo gallery
    if (g_photoCount > 0) {
      html += "<div class='label'>Snapped photos ("+String(g_photoCount)+")</div>";
      for (size_t i = g_photoCount; i > 0; --i) {
        // Strip leading '/' for the query param (handler re-adds it)
        String name = g_photoPaths[i - 1];
        if (name.startsWith("/")) name = name.substring(1);
        html += "<div class='label' style='font-size:0.85em;color:#aaa;'>" + name + "</div>";
        html += "<img src='/photo?name=" + name + "&t=" + String(cacheBust) + "'>";
      }
    }

    html += "</body></html>";
    g_httpServer.send(200, "text/html", html);
  }

  void handle_log()
  {
    g_httpServer.send(200, "text/plain", build_log_text());
  }

  const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  size_t base64_encode_block(const uint8_t* in, size_t inLen, char* out, size_t outSize)
  {
    const size_t outLen = ((inLen + 2) / 3) * 4;
    if (!out || outSize < outLen + 1) return 0;

    size_t i = 0;
    size_t o = 0;
    while (i < inLen) {
      uint32_t triple = 0;
      size_t remaining = inLen - i;
      triple |= (uint32_t)in[i++] << 16;
      if (remaining > 1) {
        triple |= (uint32_t)in[i++] << 8;
      }
      if (remaining > 2) {
        triple |= (uint32_t)in[i++];
      }

      out[o++] = kBase64Table[(triple >> 18) & 0x3F];
      out[o++] = kBase64Table[(triple >> 12) & 0x3F];
      out[o++] = (remaining > 1) ? kBase64Table[(triple >> 6) & 0x3F] : '=';
      out[o++] = (remaining > 2) ? kBase64Table[triple & 0x3F] : '=';
    }

    out[o] = '\0';
    return o;
  }
}

void turn_on_remote_serial_monitoring()
{
  if (g_serverStarted) return;

  g_httpServer.on("/", handle_root);
  g_httpServer.on("/log", handle_log);
  g_httpServer.on("/image", handle_image);
  g_httpServer.on("/photo", handle_photo);
  g_httpServer.on("/take_photo", HTTP_POST, handle_take_photo);
  g_httpServer.begin();
  g_serverStarted = true;

  Serial.print("Remote log HTTP server started at http://");
  Serial.print(WiFi.localIP());
  Serial.println(":80");
}

void remote_serial_poll()
{
  if (!g_serverStarted) return;
  g_httpServer.handleClient();
}

void remote_serial_write(const char* text)
{
  if (!text) return;
  g_currentLine += text;
}

void remote_serial_println(const char* text)
{
  if (!text) return;
  g_currentLine += text;
  append_line(g_currentLine);
  g_currentLine = String();
}

void enable_remote_serial(bool enabled)
{
  g_remoteEnabled = enabled;
}

// Returns true once if a photo was requested via the web UI.
// outPath receives the suggested SD path, e.g. "/snap_003.jpg".
bool remote_take_photo_pending(char* outPath, size_t pathSize)
{
  if (!g_takePhotoRequested) return false;
  g_takePhotoRequested = false;
  if (outPath && pathSize > 0) {
    snprintf(outPath, pathSize, "/snap_%03u.jpg", (unsigned)g_photoCount);
  }
  return true;
}

// Call after successfully saving a photo to register it in the gallery.
void remote_register_photo(const char* path)
{
  if (!path) return;
  if (g_photoCount < kMaxPhotos) {
    g_photoPaths[g_photoCount++] = String(path);
  } else {
    // Overwrite oldest (ring)
    g_photoPaths[g_photoCount % kMaxPhotos] = String(path);
    g_photoCount++;
  }
}

// Logging helpers (always prints a newline)
void log_print(const char* text)
{
  if (!text) return;
  Serial.println(text);
  if (g_remoteEnabled) {
    remote_serial_println(text);
  }
}

bool log_print_jpeg_file(const char* label, const char* path)
{
  if (!label || !path) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) {
    char msg[160];
    snprintf(msg, sizeof(msg), "IMG_JPEG_ERROR %s open_failed %s", label, path);
    log_print(msg);
    return false;
  }

  char beginLine[160];
  snprintf(beginLine, sizeof(beginLine), "IMG_JPEG_BEGIN %s %lu", label, (unsigned long)f.size());
  log_print(beginLine);

  uint8_t inBuf[48];
  char outBuf[80];

  while (true) {
    size_t n = f.read(inBuf, sizeof(inBuf));
    if (n == 0) break;

    size_t outLen = base64_encode_block(inBuf, n, outBuf, sizeof(outBuf));
    if (outLen == 0) {
      log_print("IMG_JPEG_ERROR base64_encode_failed");
      break;
    }

    char line[128];
    snprintf(line, sizeof(line), "IMG_JPEG %s", outBuf);
    log_print(line);
  }

  f.close();

  char endLine[96];
  snprintf(endLine, sizeof(endLine), "IMG_JPEG_END %s", label);
  log_print(endLine);
  return true;
}

// Numeric overloads (always println)
void log_print(int value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_print(unsigned int value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_print(long value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_print(unsigned long value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_print(float value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_print(double value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_print(const String& value) { log_print(value.c_str()); }

void log_println(int value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_println(unsigned int value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_println(long value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_println(unsigned long value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_println(float value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
void log_println(double value) { Serial.println(value); if (g_remoteEnabled) { remote_serial_println(String(value).c_str()); } }
