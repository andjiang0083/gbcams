# M5Burner 发行说明 / M5Burner Release Sheet

**GBCAMS** — 无线取景器 (接收端) + UnitCamS3 发送端
> 项目代号 2026-09-30 起统一为 **GBCAMS**（原名 Cardputer Camera / Cardputer GB Cam）
**版本 / Versions**: Cardputer `v0.8.8` · UnitCamS3 ESP-NOW `v0.0.9` · UnitCamS3 AP-HTTP `v0.0.1`
**日期 / Date**: 2026-09-30

---

## 1. 包里有什么 / What's in this folder

| 装到哪台设备 / Target | 镜像 / Image | 版本 | SHA256 (前 16) |
|---|---|---|---|
| **M5Cardputer** (接收端 Receiver) | `cardputer/firmware.factory.bin` (1,596,160 B) | v0.8.8 | `2f28da03eff73aba…` |
| **UnitCamS3-5MP** (发送端 Sender, 推荐) | `cams3/firmware.factory.bin` (1,058,320 B) | v0.0.9 | `53b8dccf0bf056d0…` |
| **UnitCamS3-5MP** (兼容官方 App 的发送端) | `cams3-wifi/firmware.factory.bin` (1,094,304 B) | v0.0.1 | `095d6f77dea9ee20…` |

封面 / Covers: `<target>/cover-320x200.png` (320×200, M5Burner 规格)
校验值 / Checksums: `<target>/firmware.factory.bin.sha256`

> ⚠️ **必须用 `firmware.factory.bin`（merged 全镜像）**。M5Burner 固定把固件烧到 flash `0x0`，
> app-only 的 `firmware.bin` 会覆盖 bootloader → 黑屏。本目录里的都是 merged 镜像
> (bootloader@0x0 + 分区表@0x8000 + boot_app0@0xe000 + app@0x10000, DIO)。
>
> ⚠️ **Always use the merged image.** M5Burner always writes at flash offset `0x0`; an app-only
> binary would overwrite the bootloader and brick the boot (black screen).

---

## 2. 刷机 / Flashing

### M5Burner (推荐 / Recommended)
1. 打开 M5Burner → 登录 → **Custom** 标签 → **Import Custom FW** → 选本目录的 `m5burner.json`
2. 两台设备各自插 USB，分别选条目 → **Burn**
3. 注意设备类别：Cardputer 条目 → **Cardputer**；UnitCamS3 条目 → **TimerCamera**

### esptool (命令行 / CLI)
```bash
# 接收端 Cardputer
esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_size 8MB \
  0x0 cardputer/firmware.factory.bin

# 发送端 UnitCamS3-5MP
esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_size 8MB \
  0x0 cams3/firmware.factory.bin
```

刷完两台都上电：Cardputer 会自己发现发送端（右上角出现蓝色 `ESP-NOW` 标签），3 秒内出画面。

---

## 3. 按键 / Controls

### 取景 / Viewfinder
| 键 Key | 作用 Function |
|---|---|
| `1`–`9` | 滤镜 / Filters: `1`GB 8阶灰 · `2`Classic DMG绿 · `3`GBC-1 Bayer · `4`GBC-2 蓝噪 · `5`GBA 15位色 · `6`BR-1 旧壁纸黄 · `7`BR-2 荧光灯 · `8`DMG 真4阶绿 · `9`原图 |
| `0` | 相框开关 / Frame on-off |
| `,` `/` | 相框换款 (DMG点阵 / GBC紫 / CRT双线 / Polaroid) |
| `-` `+` | 曝光补偿 EV −3…+3 (长按连发) |
| `ENTER` | 快门 — 存 SD / Shutter |
| `R` | 相册 / Gallery |
| `H` | 帮助 (3 页, 中英双语) / Help |

### 相册 / Gallery
| 键 Key | 作用 Function |
|---|---|
| `A` `D` `W` `S` | 移动 / Move |
| `TAB` / `E` | 下页 / 上页 |
| `ENTER` | 打开 / Open |
| `BS` | 删除 (Enter 确认) · 多选时删选中 |
| `X` | 多选 / Multi-select |
| `V` | 全部 ↔ 收藏 / All ↔ Favorites |
| `T` | 补齐本页缩略图 / Rebuild thumbs |

### 查看 / Photo viewer
| 键 Key | 作用 Function |
|---|---|
| `A` `D` | 上一张 / 下一张 |
| `O` | 原图 ↔ 效果 对比 |
| `I` | 信息 (编号/格式/尺寸) |
| `P` | 幻灯片 (3 秒自动) |
| `F` | 收藏 / Favorite |
| `BS` | 删除 · `Q` 回列表 · `H` 帮助 |

> 模式（ESP-NOW / 官方 AP）**自动切换**，没有切换键。侧栏彩色标签即当前模式。

---

## 4. 发布表单字段值（可直接粘贴）/ Paste-ready publish fields

### 条目 A — 接收端 / Receiver
| 字段 Field | 值 Value |
|---|---|
| Name | `GBCAMS Receiver (M5Cardputer)` |
| Version | `0.8.8` |
| Device Type | **Cardputer** (M5Cardputer) |
| Author | `andjiang` |
| Github | 留空（项目未开源）/ leave empty |
| Cover | `cardputer/cover-320x200.png` |
| FirmWare | `cardputer/firmware.factory.bin` |

