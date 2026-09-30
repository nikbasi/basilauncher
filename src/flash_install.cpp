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
  if (fileSize < 0x100) {
    f.close();
    return FlashResult::TooSmall;
  }
  if (fileSize > dest->size) {
    f.close();
    return FlashResult::TooLarge;
  }

  uint8_t fileMagic = 0;
  if (f.read(&fileMagic, 1) != 1 || fileMagic != kEspImageMagic) {
    f.close();
    return FlashResult::BadMagic;
  }
  f.seek(0);

  if (esp_partition_erase_range(dest, 0, dest->size) != ESP_OK) {
    f.close();
    return FlashResult::EraseFail;
  }

  size_t offset = 0;
  uint8_t buf[kChunk];
  while (offset < fileSize) {
    const size_t n = f.read(buf, std::min(kChunk, fileSize - offset));
    if (n == 0) {
      f.close();
      return FlashResult::ReadFail;
    }
    if (esp_partition_write(dest, offset, buf, n) != ESP_OK) {
      f.close();
      return FlashResult::WriteFail;
    }
    offset += n;
    if (cb) cb(offset, fileSize, ctx);
    yield();
  }
  f.close();
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
