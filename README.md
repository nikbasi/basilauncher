# Basilauncher

Aurora-style reboot-gated firmware hub for the **LilyGO T5 E-Paper S3 Pro**.

## Role

Basilauncher lives in the **factory** flash partition and is **never overwritten** by the
on-device UI. Guest apps install only into empty `ota_0`…`ota_3` slots (best-fit). To free
space, tap **Clear** on a slot. Any reboot returns here — guest apps must not call
`esp_ota_mark_app_valid_cancel_rollback()`.

## Partition map (16 MB)

| Slot | Partition | Size | Notes |
|------|-----------|------|--------|
| Launcher | `factory` | 1.5 MB | Protected |
| A | `ota_0` | 6 MB | Fits CrossPoint |
| B | `ota_1` | 3 MB | Smaller apps |
| C | `ota_2` | 3 MB | Smaller apps |
| D | `ota_3` | 2.25 MB | Smaller apps |

Status bar always shows **version** and **free / total guest flash**.

## Build / flash

Uses the same FreeInk display path as CrossPoint `lilygo_pro` (`BoardT5S3` +
`LgfxEpd` + M5GFX). Do **not** use FastEPD.

```bash
cd basilauncher
pio run -e basilauncher
pio run -e basilauncher -t upload --upload-port /dev/cu.usbmodem101
```

Upload writes **only** the factory image at `0x10000` and resets otadata.

If partitions changed (first flash of 1.0.9+), rewrite the table once:

```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 write-flash \
  0x0 .pio/build/basilauncher/bootloader.bin \
  0x8000 .pio/build/basilauncher/partitions.bin \
  0xe000 $HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/basilauncher/firmware.bin
```

## SD layout

```
/firmware/
  crosspoint-….bin
  flashcards-….bin
  …
```

Tap a file → installs into the **smallest empty slot that fits**. If none fit, Clear a slot.

## UI

Portrait 540×960. Status: version + free space. Four guest cards with Boot / Clear.
Dock: Apps / Settings.
