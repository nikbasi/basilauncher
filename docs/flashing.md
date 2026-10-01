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

## First install — web flasher (easiest)

Open **[https://nikbasi.github.io/basilauncher/flash/](https://nikbasi.github.io/basilauncher/flash/)** in **Chrome** or **Edge**, plug in the T5 Pro, click **Connect & flash**.

That writes `basilauncher-*-full.bin` at `0x0` (custom bootloader + partitions + factory). The merged image already fills otadata (`0xe000`) with `0xFF`, so the board boots Basilauncher without a separate erase step.

## First install — one file (esptool)

From [Releases](https://github.com/nikbasi/basilauncher/releases), download
`basilauncher-*-full.bin`:

```bash
PORT=/dev/cu.usbmodem101   # Windows: COMx

esptool.py --chip esp32s3 -p "$PORT" write-flash 0x0 basilauncher-1.4.23-full.bin

# Clears the OTA boot pointer (factory vs guest). Recommended after any
# three-file flash; optional after full.bin (already 0xFF in that region):
esptool.py --chip esp32s3 -p "$PORT" erase-region 0xe000 0x2000
```

Rebuild a full image after `pio run`:

```bash
./scripts/make_full_image.sh
# → .pio/build/basilauncher/basilauncher-<ver>-full.bin
```

This overwrites the bootloader and partition table. Guest slots beyond the
end of the full image (~640 KB) are not rewritten.

## What “erase otadata” means

Otadata at **`0xe000`** (8 KB) stores which OTA slot to boot. If it still points
at a guest, reset won’t return to Basilauncher until you clear it:

```bash
esptool.py --chip esp32s3 -p PORT erase-region 0xe000 0x2000
```

PlatformIO hub upload (`pio run -e basilauncher -t upload`) does this automatically
after writing factory.

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
