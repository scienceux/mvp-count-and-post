#include "utilities_debug.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>
#include <SD.h>
#include "utilities_camera.h"
#include "count_occupancy_in_frame.h"

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

  // Stream raw grayscale bytes of the current live frame (FW*FH = 307200 bytes)
  void get_raw_frame_pixels()
  {
    Frame f = CameraGetCopyOfLatestFrame();
    if (!f.valid || !f.copyOfbufferInMemory) {
      g_httpServer.send(503, "text/plain", "no frame available");
      return;
    }
    const size_t len = (size_t)FW * FH;
    g_httpServer.setContentLength(len);
    g_httpServer.send(200, "application/octet-stream", "");
    WiFiClient client = g_httpServer.client();
    const uint8_t* p = (const uint8_t*)f.copyOfbufferInMemory;
    size_t remaining = len;
    const size_t kChunk = 4096;
    while (remaining > 0) {
      const size_t n = remaining < kChunk ? remaining : kChunk;
      client.write(p, n);
      p         += n;
      remaining -= n;
    }
    free(f.copyOfbufferInMemory);
  }

  // Stream raw grayscale bytes of the PSRAM average frame (no free — it's the master copy)
  void get_average_frame_pixels()
  {
    const uint8_t* avg = CameraGetAverageFrame();
    if (!avg) {
      g_httpServer.send(503, "text/plain", "no average frame");
      return;
    }
    const size_t len = (size_t)FW * FH;
    g_httpServer.setContentLength(len);
    g_httpServer.send(200, "application/octet-stream", "");
    WiFiClient client = g_httpServer.client();
    const uint8_t* p = avg;
    size_t remaining = len;
    const size_t kChunk = 4096;
    while (remaining > 0) {
      const size_t n = remaining < kChunk ? remaining : kChunk;
      client.write(p, n);
      p         += n;
      remaining -= n;
    }
  }

  // Trigger a full average frame recompute. Blocks for numSeconds while sampling.
  void handle_recompute_avg()
  {
    String secStr = g_httpServer.arg("seconds");
    int seconds = secStr.length() > 0 ? secStr.toInt() : 10;
    if (seconds < 1)  seconds = 1;
    if (seconds > 60) seconds = 60;
    AverageFrameCreate(seconds);
    g_httpServer.send(200, "text/plain", "ok");
  }

  void handle_avg_frame_sd()
  {
    const char* path = "/average-frames/last_average_frame.jpg";
    if (!SD.exists(path)) { g_httpServer.send(404, "text/plain", "not found"); return; }
    File f = SD.open(path, FILE_READ);
    if (!f) { g_httpServer.send(500, "text/plain", "open failed"); return; }
    g_httpServer.streamFile(f, "image/jpeg");
    f.close();
  }

  void handle_grid_diff()
  {
    gridDiff currentDiff = DivideFrameIntoGridAndDiff();

    String json;
    json.reserve(512);
    json += "{";
    json += "\"valid\":";
    json += currentDiff.valid ? "true" : "false";
    json += ",\"cols\":";
    json += String(GRID_DIFF_NUM_COLS);
    json += ",\"rows\":";
    json += String(GRID_DIFF_NUM_ROWS);
    json += ",\"averageQuadrantDiff\":";
    json += String(currentDiff.averageQuadrantDiff);
    json += ",\"quadrantDiff\":[";

    for (uint8_t q = 0; q < GRID_DIFF_NUM_QUADRANTS; q++) {
      if (q > 0) {
        json += ",";
      }
      json += String(currentDiff.quadrantDiff[q]);
    }

    json += "]}";
    g_httpServer.send(200, "application/json", json);
  }

  void handle_grid_diff_image()
  {
    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;

    if (!BuildGridDiffDebugImageJpg(&jpgBuf, &jpgLen) || !jpgBuf || jpgLen == 0) {
      g_httpServer.send(500, "text/plain", "failed to build grid diff image");
      return;
    }

    g_httpServer.setContentLength(jpgLen);
    g_httpServer.send(200, "image/jpeg", "");
    g_httpServer.client().write(jpgBuf, jpgLen);

    free(jpgBuf);
  }

  void serve_configure_threshold_page()
  {
    static const char kPage[] = R"HTML(
<!doctype html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Configure Threshold</title>
<style>
body{font-family:monospace;padding:12px;background:#0b0b0b;color:#e7e7e7;}
canvas{display:block;max-width:100%;border:1px solid #333;margin:6px 0;}
button{padding:8px 18px;font-size:1em;cursor:pointer;background:#1a6aff;color:#fff;
  border:none;border-radius:4px;font-family:monospace;margin:6px 0;}
a{color:#1a6aff;}
.row{display:flex;align-items:center;gap:12px;margin:10px 0;flex-wrap:wrap;}
.section-label{font-weight:bold;margin-top:18px;margin-bottom:2px;color:#ccc;}
.key{display:flex;gap:18px;align-items:center;margin:8px 0;flex-wrap:wrap;}
.key-item{display:flex;align-items:center;gap:7px;}
.swatch{width:22px;height:22px;border-radius:3px;flex-shrink:0;}
</style>
</head><body>
<div><a href='/'>&#8592; Back</a></div>
<h2>Configure Threshold</h2>

<div style='display:flex;gap:10px;flex-wrap:wrap;align-items:center;'>
<button id='captureBtn'>Capture Frame</button>
<button id='avgBtn' style='background:#7a4a00;'>Recompute Average (10s)</button>
</div>
<div id='status' style='color:#aaa;margin:6px 0;'>Press Capture to load a frame.</div>

<div class='section-label'>Diff vs Average Frame</div>
<canvas id='cv' width='640' height='480'></canvas>

<div class='row'>
  <label>Spike threshold (per-quadrant diff sum):</label>
  <input type='range' id='sl' min='0' max='500000' step='1000' value='200000' style='flex:1;min-width:160px;'>
  <span id='sv' style='min-width:64px;text-align:right;'>200000</span>
</div>

<div class='key'>
  <div class='key-item'>
    <div class='swatch' style='background:rgba(255,60,60,0.6);border:2px solid #ff4444;'></div>
    <span>Quadrant diff sum &gt; threshold &mdash; <strong>spiking</strong></span>
  </div>
  <div class='key-item'>
    <div class='swatch' style='background:transparent;border:2px solid #44aaff;'></div>
    <span>Quadrant diff sum &le; threshold &mdash; <strong>quiet</strong></span>
  </div>
  <div class='key-item' style='color:#aaa;font-size:0.9em;'>Numbers = processed diff sum per quadrant</div>
</div>

<div style='color:#aaa;font-size:0.9em;margin:8px 0 2px;'>Grid matches detector layout: 6 columns × 5 rows = 30 quadrants.</div>

<div class='section-label'>Average Frame (baseline)</div>
<img id='avgimg' style='max-width:100%;display:block;border:1px solid #333;margin:6px 0;'>

<script>
const FW=640,FH=480;
let NC=6,NR=5,QW=FW/NC,QH=FH/NR;
let qd=null;
const cv=document.getElementById('cv');
const ctx=cv.getContext('2d');
const sl=document.getElementById('sl');
document.getElementById('avgimg').src='/avg_frame_sd?t='+Date.now();
const sv=document.getElementById('sv');
const st=document.getElementById('status');
const diffImg=new Image();
diffImg.onload=()=>{ if(qd) draw(); };
function updateGridSize(cols,rows){
  NC=cols;
  NR=rows;
  QW=FW/NC;
  QH=FH/NR;
}
document.getElementById('avgBtn').addEventListener('click',async()=>{
  st.textContent='Recomputing average frame (10s)... please wait.';
  document.getElementById('avgBtn').disabled=true;
  try{
    const r=await fetch('/recompute_avg?seconds=10');
    st.textContent=r.ok?'Done. Press Capture to see updated diff.':'Recompute failed: '+r.status;
    if(r.ok) document.getElementById('avgimg').src='/avg_frame_sd?t='+Date.now();
  }catch(e){st.textContent='Recompute error: '+e.message;}
  document.getElementById('avgBtn').disabled=false;
});
document.getElementById('captureBtn').addEventListener('click',async()=>{
  st.textContent='Fetching frames...';
  try{
    const t=Date.now();
    const[gr]=await Promise.all([
      fetch('/grid_diff?t='+t)
    ]);
    if(!gr.ok) throw new Error('HTTP '+gr.status);
    const gd=await gr.json();
    if(!gd.valid) throw new Error('grid diff not available');
    updateGridSize(gd.cols,gd.rows);
    qd=gd.quadrantDiff;
    diffImg.src='/grid_diff_image.jpg?t='+t;
    st.textContent='Captured \u2014 drag slider to adjust threshold. Avg diff removed: '+gd.averageQuadrantDiff;
  }catch(e){st.textContent='Error: '+e.message;}
});
sl.addEventListener('input',()=>{sv.textContent=sl.value;if(qd)draw();});
function draw(){
  const thr=+sl.value;
  ctx.clearRect(0,0,FW,FH);
  ctx.drawImage(diffImg,0,0,FW,FH);
  // Overlay quadrant grid
  for(let q=0;q<NC*NR;q++){
    const col=q%NC,row=q/NC|0;
    const x=col*QW,y=row*QH;
    const spike=qd[q]>thr;
    ctx.strokeStyle=spike?'#ff4444':'#44aaff';
    ctx.lineWidth=2;
    ctx.strokeRect(x+1,y+1,QW-2,QH-2);
    ctx.textAlign='center';
    ctx.fillStyle=spike?'#ffcccc':'#aaddff';
    ctx.font='bold 13px monospace';
    ctx.fillText(qd[q].toLocaleString(),x+QW/2,y+QH/2+5);
  }
}
</script>
</body></html>
)HTML";
    g_httpServer.send(200, "text/html", kPage);
  }

  void handle_root()
  {
    String html;
    html.reserve(4096);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    // html += "<meta http-equiv='refresh' content='2'>";  // uncomment to auto-refresh every 2 seconds
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

    // Configure Threshold button
    html += "<div style='margin:8px 0;'><a href='/configure' style='padding:8px 18px;font-size:1em;";
    html += "background:#2a7a2a;color:#fff;border-radius:4px;text-decoration:none;display:inline-block;";
    html += "font-family:monospace;'>Configure Threshold</a></div>";

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
  g_httpServer.on("/configure", serve_configure_threshold_page);
  g_httpServer.on("/avg_frame_sd", handle_avg_frame_sd);
  g_httpServer.on("/recompute_avg", handle_recompute_avg);
  g_httpServer.on("/grid_diff", handle_grid_diff);
  g_httpServer.on("/grid_diff_image.jpg", handle_grid_diff_image);
  g_httpServer.on("/raw_frame", get_raw_frame_pixels);
  g_httpServer.on("/avg_frame_raw", get_average_frame_pixels);
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
