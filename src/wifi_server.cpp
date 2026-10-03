#include "wifi_server.h"

#include "wifi_session.h"

#include <SD.h>
#include <WebServer.h>
#include <cstring>

namespace {

WebServer* gServer = nullptr;
File gUpload;
char gUploadPath[192] = {};
char gUploadDir[160] = "/";
char gLastMsg[96] = {};
bool gUploadOk = false;
bool gRestartAp = false;

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
input,button{font:inherit;padding:.45rem .6rem;width:100%;box-sizing:border-box}
button{margin-top:.6rem;background:#2c5f4a;color:#fff;border:0;border-radius:8px}
button.secondary{background:#eee;color:#222;margin-top:.35rem}
.muted{color:#666;font-size:.9rem}
.row{display:flex;gap:.4rem;align-items:center}
.row input{flex:1}
.list{border:1px solid #ddd;border-radius:8px;max-height:14rem;overflow:auto;margin:.5rem 0}
.list button{display:block;width:100%;text-align:left;background:#fff;color:#222;border:0;border-bottom:1px solid #eee;border-radius:0;margin:0;padding:.55rem .7rem}
.list button:last-child{border-bottom:0}
.list .file{color:#666}
#status{margin-top:.75rem;min-height:1.2em}
</style>
</head>
<body>
<h1>Basilauncher transfer</h1>
<p class="muted">Pick any folder on the SD card, then upload one or more files.</p>
<div class="card">
<label>Folder</label>
<div class="row">
<input id="dir" value="/" maxlength="120" spellcheck="false"/>
<button type="button" class="secondary" id="up" style="width:auto;white-space:nowrap">Up</button>
</div>
<div class="list" id="list"><div class="muted" style="padding:.6rem">Loading...</div></div>
<form id="form">
<label>Files</label>
<input id="file" type="file" multiple required/>
<button type="submit">Upload here</button>
</form>
<p id="status" class="muted"></p>
<p class="muted">Tips: apps go in <code>/firmware</code>. Sleep wallpapers (<code>.bmp</code>, <code>.jpg</code>) go in <code>/sleep</code>.</p>
</div>
<div class="card">
<label>Hotspot password</label>
<input id="pass" maxlength="63" autocomplete="off" spellcheck="false"/>
<button type="button" id="savepass" class="secondary">Save password</button>
<p class="muted">8 to 63 characters. Saving restarts the hotspot. Rejoin with the new password.</p>
</div>
<script>
const dirEl=document.getElementById('dir');
const listEl=document.getElementById('list');
const statusEl=document.getElementById('status');
function norm(p){
  p=(p||'/').trim()||'/';
  if(!p.startsWith('/'))p='/'+p;
  p=p.replace(/\/+/g,'/');
  if(p.length>1&&p.endsWith('/'))p=p.slice(0,-1);
  return p;
}
function parentOf(p){
  p=norm(p);
  if(p==='/')return '/';
  const i=p.lastIndexOf('/');
  return i<=0?'/':p.slice(0,i);
}
async function refresh(){
  const path=norm(dirEl.value);
  dirEl.value=path;
  listEl.innerHTML='<div class="muted" style="padding:.6rem">Loading...</div>';
  try{
    const r=await fetch('/api/ls?path='+encodeURIComponent(path));
    const j=await r.json();
    if(!r.ok)throw new Error(j.error||'List failed');
    dirEl.value=j.path||path;
    const bits=[];
    if(j.path&&j.path!=='/'){
      bits.push('<button type="button" data-path="'+parentOf(j.path)+'">../</button>');
    }
    (j.dirs||[]).forEach(d=>{
      const full=(j.path==='/'?'/':j.path+'/')+d;
      bits.push('<button type="button" data-path="'+full+'">'+d+'/</button>');
    });
    (j.files||[]).forEach(f=>{
      bits.push('<button type="button" class="file" disabled>'+f+'</button>');
    });
    listEl.innerHTML=bits.length?bits.join(''):'<div class="muted" style="padding:.6rem">Empty folder</div>';
    listEl.querySelectorAll('button[data-path]').forEach(b=>{
      b.onclick=()=>{dirEl.value=b.dataset.path;refresh();};
    });
  }catch(e){
    listEl.innerHTML='<div class="muted" style="padding:.6rem">'+(e.message||e)+'</div>';
  }
}
document.getElementById('up').onclick=()=>{dirEl.value=parentOf(dirEl.value);refresh();};
dirEl.addEventListener('change',refresh);
dirEl.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();refresh();}});
document.getElementById('form').onsubmit=async e=>{
  e.preventDefault();
  const files=document.getElementById('file').files;
  if(!files||!files.length)return;
  const path=norm(dirEl.value);
  let ok=0,fail=0;
  const notes=[];
  for(let i=0;i<files.length;i++){
    const file=files[i];
    statusEl.textContent='Uploading '+(i+1)+'/'+files.length+': '+file.name;
    const fd=new FormData();
    fd.append('file',file,file.name);
    try{
      const r=await fetch('/upload?dir='+encodeURIComponent(path),{method:'POST',body:fd});
      const t=await r.text();
      if(r.ok){ok++;notes.push(file.name);}
      else{fail++;notes.push(file.name+': '+(t||'failed'));}
    }catch(err){
      fail++;
      notes.push(file.name+': '+(err.message||err));
    }
  }
  statusEl.textContent=fail
    ?('Done: '+ok+' ok, '+fail+' failed. '+notes.filter(n=>n.includes(':')).slice(0,3).join(' | '))
    :('Saved '+ok+' file'+(ok===1?'':'s'));
  document.getElementById('file').value='';
  refresh();
};
document.getElementById('savepass').onclick=async()=>{
  const pass=document.getElementById('pass').value;
  statusEl.textContent='Saving password...';
  try{
    const r=await fetch('/api/ap-pass',{method:'POST',headers:{'Content-Type':'text/plain'},body:pass});
    statusEl.textContent=await r.text();
  }catch(e){
    statusEl.textContent='If the hotspot dropped, rejoin with the new password.';
  }
};
refresh();
</script>
</body>
</html>)HTML";

bool isSafePathSegment(const char* s) {
  if (!s || !s[0]) return false;
  if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return false;
  for (const char* p = s; *p; ++p) {
    const char c = *p;
    if (c == '/' || c == '\\' || c < 0x20) return false;
  }
  return true;
}

// Normalize to absolute path without trailing slash (except root "/").
// Rejects ".." and empty segments. Returns false on bad input.
bool normalizeDirPath(const char* in, char* out, size_t outLen) {
  if (!in || !out || outLen < 2) return false;
  char tmp[160];
  size_t n = 0;
  if (in[0] != '/') {
    tmp[n++] = '/';
  }
  for (size_t i = 0; in[i] && n + 1 < sizeof(tmp); ++i) {
    char c = in[i];
    if (c == '\\') c = '/';
    if (c == '/' && n > 0 && tmp[n - 1] == '/') continue;
    tmp[n++] = c;
  }
  while (n > 1 && tmp[n - 1] == '/') --n;
  tmp[n] = 0;

  // Walk segments; reject "..".
  char built[160] = "/";
  size_t b = 1;
  const char* p = tmp;
  if (*p == '/') ++p;
  while (*p) {
    const char* slash = strchr(p, '/');
    char seg[64];
    size_t slen = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (slen == 0 || slen >= sizeof(seg)) return false;
    memcpy(seg, p, slen);
    seg[slen] = 0;
    if (!isSafePathSegment(seg)) return false;
    if (b > 1) {
      if (b + 1 >= sizeof(built)) return false;
      built[b++] = '/';
    }
    if (b + slen >= sizeof(built)) return false;
    memcpy(built + b, seg, slen);
    b += slen;
    built[b] = 0;
    if (!slash) break;
    p = slash + 1;
  }
  if (b == 1) {
    snprintf(out, outLen, "/");
  } else {
    if (b >= outLen) return false;
    memcpy(out, built, b + 1);
  }
  return true;
}

void ensureDirTree(const char* dir) {
  if (!dir || dir[0] != '/') return;
  if (strcmp(dir, "/") == 0) return;
  char path[160];
  snprintf(path, sizeof(path), "%s", dir);
  // Create each prefix: /a, /a/b, ...
  for (char* p = path + 1; *p; ++p) {
    if (*p != '/') continue;
    *p = 0;
    if (!SD.exists(path)) SD.mkdir(path);
    *p = '/';
  }
  if (!SD.exists(path)) SD.mkdir(path);
}

const char* leafName(const char* name) {
  if (!name) return "upload.bin";
  const char* slash = strrchr(name, '/');
  const char* base = slash ? slash + 1 : name;
  const char* bslash = strrchr(base, '\\');
  base = bslash ? bslash + 1 : base;
  return (base[0] && isSafePathSegment(base)) ? base : "upload.bin";
}

void handleRoot() {
  gServer->sendHeader("Cache-Control", "no-store");
  gServer->send(200, "text/html", kIndexHtml);
}

void handleList() {
  char path[160];
  const String raw = gServer->hasArg("path") ? gServer->arg("path") : String("/");
  if (!normalizeDirPath(raw.c_str(), path, sizeof(path))) {
    gServer->send(400, "application/json", "{\"error\":\"Bad path\"}");
    return;
  }

  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    gServer->send(404, "application/json", "{\"error\":\"Not a folder\"}");
    return;
  }

  String json;
  json.reserve(512);
  json += "{\"path\":\"";
  json += path;
  json += "\",\"dirs\":[";
  bool firstDir = true;
  bool firstFile = true;
  String files = "],\"files\":[";

  // Cap entries so SoftAP responses stay small.
  int n = 0;
  for (File f = dir.openNextFile(); f && n < 80; f = dir.openNextFile(), ++n) {
    const char* name = f.name();
    // SdFat/SD may return full path; keep leaf only.
    const char* leaf = strrchr(name, '/');
    leaf = leaf ? leaf + 1 : name;
    if (!leaf[0] || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
      f.close();
      continue;
    }
    // Escape minimal JSON string chars.
    auto appendEscaped = [](String& s, const char* t) {
      for (; *t; ++t) {
        if (*t == '"' || *t == '\\') s += '\\';
        if (static_cast<unsigned char>(*t) < 0x20) continue;
        s += *t;
      }
    };
    if (f.isDirectory()) {
      if (!firstDir) json += ',';
      firstDir = false;
      json += '"';
      appendEscaped(json, leaf);
      json += '"';
    } else {
      if (!firstFile) files += ',';
      firstFile = false;
      files += '"';
      appendEscaped(files, leaf);
      files += '"';
    }
    f.close();
  }
  dir.close();
  json += files;
  json += "]}";
  gServer->sendHeader("Cache-Control", "no-store");
  gServer->send(200, "application/json", json);
}

void handleUpload() {
  HTTPUpload& upload = gServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    gUploadOk = false;
    gUploadPath[0] = 0;
    const String raw = gServer->hasArg("dir") ? gServer->arg("dir") : String("/");
    if (!normalizeDirPath(raw.c_str(), gUploadDir, sizeof(gUploadDir))) {
      snprintf(gLastMsg, sizeof(gLastMsg), "Bad folder path");
      return;
    }
    ensureDirTree(gUploadDir);
    const char* leaf = leafName(upload.filename.c_str());
    if (strcmp(gUploadDir, "/") == 0) {
      snprintf(gUploadPath, sizeof(gUploadPath), "/%s", leaf);
    } else {
      snprintf(gUploadPath, sizeof(gUploadPath), "%s/%s", gUploadDir, leaf);
    }
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

void handleApPass() {
  if (!gServer) return;
  const String body = gServer->arg("plain");
  if (!wifiSetApPassword(body.c_str())) {
    gServer->send(400, "text/plain", "Use 8 to 63 printable characters.");
    return;
  }
  gServer->send(200, "text/plain", "Saved. Rejoin Basilauncher with the new password.");
  gRestartAp = true;
}

void handleNotFound() {
  handleRoot();
}

void handleNoContent() {
  gServer->send(204);
}

void handleAppleCaptive() {
  gServer->send(200, "text/html",
                "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

void handleNcsi() {
  gServer->send(200, "text/plain", "Microsoft NCSI");
}

}  // namespace

bool wifiServerStart() {
  wifiServerStop();
  gServer = new WebServer(80);
  if (!gServer) return false;
  gServer->on("/", HTTP_GET, handleRoot);
  gServer->on("/api/ls", HTTP_GET, handleList);
  gServer->on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  gServer->on("/api/ap-pass", HTTP_POST, handleApPass);
  // Captive / connectivity checks so phones open a browser to our page.
  gServer->on("/generate_204", HTTP_GET, handleNoContent);
  gServer->on("/gen_204", HTTP_GET, handleNoContent);
  gServer->on("/hotspot-detect.html", HTTP_GET, handleAppleCaptive);
  gServer->on("/library/test/success.html", HTTP_GET, handleAppleCaptive);
  gServer->on("/ncsi.txt", HTTP_GET, handleNcsi);
  gServer->on("/connecttest.txt", HTTP_GET, handleNcsi);
  gServer->on("/fwlink/", HTTP_GET, handleRoot);
  gServer->on("/canonical.html", HTTP_GET, handleRoot);
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

bool wifiServerTakeApRestart() {
  const bool restart = gRestartAp;
  gRestartAp = false;
  return restart;
}
