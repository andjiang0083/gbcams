#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════
# GBCAMS — Release build script (build.sh)
#
# 自动构建两端固件并更新 release/ 目录:
#   1. 编译 cardputer (双模 v0.8.x) — 版本注入 git describe
#   2. 编译 cams3/espnow (v0.0.6) — ESP-NOW 发送端 (推荐, 25fps)
#   3. 编译 cams3/wifi  (v0.1.x) — AP HTTP 发送端 (兼容官方 App, 12fps)
#   4. 复制 firmware.factory.bin (merged 全镜像) → release/<target>/, 校验 VERSION 指纹
#      ⚠️ 必须用 PIO 的 firmware.factory.bin (bootloader@0x0+partitions+boot_app0+app)。
#      复制 app-only 的 firmware.bin 会让 M5Burner 烧 0x0 后黑屏 (skill: m5burner-firmware-publishing)
#   5. 更新 m5burner.json 版本号 (需要手动确认变更)
#
# 用法: ./build.sh [--skip-upload] [--tag]
# ═══════════════════════════════════════════════════════════════
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
export PATH="$HOME/.platformio/penv/bin:$PATH"

CARD_VER="$(git -C "$ROOT/cardputer" describe --tags --always 2>/dev/null || echo dev)"
ESP_VER="$(git -C "$ROOT/cams3/espnow" describe --tags --always 2>/dev/null || echo dev)"
WIFI_VER="$(git -C "$ROOT/cams3/wifi" describe --tags --always 2>/dev/null || echo dev)"

echo "═══ GBCAMS Release Build ═══"
echo "  cardputer : $CARD_VER"
echo "  cams3/esp : $ESP_VER"
echo "  cams3/wifi: $WIFI_VER"
echo

# ── 1. Cardputer ──
echo "[1/3] Building cardputer (dual-mode) ..."
( cd "$ROOT/cardputer" && CAMERA_VERSION="$CARD_VER" pio run >/dev/null 2>&1 )
cp "$ROOT/cardputer/.pio/build/m5cardputer/firmware.factory.bin" "$ROOT/release/cardputer/firmware.factory.bin"
echo "$CARD_VER" > "$ROOT/release/cardputer/VERSION"
cp "$ROOT/cardputer/.pio/build/m5cardputer/partitions.bin" "$ROOT/release/cardputer/partitions.bin" 2>/dev/null || true

# ── 2. CAMS3 ESP-NOW ──
echo "[2/3] Building cams3/espnow ..."
( cd "$ROOT/cams3/espnow" && pio run >/dev/null 2>&1 )
cp "$ROOT/cams3/espnow/.pio/build/cams3/firmware.factory.bin" "$ROOT/release/cams3/firmware.factory.bin"
echo "$ESP_VER" > "$ROOT/release/cams3/VERSION"
cp "$ROOT/cams3/espnow/.pio/build/cams3/partitions.bin" "$ROOT/release/cams3/partitions.bin" 2>/dev/null || true

# ── 3. CAMS3 WiFi (AP HTTP) ──
echo "[3/3] Building cams3/wifi ..."
( cd "$ROOT/cams3/wifi" && pio run >/dev/null 2>&1 )
WIFI_BIN="$ROOT/release/cams3-wifi/firmware.factory.bin"
mkdir -p "$(dirname "$WIFI_BIN")"
cp "$ROOT/cams3/wifi/.pio/build/cams3/firmware.factory.bin" "$WIFI_BIN"
echo "$WIFI_VER" > "$ROOT/release/cams3-wifi/VERSION"

echo
echo "═══ 产物校验 (VERSION 指纹) ═══"
for d in cardputer cams3 cams3-wifi; do
  if [ -f "$ROOT/release/$d/VERSION" ]; then
    V="$(cat "$ROOT/release/$d/VERSION")"
    SZ="$(stat -f%z "$ROOT/release/$d/firmware.factory.bin" 2>/dev/null || stat -c%s "$ROOT/release/$d/firmware.factory.bin")"
    echo "  $d : $V  ($SZ bytes)"
  fi
done

echo
echo "✅ 构建完成。记得更新 release/m5burner.json 版本号 (如需)。"
