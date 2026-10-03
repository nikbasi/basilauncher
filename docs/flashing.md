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

esptool.py --chip esp32s3 -p "$PORT" write-flash 0x0 basilauncher-1.4.44-full.bin
```

Rebuild a full image after `pio run`:

```bash
./scripts/make_full_image.sh
# → .pio/build/basilauncher/basilauncher-<ver>-full.bin
```

This overwrites the bootloader and partition table. Otadata (`0xe000`) is already
`0xFF` inside the merged image, so the board boots Basilauncher. Guest slots
beyond the end of the full image (~640 KB) are not rewritten.

## What “erase otadata” means

Otadata at **`0xe000`** (8 KB) stores which OTA slot to boot. Everyday hub upload
(`pio run -e basilauncher -t upload`) clears it automatically after writing
factory. Double-press **RST** also returns to the hub. Manual clear if needed:

```bash
esptool.py --chip esp32s3 -p PORT erase-region 0xe000 0x2000
```

## Custom bootloader only

Leaves apps, slots, and otadata alone:

```bash
python3 -m esptool --chip esp32s3 -p "$PORT" write-flash \
  0x0 bootloader/basil-bootloader.bin
```

Rebuild the committed `bootloader/basil-bootloader.bin`:

```bash
cd bootloader
pio run
# Copy the IDF second-stage image into the repo:
cp .pio/build/bootloader/bootloader.bin basil-bootloader.bin
# Or flash it directly: pio run -t flashbl
```

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

Push `.bin` files to the SD card over USB (device must be running Basilauncher):

```bash
python3 scripts/sd_push.py --dir /path/to/bins
# or: python3 scripts/sd_push.py path/to/app.bin …
```
