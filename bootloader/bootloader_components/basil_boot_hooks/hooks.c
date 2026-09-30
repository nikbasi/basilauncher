// Basilauncher boot policy, applied before the bootloader picks an app.
//
// A double press of RST (two deliberate resets within DOUBLE_PRESS_WINDOW_MS)
// erases otadata, so the stock selection falls back to the factory partition.
// A single deliberate reset (RST/EN, power-on, USB reset from a host) just
// restarts the current app after the window. Every other reset keeps the
// current app with no delay: a guest's own esp_restart(), a deep-sleep wake, a
// watchdog or a panic. Works for any guest firmware without changes, including
// Arduino apps that mark themselves valid on boot.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bootloader_flash_priv.h"
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/reset_reasons.h"

static const char* TAG = "basil";

#define PART_TABLE_ADDR 0x8000
#define PART_TABLE_MAX_ENTRIES 95

// Last sector before the partition table; the bootloader image must stay below it.
#define ARM_FLAG_ADDR 0x7000
#define ARM_FLAG_MAGIC 0xBA51A4EDu
#define DOUBLE_PRESS_WINDOW_MS 800

void bootloader_hooks_include(void) {}

void bootloader_before_init(void) {}

static bool isDeliberateReset(soc_reset_reason_t reason) {
  return reason == RESET_REASON_CHIP_POWER_ON || reason == RESET_REASON_CORE_USB_UART ||
         reason == RESET_REASON_CORE_USB_JTAG;
}

// Finds otadata; only reports it when a factory app exists to fall back to.
static bool findOtadata(uint32_t* offset, uint32_t* size) {
  bool haveFactory = false;
  bool haveOtadata = false;
  for (int i = 0; i < PART_TABLE_MAX_ENTRIES; ++i) {
    esp_partition_info_t entry;
    if (bootloader_flash_read(PART_TABLE_ADDR + i * sizeof(entry), &entry, sizeof(entry), false) != ESP_OK) {
      return false;
    }
    if (entry.magic != ESP_PARTITION_MAGIC) break;
    if (entry.type == PART_TYPE_APP && entry.subtype == PART_SUBTYPE_FACTORY) haveFactory = true;
    if (entry.type == PART_TYPE_DATA && entry.subtype == PART_SUBTYPE_DATA_OTA) {
      *offset = entry.pos.offset;
      *size = entry.pos.size;
      haveOtadata = true;
    }
  }
  return haveFactory && haveOtadata;
}

static bool sectorBlank(uint32_t addr) {
  uint32_t words[sizeof(esp_ota_select_entry_t) / sizeof(uint32_t)];
  if (bootloader_flash_read(addr, words, sizeof(words), false) != ESP_OK) return false;
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
    if (words[i] != 0xFFFFFFFFu) return false;
  }
  return true;
}

static uint32_t readArmFlag(void) {
  uint32_t word = 0xFFFFFFFFu;
  if (bootloader_flash_read(ARM_FLAG_ADDR, &word, sizeof(word), false) != ESP_OK) return 0xFFFFFFFFu;
  return word;
}

static void clearArmFlag(void) {
  if (readArmFlag() != 0xFFFFFFFFu) bootloader_flash_erase_range(ARM_FLAG_ADDR, FLASH_SECTOR_SIZE);
}

static void returnToLauncher(soc_reset_reason_t reason) {
  uint32_t offset = 0;
  uint32_t size = 0;
  if (!findOtadata(&offset, &size) || size < 2 * FLASH_SECTOR_SIZE) return;
  if (sectorBlank(offset) && sectorBlank(offset + FLASH_SECTOR_SIZE)) return;

  if (bootloader_flash_erase_range(offset, size) == ESP_OK) {
    ESP_LOGW(TAG, "double reset 0x%x: back to Basilauncher", (unsigned)reason);
  } else {
    ESP_LOGE(TAG, "otadata erase failed");
  }
}

void bootloader_after_init(void) {
  const soc_reset_reason_t reason = esp_rom_get_reset_reason(0);
  if (!isDeliberateReset(reason)) {
    clearArmFlag();
    return;
  }

  if (readArmFlag() == ARM_FLAG_MAGIC) {
    clearArmFlag();
    returnToLauncher(reason);
    return;
  }

  // Arm, then wait: a second deliberate reset inside the window lands above.
  clearArmFlag();
  const uint32_t magic = ARM_FLAG_MAGIC;
  if (bootloader_flash_write(ARM_FLAG_ADDR, (void*)&magic, sizeof(magic), false) != ESP_OK) {
    ESP_LOGE(TAG, "arm flag write failed");
    return;
  }
  esp_rom_delay_us(DOUBLE_PRESS_WINDOW_MS * 1000);
  clearArmFlag();
}
