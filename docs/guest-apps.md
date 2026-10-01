# Guest apps

Basilauncher is **reboot-gated**: you pick an app, it boots into that OTA slot,
and you stay there until reset. Double-press **RST** to erase otadata and return
to the hub. Guests do **not** need a “back to launcher” button.

## Installing

1. Copy `.bin` files to the SD card under `/firmware/`.
2. On the device: **Files** → open the bin → confirm install.
3. Or from a home slot: **Assign** / empty-slot install flow.
4. Tap the card to **Boot** (sets otadata + restart).

Clear a slot from its home card when you need the space back.

## Typical guests on T5 S3 Pro

| Kind | Notes | Slot hint |
|------|-------|-----------|
| Large readers (e.g. CrossPoint) | Often ~3–5 MB+ | Prefer **ota_0** (6 MB) |
| Meshtastic (InkHUD / e-paper builds) | Needs **`spiffs`** partition | Medium slots OK if size fits |
| Flashcards / GameBoy / small tools | Usually fit **ota_1…3** | Best-fit will choose |

Exact binary names vary by project; Basilauncher does not hard-code catalog
URLs — anything valid ESP32-S3 app image in `/firmware/` can be tried.

## Sleep art

Optional portrait BMPs (logical **540×960**) in `/sleep/` or `/.sleep/`.
While asleep, short **BOOT** cycles images; hold **BOOT** to wake.

## Compatibility caveats

- Guests flashed with a **full** esptool image that includes bootloader +
  partitions will fight this map — prefer **app-only** bins for SD install.
- Apps that assume they own **factory** or a different table need a rebuild
  against this layout (or a dedicated device).
- Power / frontlight / SD pinouts match LilyGO’s T5 S3 Pro; other T5 variants
  are not validated here.
