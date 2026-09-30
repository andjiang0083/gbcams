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

# ── 版本号来源: 各子项目的 VERSION 文件 (权威) ──
# 不用 git describe 的原因: 公开仓库里三端共用一个 release tag, describe 会把三端都报成
# 同一个版本号 (发送端 VERSION 被写成 v0.8.8)。VERSION 文件让开发树与克隆树得到同一串。
ver_of() {  # $1 = 子项目相对路径
  [ -f "$ROOT/$1/VERSION" ] && head -1 "$ROOT/$1/VERSION" || echo dev
}
CARD_VER="$(ver_of cardputer)"
ESP_VER="$(ver_of cams3/espnow)"
WIFI_VER="$(ver_of cams3/wifi)"
# tag 未指向 HEAD 时只告警 (工作树有未提交改动/未打 tag), 不影响版本串
for p in cardputer cams3/espnow cams3/wifi; do
  t="$(git -C "$ROOT/$p" describe --tags --abbrev=0 2>/dev/null || true)"
  if [ -n "$t" ] && [ "$(git -C "$ROOT/$p" rev-parse "$t" 2>/dev/null)" != "$(git -C "$ROOT/$p" rev-parse HEAD 2>/dev/null)" ]; then
    echo "  ⚠ $p: tag $t 不在 HEAD 上 (版本串仍按 VERSION 文件: $(ver_of $p))"
  fi
done

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
