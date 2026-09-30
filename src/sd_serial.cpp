#include "sd_serial.h"

#include "ui.h"

#include <Arduino.h>
#include <SD.h>
#include <cstring>

namespace {

constexpr size_t kChunk = 1024;
constexpr uint32_t kIdleTimeoutMs = 15000;

bool readExact(uint8_t* dst, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t last = millis();
  while (got < n) {
    const int avail = Serial.available();
    if (avail > 0) {
      const size_t want = n - got;
      const size_t take = static_cast<size_t>(avail) < want ? static_cast<size_t>(avail) : want;
      const int r = Serial.readBytes(reinterpret_cast<char*>(dst + got), take);
      if (r > 0) {
        got += static_cast<size_t>(r);
        last = millis();
      }
    } else if (millis() - last > timeoutMs) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

bool pathAllowed(const char* path) {
  if (!path || path[0] != '/') return false;
  if (strstr(path, "..") != nullptr) return false;
  // Host may write firmware bins and custom sleep images only.
  if (strncmp(path, "/firmware", 9) == 0 && (path[9] == '\0' || path[9] == '/')) return true;
  if (strncmp(path, "/sleep", 6) == 0 && (path[6] == '\0' || path[6] == '/')) return true;
  return false;
}

void ensureParentDirs(const char* path) {
  // Create each directory prefix (not the leaf file).
  char tmp[192];
  snprintf(tmp, sizeof(tmp), "%s", path);
  char* slash = strchr(tmp + 1, '/');
  while (slash) {
    *slash = '\0';
    if (!SD.exists(tmp)) SD.mkdir(tmp);
    *slash = '/';
    slash = strchr(slash + 1, '/');
  }
}

void handleLs(const char* path) {
  if (!pathAllowed(path)) {
    Serial.println("ERR bad path");
    return;
  }
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    Serial.println("ERR not a dir");
    return;
  }
  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;
    const char* name = f.name();
    // Bare leaf name (Arduino SD may return full path).
    const char* leaf = strrchr(name, '/');
    leaf = leaf ? leaf + 1 : name;
    if (leaf[0] == '.' && leaf[1] == '_') {
      f.close();
      continue;
    }
    if (strcmp(leaf, ".DS_Store") == 0) {
      f.close();
      continue;
    }
    if (f.isDirectory()) {
      Serial.printf("D %s\n", leaf);
    } else {
      Serial.printf("F %u %s\n", static_cast<unsigned>(f.size()), leaf);
    }
    f.close();
  }
  dir.close();
  Serial.println("END");
}

void handlePut(const char* path, size_t size) {
  if (!pathAllowed(path) || strcmp(path, "/firmware") == 0 || strcmp(path, "/sleep") == 0) {
    Serial.println("ERR bad path");
    return;
  }
  if (size > 16u * 1024u * 1024u) {
    Serial.println("ERR too large");
    return;
  }
  ensureParentDirs(path);
  if (SD.exists(path)) SD.remove(path);

  File out = SD.open(path, FILE_WRITE);
  if (!out) {
    Serial.println("ERR open");
    return;
  }

  // Don't refresh the panel mid-stream — e-ink blocking would overrun the CDC RX FIFO.
  // ACK each chunk with '.' so the host cannot flood the tiny USB RX buffer.
  Serial.println("READY");
  Serial.flush();

  uint8_t buf[kChunk];
  size_t written = 0;
  while (written < size) {
    const size_t need = (size - written) < kChunk ? (size - written) : kChunk;
    if (!readExact(buf, need, kIdleTimeoutMs)) {
      out.close();
      SD.remove(path);
      Serial.println("ERR timeout");
      return;
    }
    const size_t w = out.write(buf, need);
    if (w != need) {
      out.close();
      SD.remove(path);
      Serial.println("ERR write");
      return;
    }
    written += w;
    Serial.write('.');
    Serial.flush();
  }
  out.flush();
  out.close();
  Serial.printf("\nDONE %u\n", static_cast<unsigned>(written));
  Serial.flush();
  uiDrawProgress("SD upload", 100);
}

void handleGet(const char* path) {
  if (!pathAllowed(path) || strcmp(path, "/firmware") == 0 || strcmp(path, "/sleep") == 0) {
    Serial.println("ERR bad path");
    return;
  }
  File in = SD.open(path, FILE_READ);
  if (!in || in.isDirectory()) {
    Serial.println("ERR open");
    return;
  }
  const size_t size = in.size();
  Serial.printf("SIZE %u\n", static_cast<unsigned>(size));
  Serial.flush();

  uint8_t buf[kChunk];
  size_t sent = 0;
  while (sent < size) {
    const size_t need = (size - sent) < kChunk ? (size - sent) : kChunk;
    const int n = in.read(buf, need);
    if (n <= 0) {
      in.close();
      Serial.println("ERR read");
      return;
    }
    Serial.write(buf, static_cast<size_t>(n));
    Serial.flush();
    // Wait for host '.' ACK
    const uint32_t start = millis();
    int ack = -1;
    while (millis() - start < 15000) {
      if (Serial.available()) {
        ack = Serial.read();
        break;
      }
      delay(1);
    }
    if (ack != '.') {
      in.close();
      Serial.println("ERR ack");
      return;
    }
    sent += static_cast<size_t>(n);
  }
  in.close();
  Serial.printf("DONE %u\n", static_cast<unsigned>(sent));
  Serial.flush();
}

// Classify app-only vs LilyGO merged factory dump (bootloader @0, app @0x10000).
void handleProbe(const char* path) {
  if (!pathAllowed(path)) {
    Serial.println("ERR bad path");
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    Serial.println("ERR open");
    return;
  }
  const size_t size = f.size();
  auto mappedAt = [&](size_t base) -> bool {
    uint8_t hdr[24];
    if (base + sizeof(hdr) > size) return false;
    if (!f.seek(base)) return false;
    if (f.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) return false;
    if (hdr[0] != 0xE9) return false;
    const uint8_t nseg = hdr[1];
    if (nseg == 0 || nseg > 16) return false;
    size_t pos = base + sizeof(hdr);
    for (uint8_t i = 0; i < nseg; ++i) {
      uint8_t sh[8];
      if (!f.seek(pos) || f.read(sh, 8) != 8) return false;
      uint32_t addr = 0, len = 0;
      memcpy(&addr, sh, 4);
      memcpy(&len, sh + 4, 4);
      const uint32_t top = addr & 0xFF000000u;
      if (top == 0x3C000000u || top == 0x42000000u) return true;
      pos += 8 + len;
    }
    return false;
  };
  const bool at0 = mappedAt(0);
  const bool at64k = mappedAt(0x10000);
  const char* kind = at0 ? "APP" : (at64k ? "MERGED" : "BAD");
  Serial.printf("PROBE %s size=%u app0=%d app64k=%d\n", kind, static_cast<unsigned>(size), at0 ? 1 : 0,
                at64k ? 1 : 0);
  f.close();
}

void handleLine(char* line) {
  if (strcmp(line, "BASI") == 0) {
    Serial.printf("BASI OK %s\n", BASILAUNCHER_VERSION);
    return;
  }
  if (strncmp(line, "MKDIR ", 6) == 0) {
    const char* path = line + 6;
    if (!pathAllowed(path)) {
      Serial.println("ERR bad path");
      return;
    }
    if (SD.exists(path) || SD.mkdir(path)) Serial.println("OK");
    else Serial.println("ERR mkdir");
    return;
  }
  if (strncmp(line, "LS ", 3) == 0) {
    handleLs(line + 3);
    return;
  }
  if (strncmp(line, "PUT ", 4) == 0) {
    char path[192];
    unsigned long size = 0;
    if (sscanf(line + 4, "%191s %lu", path, &size) != 2) {
      Serial.println("ERR syntax");
      return;
    }
    handlePut(path, static_cast<size_t>(size));
    return;
  }
  if (strncmp(line, "GET ", 4) == 0) {
    handleGet(line + 4);
    return;
  }
  if (strncmp(line, "PROBE ", 6) == 0) {
    handleProbe(line + 6);
    return;
  }
}

}  // namespace

void sdSerialPoll() {
  static char line[256];
  static size_t len = 0;

  while (Serial.available()) {
    const int c = Serial.read();
    if (c < 0) return;
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      if (len > 0) handleLine(line);
      len = 0;
      return;
    }
    if (len + 1 < sizeof(line)) {
      line[len++] = static_cast<char>(c);
    } else {
      len = 0;  // overflow — resync
    }
  }
}
