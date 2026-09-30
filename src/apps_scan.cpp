#include "apps_scan.h"

#include <Preferences.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

Preferences prefs;

esp_partition_subtype_t slotSubtype(int slotIndex) {
  switch (slotIndex) {
    case 0:
      return ESP_PARTITION_SUBTYPE_APP_OTA_0;
    case 1:
      return ESP_PARTITION_SUBTYPE_APP_OTA_1;
    case 2:
      return ESP_PARTITION_SUBTYPE_APP_OTA_2;
    case 3:
      return ESP_PARTITION_SUBTYPE_APP_OTA_3;
    default:
      return ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
  }
}

const esp_partition_t* slotPart(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return nullptr;
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, slotSubtype(slotIndex), nullptr);
}

bool slotHasImage(int slotIndex) {
  const esp_partition_t* p = slotPart(slotIndex);
  if (!p) return false;
  uint8_t magic = 0;
  if (esp_partition_read(p, 0, &magic, 1) != ESP_OK) return false;
  return magic == 0xE9;
}

}  // namespace

void appsFormatBytes(size_t bytes, char* buf, size_t bufLen) {
  if (!buf || bufLen == 0) return;
  if (bytes >= 1024u * 1024u) {
    const unsigned mb10 = static_cast<unsigned>((bytes * 10u) / (1024u * 1024u));
    snprintf(buf, bufLen, "%u.%u MB", mb10 / 10u, mb10 % 10u);
  } else if (bytes >= 1024u) {
    snprintf(buf, bufLen, "%u KB", static_cast<unsigned>(bytes / 1024u));
  } else {
    snprintf(buf, bufLen, "%u B", static_cast<unsigned>(bytes));
  }
}

void appsLoadSlotLabels() {
  if (!prefs.begin("basil", false)) return;
  for (int i = 0; i < kSlotCount; ++i) {
    char keyN[8], keyS[8];
    snprintf(keyN, sizeof(keyN), "n%d", i);
    snprintf(keyS, sizeof(keyS), "s%d", i);
    if (!prefs.isKey(keyN)) prefs.putString(keyN, "");
    if (!prefs.isKey(keyS)) prefs.putULong(keyS, 0);
  }
  prefs.end();
}

void appsSaveSlotLabel(int slotIndex, const char* name, size_t size) {
  if (slotIndex < 0 || slotIndex >= kSlotCount || !name) return;
  if (!prefs.begin("basil", false)) return;
  char keyN[8], keyS[8];
  snprintf(keyN, sizeof(keyN), "n%d", slotIndex);
  snprintf(keyS, sizeof(keyS), "s%d", slotIndex);
  prefs.putString(keyN, name);
  prefs.putULong(keyS, static_cast<uint32_t>(size));
  prefs.end();
}

void appsClearSlotLabel(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;
  if (!prefs.begin("basil", false)) return;
  char keyN[8], keyS[8];
  snprintf(keyN, sizeof(keyN), "n%d", slotIndex);
  snprintf(keyS, sizeof(keyS), "s%d", slotIndex);
  prefs.putString(keyN, "");
  prefs.putULong(keyS, 0);
  prefs.end();
}

SlotInfo appsSlotInfo(int slotIndex) {
  SlotInfo info;
  info.index = slotIndex;
  info.label[0] = static_cast<char>('A' + (slotIndex >= 0 && slotIndex < kSlotCount ? slotIndex : 0));
  info.label[1] = 0;

  const esp_partition_t* p = slotPart(slotIndex);
  info.capacity = p ? p->size : 0;
  info.occupied = slotHasImage(slotIndex);

  String n;
  if (prefs.begin("basil", true)) {
    char keyN[8], keyS[8];
    snprintf(keyN, sizeof(keyN), "n%d", slotIndex);
    snprintf(keyS, sizeof(keyS), "s%d", slotIndex);
    n = prefs.getString(keyN, "");
    info.size = prefs.getULong(keyS, 0);
    prefs.end();
  }

  if (n.length() > 0) {
    info.name = n.c_str();
  } else if (info.occupied) {
    char fallback[12];
    snprintf(fallback, sizeof(fallback), "ota_%d", slotIndex);
    info.name = fallback;
  } else {
    info.name = "Empty";
  }
  return info;
}

void appsAllSlots(SlotInfo out[kSlotCount]) {
  for (int i = 0; i < kSlotCount; ++i) out[i] = appsSlotInfo(i);
}

