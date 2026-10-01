# Flashing Basilauncher

Basilauncher must live in the **factory** app partition (`0x10000`). Guest apps
use `ota_0`…`ota_3` only. The PlatformIO upload target is intentionally
factory-only so day-to-day updates do not wipe guests.

## Everyday update (recommended)

```bash
pio run -e basilauncher -t upload
```

This writes firmware to **`0x10000`** and erases otadata (`0xe000`) so the next
boot lands on Basilauncher. Bootloader and partition table are **not** rewritten.

## First install — one file (easiest)

From [Releases](https://github.com/nikbasi/basilauncher/releases), download
`basilauncher-*-full.bin` (custom bootloader + partition table + factory app
merged). Flash it at offset **0**, then clear otadata:

```bash
PORT=/dev/cu.usbmodem101   # Windows: COMx

esptool.py --chip esp32s3 -p "$PORT" write-flash 0x0 basilauncher-1.4.23-full.bin
esptool.py --chip esp32s3 -p "$PORT" erase-region 0xe000 0x2000
```

Rebuild a full image after `pio run`:

```bash
./scripts/make_full_image.sh
# → .pio/build/basilauncher/basilauncher-<ver>-full.bin
```

This overwrites the bootloader and partition table. Guest slots that already
exist are left alone if their offsets still match; on a blank board it is the
recommended path.

## First install / partition map change (three files)

When you prefer separate binaries (or changed [`partitions.csv`](../partitions.csv)
and want an explicit table rewrite):

```bash
pio run -e basilauncher   # produce .pio/build/basilauncher/*

PORT=/dev/cu.usbmodem101   # Windows: COMx

python3 -m esptool --chip esp32s3 -p "$PORT" write-flash \
  0x0     bootloader/basil-bootloader.bin \
  0x8000  .pio/build/basilauncher/partitions.bin \
  0x10000 .pio/build/basilauncher/firmware.bin

python3 -m esptool --chip esp32s3 -p "$PORT" erase-region 0xe000 0x2000
```

Use the **custom** [`bootloader/basil-bootloader.bin`](../bootloader/basil-bootloader.bin)
(RST double-press). Arduino’s stock `boot_app0` / default bootloader will not
implement the return-to-hub gesture.

## Custom bootloader only

Leaves apps, slots, and otadata alone:

```bash
python3 -m esptool --chip esp32s3 -p "$PORT" write-flash \
  0x0 bootloader/basil-bootloader.bin
```

Rebuild notes live in the bootloader folder README section of the main README
(`cd bootloader && pio run`, then `ninja` / `elf2image` as documented there).
The image must stay under **28 KB** so the flag sector at `0x7000` remains free.

## Do not

- Flash a guest (CrossPoint, Meshtastic, …) to `0x0` / `0x10000` with a full
  PlatformIO default upload — that clobbers the hub.
- Erase all flash casually if you care about installed guests.

## Serial

`115200` baud, USB CDC. Example:

```bash
pio device monitor -e basilauncher
```
