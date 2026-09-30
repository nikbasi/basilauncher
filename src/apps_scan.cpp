#include "apps_scan.h"

#include <Preferences.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <cstdio>

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
  out.reserve(16);
  if (!SD.exists("/firmware")) {
    SD.mkdir("/firmware");
    return out;
  }
  File root = SD.open("/firmware");
  if (!root || !root.isDirectory()) return out;

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".bin")) {
        FirmwareFile f;
        f.name = name.c_str();
        f.path = std::string("/firmware/") + name.c_str();
        f.size = entry.size();
        out.push_back(std::move(f));
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return out;
}
