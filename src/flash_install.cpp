#include "flash_install.h"

#include "apps_scan.h"

#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t kEspImageMagic = 0xE9;
constexpr size_t kChunk = 4096;
constexpr size_t kMergedAppOffset = 0x10000;

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

// Guest slots only. Factory (Basilauncher) is never returned.
const esp_partition_t* slotPartition(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return nullptr;
  const esp_partition_t* p =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, slotSubtype(slotIndex), nullptr);
  if (!p) return nullptr;
  if (p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return nullptr;
  if (p->address < 0x190000) return nullptr;
  return p;
}

// True if the ESP image at file offset `base` contains flash-mapped IROM/DROM
// segments (a real app). Bootloader stubs only have IRAM/DRAM and are not bootable
// as an OTA payload — LilyGO "factory" dumps put those at 0x0 and the app at 0x10000.
bool imageHasMappedFlashSeg(File& f, size_t base) {
  uint8_t hdr[24];
  if (!f.seek(base)) return false;
  if (f.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) return false;
  if (hdr[0] != kEspImageMagic) return false;
  const uint8_t nseg = hdr[1];
  if (nseg == 0 || nseg > 16) return false;

  size_t pos = base + sizeof(hdr);
  for (uint8_t i = 0; i < nseg; ++i) {
    uint8_t sh[8];
    if (!f.seek(pos)) return false;
    if (f.read(sh, sizeof(sh)) != static_cast<int>(sizeof(sh))) return false;
    uint32_t addr = 0;
    uint32_t len = 0;
    memcpy(&addr, sh, 4);
    memcpy(&len, sh + 4, 4);
    if (len > 16u * 1024u * 1024u) return false;
    const uint32_t top = addr & 0xFF000000u;
    // ESP32-S3: DROM ~0x3Cxxxxxx, IROM ~0x42xxxxxx
    if (top == 0x3C000000u || top == 0x42000000u) return true;
    pos += sizeof(sh) + len;
  }
  return false;
}

// App-only image → offset 0. Merged bootloader+app dump → offset 0x10000.
bool resolvePayload(File& f, size_t fileSize, size_t& payloadOff, size_t& payloadSize) {
  payloadOff = 0;
  payloadSize = fileSize;
  if (fileSize < 0x100) return false;

  if (imageHasMappedFlashSeg(f, 0)) {
    payloadOff = 0;
    payloadSize = fileSize;
    return true;
  }
  if (fileSize > kMergedAppOffset && imageHasMappedFlashSeg(f, kMergedAppOffset)) {
    payloadOff = kMergedAppOffset;
    payloadSize = fileSize - kMergedAppOffset;
    return true;
  }
  return false;
}

}  // namespace

const char* flashResultName(FlashResult r) {
  switch (r) {
    case FlashResult::Ok:
      return "ok";
    case FlashResult::OpenFail:
      return "open fail";
    case FlashResult::TooSmall:
      return "too small";
    case FlashResult::TooLarge:
      return "too large";
    case FlashResult::BadMagic:
      return "bad magic";
    case FlashResult::NoPartition:
      return "no partition";
    case FlashResult::EraseFail:
      return "erase fail";
    case FlashResult::WriteFail:
      return "write fail";
    case FlashResult::ReadFail:
      return "read fail";
    case FlashResult::BootFail:
      return "boot fail";
    case FlashResult::Forbidden:
      return "forbidden";
    case FlashResult::Occupied:
      return "occupied";
  }
  return "unknown";
}

size_t flashSlotCapacity(int slotIndex) {
  const esp_partition_t* dest = slotPartition(slotIndex);
  return dest ? dest->size : 0;
}

FlashResult flashValidateAndWrite(const char* sdPath, int slotIndex, FlashProgressCb cb, void* ctx) {
  const esp_partition_t* dest = slotPartition(slotIndex);
  if (!dest) return FlashResult::NoPartition;

  const esp_partition_t* factory =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (factory && dest->address == factory->address) return FlashResult::Forbidden;

  // Never overwrite an installed guest app — user must Clear first.
  uint8_t magic = 0;
  if (esp_partition_read(dest, 0, &magic, 1) == ESP_OK && magic == kEspImageMagic) {
    return FlashResult::Occupied;
  }

  File f = SD.open(sdPath, FILE_READ);
  if (!f) return FlashResult::OpenFail;

  const size_t fileSize = f.size();
  size_t payloadOff = 0;
  size_t payloadSize = 0;
  if (!resolvePayload(f, fileSize, payloadOff, payloadSize)) {
    f.close();
    return FlashResult::BadMagic;
  }
  if (payloadSize < 0x100) {
    f.close();
    return FlashResult::TooSmall;
  }
  if (payloadSize > dest->size) {
    f.close();
    return FlashResult::TooLarge;
  }

  if (esp_partition_erase_range(dest, 0, dest->size) != ESP_OK) {
    f.close();
    return FlashResult::EraseFail;
  }

  if (!f.seek(payloadOff)) {
    f.close();
    return FlashResult::ReadFail;
  }

  size_t offset = 0;
  uint8_t buf[kChunk];
  while (offset < payloadSize) {
    const size_t n = f.read(buf, std::min(kChunk, payloadSize - offset));
    if (n == 0) {
      f.close();
      return FlashResult::ReadFail;
    }
    if (esp_partition_write(dest, offset, buf, n) != ESP_OK) {
      f.close();
      return FlashResult::WriteFail;
    }
    offset += n;
    if (cb) cb(offset, payloadSize, ctx);
    yield();
  }
  f.close();
  if (payloadOff != 0) {
    Serial.printf("Install: skipped 0x%X bootloader prefix (%u -> %u bytes)\n",
                  static_cast<unsigned>(payloadOff), static_cast<unsigned>(fileSize),
                  static_cast<unsigned>(payloadSize));
  }
  return FlashResult::Ok;
}

FlashResult flashEraseSlot(int slotIndex) {
  const esp_partition_t* dest = slotPartition(slotIndex);
  if (!dest) return FlashResult::NoPartition;
  const esp_partition_t* factory =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (factory && dest->address == factory->address) return FlashResult::Forbidden;
  if (esp_partition_erase_range(dest, 0, dest->size) != ESP_OK) return FlashResult::EraseFail;
  return FlashResult::Ok;
}

FlashResult bootSlotPendingVerify(int slotIndex) {
  const esp_partition_t* dest = slotPartition(slotIndex);
  if (!dest) return FlashResult::NoPartition;

  const esp_err_t err = esp_ota_set_boot_partition(dest);
  if (err != ESP_OK) {
    Serial.printf("esp_ota_set_boot_partition: %s\n", esp_err_to_name(err));
    return FlashResult::BootFail;
  }
  Serial.printf("Booting slot %d (%s) pending verify\n", slotIndex, dest->label);
  delay(50);
  esp_restart();
  return FlashResult::Ok;
}
