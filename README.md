# Basilauncher

Aurora-style reboot-gated firmware hub for the **LilyGO T5 E-Paper S3 Pro**.

## Role

Basilauncher lives in the **factory** flash partition and is **never overwritten** by the
on-device UI. Guest apps install only into empty `ota_0`…`ota_3` slots (best-fit). To free
space, tap **Clear** on a slot.

## Returning to the launcher

The custom bootloader in [`bootloader/`](bootloader/) decides by reset cause, so it
works for any guest firmware without changes:

| Reset | Boots |
|-------|-------|
| **RST pressed twice** within 0.8 s | Basilauncher (otadata erased) |
| RST pressed once, power-on | The same app, after a 0.8 s wait |
| USB reset from a host, app's own restart, deep-sleep wake, watchdog, crash | The same app, no wait |

**Double-press RST** from any app to get back here; a single stray press only
restarts the app. The pending first press is a flag word in the sector at
`0x7000`, so the bootloader image must stay below 28 KB (`flashbl` checks this).

## Partition map (16 MB)

| Slot | Partition | Size | Notes |
|------|-----------|------|--------|
| Launcher | `factory` | 1.5 MB | Protected |
| A | `ota_0` | 6 MB | Fits CrossPoint |
| B | `ota_1` | 2.5 MB | Smaller apps |
| C | `ota_2` | 2.5 MB | Smaller apps |
| D | `ota_3` | 2.25 MB | Smaller apps |
| (shared) | `spiffs` | 1 MB | LittleFS for Meshtastic-style guests |

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

If partitions changed (first flash of 1.0.9+), rewrite the table once (then flash
the custom bootloader below, since this writes the stock one):

```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 write-flash \
  0x0 .pio/build/basilauncher/bootloader.bin \
  0x8000 .pio/build/basilauncher/partitions.bin \
  0x10000 .pio/build/basilauncher/firmware.bin
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 erase-region 0xe000 0x2000
```

Custom bootloader (only `0x0`; apps, slots and otadata are untouched):

```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 write-flash 0x0 bootloader/basil-bootloader.bin
```

Rebuilding it: `cd bootloader && pio run` configures the ESP-IDF bootloader
subproject (the stub app step fails on a PlatformIO SCons issue and is not needed),
then `ninja` in `.pio/build/bootloader/bootloader` and `esptool elf2image
--flash-mode dio --flash-freq 80m --flash-size 16MB` produce `bootloader.bin`.

## SD layout

```
/firmware/
  crosspoint-….bin
  flashcards-….bin
  …
```

Tap a file → installs into the **smallest empty slot that fits**. If none fit, Clear a slot.

## UI

Portrait 540×960 with ASC16 (8×16) glyphs.

- **Home** — status (clock, battery, version, free flash) + four guest slot cards.
  Pull down / tap the top bar for **Quick settings**.
- **Quick settings** — frontlight brightness + on/off, hour/minute adjust. Swipe up or Done to close.
- **Install** — file picker (best-fit). Empty slot **Assign** targets that slot.
- **Settings** — about + power off.

Launcher partition stays protected; install never overwrites an occupied slot.
