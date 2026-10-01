# Partition map (16 MB)

From [`partitions.csv`](../partitions.csv):

| Name | Type | Offset | Size | Role |
|------|------|--------|------|------|
| nvs | data | `0x9000` | 20 KB | NVS |
| otadata | data | `0xe000` | 8 KB | Which OTA app boots |
| **factory** | app | `0x10000` | **1.5 MB** | **Basilauncher (protected)** |
| ota_0 | app | `0x190000` | 6 MB | Guest A (large apps) |
| ota_1 | app | `0x790000` | 2.5 MB | Guest B |
| ota_2 | app | `0xA10000` | 2.5 MB | Guest C |
| ota_3 | app | `0xC90000` | 2.25 MB | Guest D |
| spiffs | data | `0xED0000` | 1 MB | LittleFS / Meshtastic InternalFS |
| coredump | data | `0xFD0000` | 192 KB | Crash dumps |

## Design notes

- **Factory is never written by the on-device UI.** Install / Clear only touch
  empty or selected OTA slots.
- **Best-fit install** picks the smallest empty slot that fits the `.bin` size.
- **`spiffs`** is required for guests that mount a Meshtastic-style internal
  filesystem; without it they often hang at “Booting 100%”.
- Bootloader image must remain **< 28 KB** so `0x7000` can hold the RST
  double-press flag word.

See also [guest-apps.md](guest-apps.md) and [flashing.md](flashing.md).
