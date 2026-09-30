# Screenshots

| File | What it is |
|---|---|
| `viewfinder-hud-320x200.png` | v0.8.8 viewfinder: 180×135 picture on the left, 60 px sidebar HUD on the right (rendered, filter `DMG`, frame `CRT`) |
| `sender-espnow-cover-320x200.png` | ESP-NOW sender illustration + specs (rendered) |
| `sender-ap-cover-320x200.png` | AP-HTTP sender illustration + specs (rendered) |
| `ui-layout-check.png` | Layout audit sheet: mixed CN/EN baseline alignment before/after the v0.8.8 fix, plus the key-uppercase pass |
| `design-report-v0.8.8.png` | v0.8.8 design report sheet (sidebar options, geometry) |

⚠️ **All of these are renders, not photographs.** The screen content is drawn off-device by
`tools/make_m5burner_covers.py` (titles use the device's own 5×7 font, parsed from M5GFX's
`glcdfont.h`), and the camera frame inside the mocked viewfinder is a synthetic scene. They show the
layout faithfully, but they are not evidence of on-device behaviour — see
[ROADMAP](../../ROADMAP.md) item 7 for replacing them with real photos.
