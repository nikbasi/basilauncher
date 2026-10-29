# Basilauncher

**Multi-app firmware hub for the [LilyGO T5 E-Paper S3 Pro](https://www.lilygo.cc/)**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
[![Board](https://img.shields.io/badge/board-LilyGO%20T5%20E--Paper%20S3%20Pro-black.svg)](https://github.com/Xinyuan-LilyGO/T5-e-paper-s3)
[![Version](https://img.shields.io/badge/version-1.4.62-green.svg)](platformio.ini)

Basilauncher is a **factory-partition launcher**: it boots first after reset, lets you
install guest firmwares from the SD card into flash slots, and comes back when you
**double-press RST** — without patching those guests.

If Basilauncher is useful on your T5 Pro, a GitHub star helps others find it.

## Download

Pre-built images: **[Releases](https://github.com/nikbasi/basilauncher/releases)** (latest: [v1.4.62](https://github.com/nikbasi/basilauncher/releases/tag/v1.4.62)).

**Easiest install:** open the **[web flasher](https://nikbasi.github.io/basilauncher/flash/)** (Chrome/Edge), plug in the T5 Pro, click **Connect & flash**.

**Wi-Fi transfer (on device):** pull down the shade and tap the **Wi-Fi** tile to start the hotspot. Hold the tile for the password and QR. The network is `Basilauncher` with password `basilauncher` until you change it (on the device, or on the transfer page). Scan the Join Wi-Fi QR, then scan Open page / visit `http://192.168.4.1/`. Browse any SD folder and upload one or more files (e.g. `/firmware` for apps, `/sleep` for `.bmp`/`.jpg` wallpapers). The tile and the status-bar icon fill in while the hotspot is on.

| Asset | Offset | When |
|-------|--------|------|
| **`basilauncher-*-full.bin`** | **`0x0`** | **First install — one file** (also what the web flasher uses) |
| `basilauncher-*-t5pro.bin` | `0x10000` | Everyday hub update (guests untouched) |
| `basil-bootloader.bin` | `0x0` | Bootloader-only refresh |
| `basilauncher-*-partitions.bin` | `0x8000` | Partition table only |

**First install via esptool (one file):**

```bash
esptool.py --chip esp32s3 -p PORT write-flash 0x0 basilauncher-1.4.62-full.bin
```

Full recipes: **[docs/flashing.md](docs/flashing.md)** · **[Web flasher](https://nikbasi.github.io/basilauncher/flash/)**.

## Hardware

| | |
|---|---|
| **Board** | LilyGO T5 E-Paper S3 Pro (`T5-ePaper-S3-Pro`, ESP32-S3 + 8 MB PSRAM) |
| **Panel** | 960×540 e-paper (UI is portrait 540×960) |
| **Touch** | GT911 |
| **Power** | BQ25896 charger + fuel gauge |
| **Storage** | microSD (apps under `/firmware`, sleep art under `/sleep`) |

Also searchable as: *LilyGO T5 S3 Pro*, *T5 epaper S3 Pro*, *ESP32-S3 e-ink launcher*.

## Features

- **Four guest slots** (`ota_0`…`ota_3`) — best-fit install from `/firmware/*.bin`
- **Home that knows your apps** — names and versions from each image, a Continue card for the
  last app you opened, and an update badge when `/firmware` holds a newer build of an installed app.
  Hold a card (or tap ⋮) to open, update, rename or clear it
- **Protected factory hub** — on-device UI never overwrites the launcher
- **RST double-press** custom bootloader — return from any guest without app changes
- **Files explorer** — browse SD with tappable breadcrumbs, install bins, view BMPs, edit text, clipboard,
  multi-select from the dock
- **Quick settings shade** — frontlight, scrub, jump to Settings
- **Back everywhere** — the arrow in each screen's header, or swipe in from the left edge
- **Sleep screensaver** — random BMPs from `/sleep` or `/.sleep`, short BOOT to change, hold BOOT to wake
- **Clock + battery** — RTC time, charge lightning / USB plug cues
- **1 MB `spiffs`** — so Meshtastic-style guests can mount InternalFS

## Screenshots

<p align="center">
  <img src="docs/images/home.jpg" alt="Basilauncher home — four guest slots" width="240" />
  &nbsp;
  <img src="docs/images/shade.jpg" alt="Basilauncher Quick settings shade" width="240" />
  &nbsp;
  <img src="docs/images/sleep.jpg" alt="Basilauncher sleep screensaver" width="240" />
</p>

<p align="center"><em>Home · Quick settings · Sleep</em></p>

## Quick start

```bash
git clone --recurse-submodules https://github.com/nikbasi/basilauncher.git
cd basilauncher
pio run -e basilauncher
pio run -e basilauncher -t upload   # factory @ 0x10000 only; guests untouched
```

**First flash on a blank board** (partition table + custom bootloader once):

```bash
# See docs/flashing.md for the full recipe and warnings.
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 write-flash \
  0x0     bootloader/basil-bootloader.bin \
  0x8000  .pio/build/basilauncher/partitions.bin \
  0x10000 .pio/build/basilauncher/firmware.bin
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 erase-region 0xe000 0x2000
```

More detail: **[docs/flashing.md](docs/flashing.md)** · **[docs/partitions.md](docs/partitions.md)** · **[docs/guest-apps.md](docs/guest-apps.md)**

## Returning to the launcher

| Reset | Boots |
|-------|-------|
| **RST pressed twice** within ~0.8 s | Basilauncher (otadata cleared) |
| RST once / power-on | Same app (short wait after first press) |
| USB host reset, app restart, deep-sleep wake, WDT, crash | Same app, no wait |

Double-press **RST** from any guest to get back here.

## SD card layout

```
/firmware/          # .bin images to install into empty slots
  crosspoint-….bin
  meshtastic-….bin
  …
/sleep/             # optional portrait BMPs for the screensaver
  01_forest.bmp
```

Tap a file in **Files** → installs into the **smallest empty slot that fits**.
Clear a slot from the home cards when you need space.

## Build notes

Uses FreeInk display path: `BoardT5S3` + `LgfxEpd` + M5GFX (not FastEPD — FastEPD’s
T5 Pro panel path drives the frontlight pin as data).

```ini
; platformio.ini upload writes ONLY factory @ 0x10000, then clears otadata
```

SDK: submodule [`nikbasi/freeink-sdk`](https://github.com/nikbasi/freeink-sdk) (FreeInk-derived, with LilyGO T5 charge / clean-bank fixes used by this hub).

**Updating the SDK.** Some of what keeps the panel fast and ghost-free came from
the `aurora` line or from T5 fixes that upstream FreeInk does not carry: the
2×L15 clean refresh, deferred window and frame refreshes, queued long presses,
the staged rail park and the warm-panel waveform floor. An upstream merge can
drop one without breaking a call site, so the build checks for them:
`src/sdk_requirements.h` stops on a missing SDK feature marker, and
`scripts/check_sdk.py` (run before every build, or by hand) checks the clean
bank's frame counts and the clean-refresh normalize. If either fails after a
submodule bump, resolve the SDK merge in favour of the listed feature; the list
lives in `freeink-sdk/docs/launcher-features.md`.

**UI preview without the device.** `tools/ui_preview/run.sh` builds the real `src/ui.cpp`
and `src/canvas.cpp` on your computer against a fake device and writes every screen (at
each text size) to PNGs plus contact sheets in `tools/ui_preview/out/`. It also runs a
tap test that checks each control's hit area matches what is drawn. Needs a C++20
compiler; Pillow turns the output into PNGs.

UI icons are [Lucide](https://lucide.dev) icons rendered to 1-bit bitmaps at each size
(`src/ui_icons.h`, regenerate with `scripts/gen_ui_icons.py`; license in `fonts/`).
UI text uses JetBrains Mono rendered to 1-bit bitmaps at each cell size
(`src/ui_font.h`, regenerate with `python3 scripts/gen_ui_font.py > src/ui_font.h`;
font license in `fonts/`).

## License

MIT — see [LICENSE](LICENSE). UI font: JetBrains Mono, Apache-2.0 (see [fonts/](fonts/)). FreeInk SDK is MIT (FreeInk / OpenX4 lineage); M5GFX has its own license via PlatformIO.

## Related

- [LilyGO T5 e-Paper S3](https://github.com/Xinyuan-LilyGO/T5-e-paper-s3)
- [FreeInk](https://freeink.org/) / [freeink-sdk](https://github.com/nikbasi/freeink-sdk)
- Guest examples often used on this board: CrossPoint reader, Meshtastic InkHUD, Flashcards, GameBoy ports
