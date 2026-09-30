# Building & flashing GBCAMS

**English** · [中文](BUILDING_CN.md)

Three independent PlatformIO projects: `cardputer/` (receiver), `cams3/espnow/` and `cams3/wifi/` (senders).
Nothing else is needed — PlatformIO fetches the ESP32 toolchain and the Arduino framework.

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) (`pio`, Python 3.8+)
- two USB-C cables, M5Cardputer + UnitCamS3-5MP
- `esptool` (comes with PlatformIO, or `pip install esptool`) if you want to flash merged images

```bash
pio --version        # 6.x is what this project is developed with
```

## Build

```bash
cd cardputer        && pio run        # receiver
cd cams3/espnow     && pio run        # sender, ESP-NOW   (recommended)
cd cams3/wifi       && pio run        # sender, AP-HTTP   (stock-app compatible)
```

The full release package (all three + `VERSION` files + merged images) is one script:

```bash
./release/build.sh      # writes release/<target>/firmware.factory.bin (merged, flash_mode=dio)
python3 tools/make_m5burner_covers.py   # regenerates the 320x200 covers
```

Untracked build output lives in each project's `.pio/` directory. Binaries are intentionally **not**
committed; they ship as GitHub release assets, and `release/*/firmware.factory.bin.sha256` is in-tree.

## Dependencies are pinned on purpose

```ini
# cardputer/platformio.ini
m5stack/M5Unified@0.2.21      ; transitive deps M5GFX + IRremote are pinned too, on purpose
m5stack/M5GFX@0.2.28
m5stack/M5Cardputer@1.1.1
IRremote@4.7.1
# cams3/*/platformio.ini
https://github.com/espressif/esp32-camera.git#v2.0.0
```

Caret ranges (`^0.2.15`) silently resolve to whatever is newest on clone day, and the resulting
firmware is a *different build* from the released one. If you clone this repo and the built image
size does not match the release notes, check the resolved versions before anything else.

**PlatformIO double-copy trap.** When a transitive dependency also declares the library you pinned,
PlatformIO may install *both* copies — you will see `M5GFX` and `M5GFX@0.2.31` side by side in
`.pio/libdeps/`. That is a real difference in compiled code (we hit it: 350k bytes of `.text`
changed). Fix:

```bash
rm -rf cardputer/.pio/libdeps/m5cardputer/M5GFX*
cd cardputer && pio run -t clean && pio run
ls .pio/libdeps/m5cardputer/     # expect exactly one M5GFX directory, at the pinned version
```

## Flash

### PlatformIO (development)

```bash
cd cardputer      && pio run -t upload --upload-port /dev/cu.usbmodem101
cd cams3/espnow   && pio run -t upload --upload-port /dev/cu.usbmodem1101
```

### esptool with the merged image (same thing M5Burner does)

```bash
esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_size 8MB \
  0x0 release/cardputer/firmware.factory.bin
```

### M5Burner

Import `release/m5burner.json` (M5Burner → **Custom** → **Import Custom FW**) and burn each device.
Device type for the receiver listings is **Cardputer**, for the UnitCamS3 listings **TimerCamera**.

## The traps that give you a black screen

| Trap | Why it bites | Correct |
|---|---|---|
| Flashing `firmware.bin` (app-only) | M5Burner hardcodes offset `0x0`; the app image expects `0x10000`, so it overwrites the bootloader | Always publish `firmware.factory.bin` — PlatformIO 12+ produces a merged image (bootloader@0x0, partitions@0x8000, boot_app0@0xe000, app@0x10000) |
| `flash_mode` mismatch | The bootloader is compiled with a different flash mode than the merged header says → it starts but cannot bring up flash | `board_build.flash_mode = dio` in `platformio.ini` **and** `--flash_mode dio` when merging |
| Partition table at `0x9000` | ESP-IDF v5 layouts use `0x9000`; this project's bootloader (v4-era, via PlatformIO's pinned framework) looks at `0x8000` | merge with `0x8000` |
| `--pad-to-size` / `--fill-flash-size` | Inflates the image to the full 8 MB, pointless and confusing | don't use it; a correct merged image is ~1.0–1.6 MB |
| `Failed to connect to ESP32-S3: No serial data received` | the chip is asleep or the previous firmware does not release USB | hold **KEY1** (GPIO0), tap **RST**, keep holding KEY1, then upload |

Verify any merged image before publishing:

```bash
python3 - <<'EOF'
d = open('release/cardputer/firmware.factory.bin','rb').read()
print('size', len(d))
print('bootloader 0x0     ', hex(d[0]),     'expect 0xe9')
print('flash mode byte    ', hex(d[2]),     'expect 0x02 (DIO)')
print('partitions 0x8000  ', hex(d[0x8000]),'expect 0xaa')
print('app 0x10000        ', hex(d[0x10000]),'expect 0xe9')
EOF
```

## Verifying a build is the same build

Whole-file `sha256` of two builds of the same source **never** matches — the app descriptor carries
build date/time and an ELF hash. Compare machine code instead:

```bash
xtensa-esp32s3-elf-objcopy -O binary --only-section='.flash.text*' -j '.text*' \
  .pio/build/m5cardputer/firmware.elf a.bin   # repeat in the other tree, then
shasum a.bin b.bin
```

Same segments + same size = same firmware, regardless of the timestamps.

### Path strings shift the code (and fool your diff)

The same source built from `.pio/libdeps/m5cardputer/M5GFX` and from
`.pio/libdeps/m5cardputer/M5GFX@0.2.28` (same M5GFX *version*) produces binaries whose `.text`
differs by ~350,000 bytes — because library sources embed their own path via `__FILE__` in asserts
and log statements, one string gets longer, and everything after it shifts. Sizes stay identical and
no code changed. If you see a huge diff like that, compare the `strings` dumps first: the real
differences will be a handful of paths and the build time. Compare `.flash.text` (see above) rather
than raw bytes.

## Resource budget (receiver, M5Cardputer)

The Cardputer has **no PSRAM**, so everything is budgeted against DRAM. Current release image:
**1,596,064 B** for the merged receiver image, of which the embedded `efontCN_12` Chinese font is
~151 KB. Adding a large font, an extra framebuffer or a bigger reassembly buffer will not fit without
giving something up — measure before you assume.

## No-serial acceptance

The project's own testing is done on the screen (USB CDC is unreliable while WiFi/ESP-NOW is busy):
sidebar `FPS`, the drop counter, the `3/3` help page (`H`, then `TAB` twice) which shows
`T`/`O`/`S` frame counters, the gallery, and the capture filename echo. See
[CONTRIBUTING.md](CONTRIBUTING.md).
