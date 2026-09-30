# GBCAMS — Wireless Camera Viewfinder for M5Cardputer

**English** · [中文](README_CN.md)

Turn an **M5Cardputer** + a **M5Stack UnitCamS3-5MP** camera unit into a low-latency wireless
viewfinder with Game Boy era pixel filters, SD capture and a photo gallery. No WiFi router, no
phone app — the camera streams straight to the handheld over ESP-NOW.

![Viewfinder with sidebar HUD](docs/screenshots/viewfinder-hud-320x200.png)

> The image above is a render of the real UI (title drawn with the device's own 5×7 font), not a photo.
> Real-hardware photos are welcome — see [ROADMAP](ROADMAP.md).

## Status / 现状

| Part | Version | State |
|---|---|---|
| Receiver — M5Cardputer `cardputer/` | `v0.8.8` | ✅ works, hardware-verified |
| Sender — UnitCamS3-5MP, ESP-NOW `cams3/espnow/` | `v0.0.9` | ✅ works, hardware-verified (recommended) |
| Sender — UnitCamS3-5MP, AP-HTTP `cams3/wifi/` | `v0.0.1` | ✅ works, hardware-verified (stock-app compatible) |

Measured end-to-end framerate on the Cardputer: **~16–17 fps** over ESP-NOW (sender captures up to
~25 fps; the receiver's JPEG decode + palette quantisation is the bottleneck), **~12 fps** over AP-HTTP.
These are measured numbers, not targets — the on-screen `FPS` field in the sidebar shows the live value.

## Features

| | |
|---|---|
| 📡 **Link** | ESP-NOW unicast, auto-discovery via beacon, auto-reconnect after link loss. No router needed. Frames of ~12 KB are split into 9 ESP-NOW v2.0 large packets (`PKT_DATA_MAX` 1450 B). |
| 🎨 **9 pixel filters** | `1` GB 8-shade grey · `2` Classic DMG green · `3` GBC-1 8×8 Bayer · `4` GBC-2 blue-noise · `5` GBA 15-bit · `6` BR-1 Backrooms wallpaper yellow · `7` BR-2 fluorescent · `8` DMG true 4-shade (#0F380F→#9BBC0F) · `9` passthrough |
| 🖼 **4 pixel frames** | DMG dot-matrix / GBC purple / CRT double-line / Polaroid. Orthogonal to filters (36 combinations), remembered in NVS. |
| 🖥 **Sidebar HUD** | Image 180×135 on the left, 60 px status column on the right: filter, frame, EV, link mode, FPS, drop counter (event), capture filename (event), `H HELP`. Nothing is drawn over the picture. |
| 📸 **Capture** | `ENTER` writes to SD: raw JPEG (filter off) or a 24-bit BMP with the filter baked in. Shutter click, filename echoed in the sidebar. |
| 🗂 **Gallery** | Thumbnail grid, multi-select delete, favourites, original↔styled comparison, 3 s slideshow, metadata view. |
| 🈶 **Bilingual help** | `H` opens 3 help pages in the viewfinder and 2 in the gallery (CN/EN, including a link-status diagnostic page). |
| 🔄 **Dual-mode** | Detects which sender firmware is running: ESP-NOW (blue `ESP-NOW` label) or the stock AP-HTTP path (yellow `AP` label). |

## Hardware

- **M5Cardputer** — receiver. ESP32-S3, 8 MB flash, **no PSRAM** (firmware is budgeted for its DRAM).
- **M5Stack UnitCamS3-5MP** — sender. ESP32-S3 + 8 MB PSRAM, PY260 / BF3005 sensor.
- **microSD card**, FAT32 (SPI mode) — for photos.
- 2 × USB-C cables, for flashing.

Software is built with PlatformIO (Arduino framework, ESP32-S3).

## Quick start

1. Flash both ends — see [BUILDING.md](BUILDING.md). M5Burner package: `release/README_M5Burner.md`,
   or grab the merged images from the [latest release](https://github.com/andjiang0083/gbcams/releases).
2. Power both up. The Cardputer shows a blue `ESP-NOW` label within ~3 s and the viewfinder appears.
3. `1`–`9` change filter, `0` toggles the frame, `,` `/` switch frame style, `-` `+` exposure,
   `ENTER` captures, `R` opens the gallery, `H` shows help. Full key map: [release/README.md](release/README.md).

## Repository layout

```
cardputer/       receiver firmware  (M5Cardputer: LovyanGFX/M5GFX + TJpgDec + efontCN_12)
cams3/espnow/    sender firmware    (UnitCamS3: ESP-NOW v2.0 large packets)
cams3/wifi/      sender firmware    (UnitCamS3: AP + HTTP MJPEG, stock-APP compatible)
release/         M5Burner package: manifest, 320×200 covers, sha256, end-user README
tools/           helper scripts (cover generator, blue-noise dither table)
docs/            protocol, stock-firmware compatibility, design & review reports
archive/         early prototype kept for history, not maintained
```

## Documentation

| Doc | What's in it |
|---|---|
| [BUILDING.md](BUILDING.md) | build & flash everything, plus the traps that produce a black screen |
| [release/README.md](release/README.md) | end-user manual: keys, filters, gallery, troubleshooting |
| [release/README_M5Burner.md](release/README_M5Burner.md) | M5Burner publishing sheet + paste-ready listing fields |
| [docs/protocol.md](docs/protocol.md) | wire protocol: packet layout, discovery beacon, limits |
| [docs/wifi-compat.md](docs/wifi-compat.md) | UnitCamS3 stock-firmware WiFi/API compatibility |
| [docs/design-v0.8.8.md](docs/design-v0.8.8.md) | v0.8.8 design: sidebar layout, bilingual help, soft frame recovery |
| [docs/fix-report-v0.8.7.md](docs/fix-report-v0.8.7.md) · [docs/code-review-v0.8.6.md](docs/code-review-v0.8.6.md) | code review findings and fixes (useful as a "what to watch out for" list) |
| [ROADMAP.md](ROADMAP.md) | concrete next steps, claim one by commenting on it |
| [CHANGELOG.md](CHANGELOG.md) | project-level history |

## Known limits

- Receiver-side CPU is the bottleneck (~16–17 fps); the sender can capture faster than the
  Cardputer can quantise and draw.
- A single frame larger than **32 KB** is dropped (the reassembly buffer is bounded). The receiver
  requests a fixed framesize/quality from the stock AP path at connect time.
- The 9 filters are fixed palettes; nothing is user-definable at runtime yet.
- Only JPEG streams from an ESP32 camera module (UnitCamS3-5MP class) are supported.
- No OTA update, no on-device WiFi credential UI: the AP-HTTP sender always comes up with the stock
  SSID `UnitCamS3-WiFi`.
- Portrait mode was attempted in v0.8.7 and removed in v0.8.8 — it looked bad on hardware. It will
  not come back without a different approach.
- Ship builds keep `Serial.println` diagnostics; the project's own acceptance testing is done **on
  the screen**, because USB CDC is unreliable while the radio is busy. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

Bug reports and PRs are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md) (project-specific
conventions) and [ROADMAP.md](ROADMAP.md) (what is actually wanted). Issue forms ask for the
evidence needed to locate a hardware problem, so please fill them in.

## Credits & license

Third-party code, fonts and their licenses: [CREDITS.md](CREDITS.md).
This project is **MIT** licensed — see [LICENSE](LICENSE).
