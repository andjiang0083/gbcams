# Roadmap

**English** · [中文](ROADMAP_CN.md)

Concrete, sized next steps. Comment on an item (or open an issue referencing it) to claim it — say
which hardware you have, because most of these want a real Cardputer + UnitCamS3 to be worth doing.
Items are ordered by "usefulness ÷ effort", not by ambition.

1. **Sender-side adaptive quality.** A frame above ~32 KB is dropped by the receiver (bounded
   reassembly buffer) and currently just shows up as loss. Have the sender lower JPEG quality (or
   framesize) when a frame overflows, so a bad scene degrades instead of dropping.
   *Files:* `cams3/espnow/src/main.cpp` (quality control), `cardputer/src/main.cpp` (reassembly
   counter already tracks over-limit frames as `O`). *Hardware:* sender + receiver.

2. **Link diagnostics on screen.** Today the sidebar shows `FPS` + an event-driven drop counter and
   the `3/3` help page shows `T`/`O`/`S` counters. Add a short history (last 30 s) so intermittent
   trouble is visible without watching the counter at the right moment.
   *Files:* `cardputer/src/UIManager.cpp`. *Hardware:* receiver.

3. **Gallery ordering + EXIF time.** List by capture time instead of enumeration order, and show the
   timestamp in the info view. *Files:* `cardputer/src/UIManager.cpp` (gallery), SD layer.
   *Hardware:* receiver + SD card.

4. **Two senders, one receiver.** Switch between two UnitCamS3 units (the discovery beacon already
   carries an id) — useful for two rooms / two angles. *Files:* both ends, protocol doc.
   *Hardware:* 2 × sender + receiver.

5. **CI for the three environments.** GitHub Actions matrix that runs `pio run` for
   `cardputer`, `cams3/espnow`, `cams3/wifi` on every PR. No hardware needed, and it would have
   caught the dependency-resolution drift described in [BUILDING.md](BUILDING.md).
   *Hardware:* none — good first contribution.

6. **Full English help pages.** The help system is bilingual but the English lines are terse.
   Same for the release README. *Hardware:* receiver (for screenshots).

7. **Real hardware photos.** `docs/screenshots/` currently holds renders, not photos. A good shot of
   the viewfinder with the sidebar HUD in daylight would improve the README a lot.
   *Hardware:* receiver + sender.

8. **Host-side tests for the pixel maths.** Extract the dither/palette quantisation into pure
   functions and test them from Python/C on the host (golden images). The blue-noise table generator
   in `tools/` is already host-side. *Hardware:* none.

9. **Idle behaviour / power.** The receiver currently renders at full speed forever. Desk use
   (a persistent viewfinder) would benefit from an idle mode: lower frame rate, dimmed HUD, or
   screen-off after inactivity. *Hardware:* receiver.

10. **Second receiver target.** The protocol does not care about the display; a smaller screen
    (e.g. M5StickS3-size) or an e-paper/RLCD variant would be a different renderer, same wire
    format. *Hardware:* whatever you want to port it to.

## Not planned

- **Portrait "anime" mode.** Tried in v0.8.7, removed in v0.8.8 — it looked bad on real hardware.
  Would need a fundamentally different approach to be worth another attempt.
- **On-device WiFi credential UI.** The AP-HTTP sender intentionally keeps the stock SSID/API so the
  official app still works; adding a config UI means a display + keyboard on the camera side.