**Description (粘贴这段):**
```
[GBCAMS] Wireless camera viewfinder for M5Cardputer. Pair with a UnitCamS3-5MP sender — ESP-NOW,
no WiFi router needed. ~25fps, auto-discovery, 9 pixel filters (incl. real DMG 4-shade
green), 4 pixel frames, SD capture + gallery, bilingual CN/EN help.

M5Cardputer 无线相机取景器：配一块 UnitCamS3-5MP 发送端，走 ESP-NOW，无需路由器，约 25fps，
自动发现。9 种像素滤镜（含真 DMG 四阶绿）、4 款像素相框、SD 拍照 + 相册管理、中英双语帮助。

| key | 取景 / viewfinder |
|------|------|
| `1`-`9` | 滤镜 filters (GB / Classic / GBC-1 / GBC-2 / GBA / BR-1 / BR-2 / DMG / NORMAL) |
| `0` `,` `/` | 相框 frame on-off / 换款 switch style |
| `-` `+` | 曝光 EV |
| `ENTER` | 快门 shutter (save to SD) |
| `R` | 相册 gallery |
| `H` | 帮助 help (bilingual) |

| key | 相册/查看 gallery & viewer |
|------|------|
| `A D W S` | 移动 move |
| `TAB` / `E` | 翻页 page |
| `ENTER` | 打开 open |
| `BS` `X` | 删除 delete · 多选 multi-select |
| `V` `T` | 全部/收藏 all-fav · 补缩略图 thumbs |
| `O` `I` `P` `F` `Q` | 对比 compare · 信息 info · 幻灯 slideshow · 收藏 fav · 返回 back |

HUD sidebar shows filter / frame / EV / mode (ESP-NOW or AP) / FPS / drop & capture events.
```

### 条目 B — 发送端（推荐）/ Sender (ESP-NOW)
| 字段 Field | 值 Value |
|---|---|
| Name | `GBCAMS Sender ESP-NOW (UnitCamS3-5MP)` |
| Version | `0.0.9` |
| Device Type | **TimerCamera** (UnitCamS3-5MP 属此类别；全网同类固件都在 timercam) |
| Author | `andjiang` |
| Github | 留空 |
| Cover | `cams3/cover-320x200.png` |
| FirmWare | `cams3/firmware.factory.bin` |

**Description:**
```
[GBCAMS] ESP-NOW sender firmware for UnitCamS3-5MP. Streams MJPEG to a M5Cardputer receiver
without any WiFi router — ~25fps, 12KB frames packed into 9 ESP-NOW v2.0 large packets,
auto-discovery beacon, auto-reconnect after link loss.

UnitCamS3-5MP 发送端固件：不用路由器，直接把 MJPEG 推给 M5Cardputer 接收端，约 25fps，
12KB 一帧打成 9 个大包（ESP-NOW v2.0），自带发现信标、失联自动重连。
配 GBCAMS 接收端 (M5Cardputer) 使用；接收端会自动识别（右上角蓝标 ESP-NOW）。

⚠️ 需要接收端一起刷：GBCAMS Receiver (M5Cardputer)
```

### 条目 C — 发送端（兼容官方 App）/ Sender (AP-HTTP, 可选)
| 字段 Field | 值 Value |
|---|---|
| Name | `GBCAMS Sender AP-HTTP (UnitCamS3-5MP)` |
| Version | `0.0.1` |
| Device Type | **TimerCamera** |
| Author | `andjiang` |
| Github | 留空 |
| Cover | `cams3-wifi/cover-320x200.png` |
| FirmWare | `cams3-wifi/firmware.factory.bin` |

**Description:**
```
[GBCAMS] AP-HTTP MJPEG sender for UnitCamS3-5MP. Same SSID and API as the stock firmware
(UnitCamS3-WiFi, /api/v1/stream) — works with the official app and with Cardputer
Camera Receiver (auto-detected, yellow AP label). ~12fps, camera reset + triple buffering.

使用与官方出厂固件相同的 SSID 与接口（UnitCamS3-WiFi, /api/v1/stream），
既能被官方 App 使用，也能被 GBCAMS 接收端自动识别（右上角黄标 AP），约 12fps。
```

> 上架后把每个条目的 **Share Code** 记到本文件底部（`Share Code A/B/C:`），方便自己复现和分享。

---

## 5. 上架前自检 / Pre-publish checklist

- [x] 三个镜像都是 merged 全镜像，头部 `e9 03 02 3f`（DIO）
- [x] 分区表魔数在 `0x8000`（`aa`），app 在 `0x10000`（`e9`）
- [x] 与上一版镜像的前 `0x10000` 字节逐字节一致（bootloader + 分区表 + boot_app0 未变 → M5Burner 不会黑屏）
- [x] `platformio.ini` 三处 `board_build.flash_mode = dio`
- [x] 封面 320×200、`sha256` 已生成
- [ ] M5Burner 里填完 → Publish → 记下 Share Code → 真机 Burn 一次复验

生成封面：`python3 tools/make_m5burner_covers.py`（标题用设备同款 Font0 字模渲染）
