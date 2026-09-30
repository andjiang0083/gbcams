# Contributing to GBCAMS

**English** · [中文](CONTRIBUTING_CN.md)

Small hobby-hardware project: issues and PRs are welcome, and the conventions below are the ones the
project actually uses. If a rule sounds odd, it is there because the opposite cost us a release cycle.

## The workflow this project uses

1. **Design first.** For anything visual, write down what changes and why *before* coding
   (`docs/design-v*.md` is the pattern). For multi-part changes, list the decisions you are unsure
   about and get them answered — guessing costs more.
2. **Verify on the PC before touching hardware.** UI/layout work is checked by rendering the screen
   layout off-device and doing the arithmetic: does anything overlap, does any row leave the screen,
   are the baselines aligned? See `tools/make_m5burner_covers.py` for how the device's own 5x7 font
   is parsed for that. Layout constants must come from measured device data (font header, glyph
   metrics), never from eyeballing — "looks aligned" is not evidence.
3. **Accept on the screen, not over serial.** USB CDC is unreliable while the radio is busy. Every
   change should be verifiable by looking at the device: sidebar `FPS`, drop counters, the `3/3`
   help page, gallery state, capture filename echo.
4. **Small steps, small version numbers.** Bump the last digit (`v0.8.7` → `v0.8.8`) even for a
   substantial UI change. Large version jumps are rejected in review. Tag the release commit and
   **tag before you build** — the firmware version string comes from `git describe`, so building
   before tagging bakes `v0.8.8-1-gabc1234` into the image.
5. **Bilingual by default.** Anything a user reads (help pages, M5Burner description, release notes)
   is written in Chinese *and* English. Long docs use parallel `_CN.md` files with cross-links.

## Code conventions worth knowing

- **One dispatch point per subsystem.** Filters are dispatched from `UIManager::renderFiltered()`;
  the viewfinder and the capture re-render both go through it. Adding a filter means adding one case
  — do not add a second switch somewhere else (that bug already happened once).
- **Keys are edge-detected.** Use `UIManager::keyEdge()` / `keyEdgeRepeat()`. Do not use
  `isKeyPressed()` in the main loop: holding a key would re-trigger every iteration.
- **NVS writes are batched.** Preferences are stored as a single blob, written on a debounce (or when
  leaving the screen), never per keypress — flash wear is real.
- **Nothing is drawn over the picture.** New status information goes in the 60 px sidebar. The
  sidebar strip is fully cleared every frame (a stale-HUD ghost was a real bug).
- **Frame reassembly lives in the ESP-Now callback**, and the receiver tolerates a single missing
  packet (soft recovery) instead of throwing the frame away.
- **Palette tables are built once** (`build_palettes()`), dithering is per-pixel with 16-bit colour.
- **Fixed-size buffers with explicit bounds.** The reassembly buffer is capped (32 KB); SD images are
  drawn through a row buffer whose source width is clamped.

## Testing

There is no CI yet (see [ROADMAP.md](ROADMAP.md)): build each environment you touched
(`pio run` in `cardputer/`, `cams3/espnow/`, `cams3/wifi/`) and physically verify on hardware.
When you open a PR, state what you ran and on which end — the PR template asks for exactly that.
Host-side helpers (`tools/`) are plain Python; run them before committing if you touched them.

## Do not commit

Binaries (`release/*/firmware.factory.bin`, `*.bin`), `.pio/` build trees, sd-card dumps, device
flash backups or logs. Binaries ship as release assets; the in-tree `*.sha256` is the record.

## Reporting a bug

Use the issue form — it asks for the firmware version string (from the boot log), which end, and
whether the device recovers by itself, because those three separate software bugs from hardware
faults. "It does not work" without them usually gets closed with a question.
