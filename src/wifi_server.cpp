#include "wifi_server.h"

#include "wifi_session.h"

#include <SD.h>
#include <WebServer.h>
#include <cstring>

namespace {

WebServer* gServer = nullptr;
File gUpload;
char gUploadPath[192] = {};
char gLastMsg[96] = {};
bool gUploadOk = false;
const char* gUploadDir = "/firmware";

constexpr char kIndexHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Basilauncher</title>
<style>
body{font-family:system-ui,sans-serif;max-width:28rem;margin:1.5rem auto;padding:0 1rem;line-height:1.4}
h1{font-size:1.25rem;margin:0 0 .5rem}
.card{border:1px solid #ccc;border-radius:10px;padding:1rem;margin:1rem 0}
label{display:block;margin:.5rem 0 .2rem;font-size:.9rem}
input,select,button{font:inherit;padding:.45rem .6rem;width:100%;box-sizing:border-box}
button{margin-top:.75rem;background:#2c5f4a;color:#fff;border:0;border-radius:8px}
.muted{color:#666;font-size:.9rem}
</style>
</head>
<body>
<h1>Basilauncher</h1>
<p class="muted">Upload firmware bins or sleep images to the SD card.</p>
<div class="card">
<form id="up" method="POST" action="/upload/firmware" enctype="multipart/form-data">
<label>Destination</label>
<select id="dest" onchange="document.getElementById('up').action='/upload/'+this.value">
<option value="firmware">/firmware (apps)</option>
<option value="sleep">/sleep (screensaver BMPs)</option>
</select>
<label>File</label>
<input type="file" name="file" required/>
<button type="submit">Upload</button>
</form>
</div>
<div class="card">
<form method="POST" action="/wifi">
<label>Home Wi‑Fi SSID (optional)</label>
<input name="ssid" maxlength="32" placeholder="MyNetwork"/>
<label>Password</label>
<input name="pass" type="password" maxlength="64" placeholder="••••••••"/>
<button type="submit">Save network</button>
</form>
<p class="muted">Saved credentials let the device join your LAN later for NTP and transfers.</p>
</div>
</body>
</html>)HTML";

void ensureDir(const char* dir) {
  if (!SD.exists(dir)) SD.mkdir(dir);
}

const char* leafName(const char* name) {
  if (!name) return "upload.bin";
  const char* slash = strrchr(name, '/');
  const char* base = slash ? slash + 1 : name;
  const char* bslash = strrchr(base, '\\');
  base = bslash ? bslash + 1 : base;
  return base[0] ? base : "upload.bin";
}

void handleRoot() {
  gServer->sendHeader("Cache-Control", "no-store");
  gServer->send(200, "text/html", kIndexHtml);
}

void handleWifiSave() {
  const String ssid = gServer->arg("ssid");
  const String pass = gServer->arg("pass");
  if (ssid.length() == 0) {
    gServer->send(400, "text/plain", "SSID required");
    return;
  }
  wifiSaveNetwork(ssid.c_str(), pass.c_str());
  snprintf(gLastMsg, sizeof(gLastMsg), "Saved Wi‑Fi: %s", ssid.c_str());
  gServer->send(200, "text/plain", "Saved. You can join this network from the device.");
}

void handleUpload() {
  HTTPUpload& upload = gServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    gUploadOk = false;
    gUploadPath[0] = 0;
    ensureDir(gUploadDir);
    const char* leaf = leafName(upload.filename.c_str());
    if (strchr(leaf, '/') || strchr(leaf, '\\') || strstr(leaf, "..")) {
      snprintf(gLastMsg, sizeof(gLastMsg), "Bad filename");
      return;
    }
    snprintf(gUploadPath, sizeof(gUploadPath), "%s/%s", gUploadDir, leaf);
    if (SD.exists(gUploadPath)) SD.remove(gUploadPath);
    gUpload = SD.open(gUploadPath, FILE_WRITE);
    if (!gUpload) {
      snprintf(gLastMsg, sizeof(gLastMsg), "SD open failed");
      gUploadPath[0] = 0;
      return;
    }
    Serial.printf("WiFi upload start: %s\n", gUploadPath);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (gUpload) {
      const size_t w = gUpload.write(upload.buf, upload.currentSize);
      if (w != upload.currentSize) {
        gUpload.close();
        if (gUploadPath[0]) SD.remove(gUploadPath);
        gUploadPath[0] = 0;
        snprintf(gLastMsg, sizeof(gLastMsg), "Write failed");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (gUpload) {
      gUpload.close();
      gUploadOk = gUploadPath[0] != 0;
      if (gUploadOk) {
        snprintf(gLastMsg, sizeof(gLastMsg), "Saved %s", gUploadPath);
        Serial.printf("WiFi upload done: %s (%u bytes)\n", gUploadPath,
                      static_cast<unsigned>(upload.totalSize));
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (gUpload) gUpload.close();
    if (gUploadPath[0]) SD.remove(gUploadPath);
    gUploadPath[0] = 0;
    snprintf(gLastMsg, sizeof(gLastMsg), "Upload aborted");
  }
}

void handleUploadDone() {
  if (gUploadOk) gServer->send(200, "text/plain", gLastMsg);
  else gServer->send(500, "text/plain", gLastMsg[0] ? gLastMsg : "Upload failed");
}

void uploadFirmware() {
  gUploadDir = "/firmware";
  handleUpload();
}
void uploadSleep() {
  gUploadDir = "/sleep";
  handleUpload();
}

void handleNotFound() {
  handleRoot();
}

}  // namespace

bool wifiServerStart() {
  wifiServerStop();
  gServer = new WebServer(80);
  if (!gServer) return false;
  gServer->on("/", HTTP_GET, handleRoot);
  gServer->on("/upload/firmware", HTTP_POST, handleUploadDone, uploadFirmware);
  gServer->on("/upload/sleep", HTTP_POST, handleUploadDone, uploadSleep);
  gServer->on("/upload", HTTP_POST, handleUploadDone, uploadFirmware);
  gServer->on("/wifi", HTTP_POST, handleWifiSave);
  gServer->onNotFound(handleNotFound);
  gServer->begin();
  Serial.println("WiFi: web server on :80");
  return true;
}

void wifiServerStop() {
  if (!gServer) return;
  gServer->stop();
  delete gServer;
  gServer = nullptr;
  if (gUpload) gUpload.close();
}

void wifiServerPoll() {
  if (gServer) gServer->handleClient();
}

bool wifiServerRunning() {
  return gServer != nullptr;
}

const char* wifiServerLastMessage() {
  return gLastMsg;
}

void wifiServerClearMessage() {
  gLastMsg[0] = 0;
}