FlashSpace appsFlashSpace() {
  FlashSpace sp;
  for (int i = 0; i < kSlotCount; ++i) {
    const SlotInfo s = appsSlotInfo(i);
    sp.guestTotal += s.capacity;
    if (s.occupied) {
      sp.guestUsed += s.capacity;
      sp.occupiedSlots++;
    } else {
      sp.guestFree += s.capacity;
      sp.emptySlots++;
    }
  }
  return sp;
}

int appsBestFitSlot(size_t bytes) {
  int best = -1;
  size_t bestCap = SIZE_MAX;
  for (int i = 0; i < kSlotCount; ++i) {
    const SlotInfo s = appsSlotInfo(i);
    if (s.occupied) continue;
    if (s.capacity < bytes) continue;
    if (s.capacity < bestCap) {
      bestCap = s.capacity;
      best = i;
    }
  }
  return best;
}

int appsFittingEmptySlots(size_t bytes, int* outIndices, int maxOut) {
  int n = 0;
  for (int i = 0; i < kSlotCount && n < maxOut; ++i) {
    const SlotInfo s = appsSlotInfo(i);
    if (s.occupied) continue;
    if (s.capacity < bytes) continue;
    if (outIndices) outIndices[n] = i;
    n++;
  }
  return n;
}

std::vector<FirmwareFile> appsScanFirmwareDir() {
  std::vector<FirmwareFile> out;
  const auto entries = appsScanDir("/firmware");
  out.reserve(entries.size());
  for (const auto& e : entries) {
    if (e.isDir) continue;
    FirmwareFile f;
    f.name = e.name;
    f.path = e.path;
    f.size = e.size;
    out.push_back(std::move(f));
  }
  return out;
}

bool appsIsRootDir(const char* path) {
  return path && (path[0] == '/' && path[1] == 0);
}

void appsParentDir(const char* path, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  if (!path || appsIsRootDir(path)) {
    snprintf(out, outLen, "/");
    return;
  }
  // Copy and strip trailing slash
  char buf[192];
  snprintf(buf, sizeof(buf), "%s", path);
  size_t n = strlen(buf);
  while (n > 1 && buf[n - 1] == '/') {
    buf[--n] = 0;
  }
  char* slash = strrchr(buf, '/');
  if (!slash || slash == buf) {
    snprintf(out, outLen, "/");
    return;
  }
  *slash = 0;
  snprintf(out, outLen, "%s", buf[0] ? buf : "/");
}

std::vector<DirEntry> appsScanDir(const char* dirPath) {
  std::vector<DirEntry> out;
  out.reserve(32);
  if (!dirPath || !dirPath[0]) dirPath = "/";

  if (!SD.exists(dirPath)) {
    if (strcmp(dirPath, "/firmware") == 0) SD.mkdir("/firmware");
    else return out;
  }

  File root = SD.open(dirPath);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return out;
  }

  const bool atRoot = appsIsRootDir(dirPath);
  File entry = root.openNextFile();
  while (entry) {
    String raw = entry.name();
    // SdFat / SD may return "/firmware/foo.bin" or "foo.bin"
    const char* rawC = raw.c_str();
    const char* base = strrchr(rawC, '/');
    base = base ? base + 1 : rawC;
    if (base[0] == 0 || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
      entry.close();
      entry = root.openNextFile();
      continue;
    }
    // macOS AppleDouble / Finder metadata (._*, .DS_Store)
    if (base[0] == '.' && base[1] == '_') {
      entry.close();
      entry = root.openNextFile();
      continue;
    }
    if (strcmp(base, ".DS_Store") == 0 || strcmp(base, ".Spotlight-V100") == 0 ||
        strcmp(base, ".Trashes") == 0 || strcmp(base, ".fseventsd") == 0) {
      entry.close();
      entry = root.openNextFile();
      continue;
    }

    DirEntry d;
    d.name = base;
    d.isDir = entry.isDirectory();
    d.size = d.isDir ? 0 : entry.size();

    if (atRoot) {
      d.path = std::string("/") + base;
    } else {
      d.path = std::string(dirPath);
      if (d.path.back() != '/') d.path.push_back('/');
      d.path += base;
    }

    if (d.isDir) {
      out.push_back(std::move(d));
    } else {
      String lower = d.name.c_str();
      lower.toLowerCase();
      if (lower.endsWith(".bin")) out.push_back(std::move(d));
    }

    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
    if (a.isDir != b.isDir) return a.isDir && !b.isDir;
    return a.name < b.name;
  });
  return out;
}
