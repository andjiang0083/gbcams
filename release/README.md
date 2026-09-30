# GBCAMS — Wireless ESP-NOW Viewfinder / 无线 ESP-NOW 取景器

> 项目代号 **GBCAMS**（2026-09-30 起；原名 Cardputer Camera / Cardputer GB Cam）。

[EN] Turn your M5Cardputer + UnitCamS3-5MP into a low-latency wireless camera viewfinder with retro pixel filters, SD capture and a photo gallery. No WiFi router needed.
[CN] 把 M5Cardputer + UnitCamS3-5MP 变成低延迟无线相机取景器：复古像素滤镜、SD 卡拍照、相册管理，不需要路由器。

**当前版本 / Versions** — Cardputer `v0.8.8` · UnitCamS3 ESP-NOW `v0.0.9` · UnitCamS3 AP-HTTP `v0.0.1`
**发行说明与刷机 / Release sheet** → [`README_M5Burner.md`](README_M5Burner.md)

---

## Features / 功能

| Feature | Details |
|---------|---------|
| 📡 **Wireless** | ESP-NOW 直连 (无需路由器), 约 25fps; 自动发现 + 失联自动重连 |
| 🖥 **Sidebar HUD** | 画面 180×135 靠左 + 右侧 60px 状态栏: 滤镜 / 相框 / EV / 模式 / FPS / 丢帧 / 拍照反馈 / `H HELP` |
| 🎨 **9 filters** | `1`GB 8阶灰 · `2`Classic DMG绿 · `3`GBC-1 (8×8 Bayer) · `4`GBC-2 (蓝噪) · `5`GBA 15位色 · `6`BR-1 旧壁纸黄 · `7`BR-2 荧光灯 · `8`DMG 真4阶绿 (#0F380F→#9BBC0F) · `9`原图 |
| 🖼 **4 frames** | 像素相框: DMG点阵 / GBC紫 / CRT双线 / Polaroid (与滤镜正交, NVS 记忆) |
| 📸 **Capture** | `ENTER` 存 SD: 原图 JPG (滤镜关) 或套用滤镜的 BMP |
| 🗂 **Gallery** | 缩略图网格 · 批量删除 · 收藏 · 原图/效果对比 · 幻灯片 |
| 🈶 **Help** | `H` 中英双语帮助页 5 页 (取景 3 + 相册 2, 含状态诊断页) |
| 🔄 **Dual-mode** | 自动识别发送端: ESP-NOW (蓝标) ↔ 官方 AP-HTTP (黄标) |

---

## Hardware Required / 所需硬件

- **M5Cardputer** (接收端 / receiver)
- **UnitCamS3-5MP** with PY260 / BF3005 sensor (发送端 / sender)
- **MicroSD card** (FAT32, 拍照用)
- **2× USB-C cables** (刷机用)

---

## Installation / 刷机安装

### Method 1: M5Burner (Recommended / 推荐)
1. 打开 M5Burner → **Custom** → **Import Custom FW** → 选 `release/m5burner.json`
2. 两台设备分别选条目 → **Burn**
   - Cardputer 条目 → 设备类别 **Cardputer**
   - UnitCamS3 条目 → 设备类别 **TimerCamera**
3. ⚠️ 只用本目录的 `firmware.factory.bin`（merged 全镜像）。M5Burner 固定烧 `0x0`，app-only 镜像会黑屏。

### Method 2: esptool
```bash
esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_size 8MB \
  0x0 release/cardputer/firmware.factory.bin     # 接收端
esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_size 8MB \
  0x0 release/cams3/firmware.factory.bin         # 发送端
```

### Method 3: PlatformIO (开发用 / dev)
```bash
cd cardputer     && pio run -t upload --upload-port /dev/cu.usbmodem101    # 接收端
cd cams3/espnow  && pio run -t upload --upload-port /dev/cu.usbmodem1101   # 发送端 (ESP-NOW)
cd cams3/wifi    && pio run -t upload --upload-port /dev/cu.usbmodem1101   # 发送端 (AP-HTTP)
```

### 重新打包发行件 / Rebuild release folder
```bash
./release/build.sh                              # 三端编译 + 复制 merged 镜像 + VERSION
python3 tools/make_m5burner_covers.py           # 320×200 封面 (设备同款 Font0 字模)
```

---

## Quick Start / 快速使用

1. 两台设备通电 / Power on both
2. Cardputer 右上角出现 **蓝色 `ESP-NOW`** 标签 (官方 AP 路径则是黄色 `AP`)
3. 3 秒内自动连上发送端 → 出画面
4. `1`–`9` 换滤镜，`0` 开相框，`ENTER` 拍照，`R` 进相册，`H` 看帮助

## Controls / 按键

| Key | Function |
|-----|----------|
| `1`–`9` | 滤镜 / Filters (见上表) |
| `0` `,` `/` | 相框开关 / 换款 |
| `-` `+` | 曝光 EV (−3…+3, 长按连发) |
| `ENTER` | 快门拍照 / Shutter |
| `R` | 相册 / Gallery |
| `H` | 帮助 (双语) / Help |
| `A D W S` | (相册) 移动 |
| `TAB` `E` | (相册) 下页 / 上页 |
| `BS` `X` `V` `T` | (相册) 删除 / 多选 / 全部↔收藏 / 补缩略图 |
| `O` `I` `P` `F` `Q` | (查看) 对比 / 信息 / 幻灯 / 收藏 / 返回 |

---

## Project Structure / 项目结构

```
gbcams/
├── cardputer/        ← 接收端固件 (M5Cardputer, LovyanGFX/M5GFX + TJpgDec)
├── cams3/espnow/     ← 发送端固件 (UnitCamS3, ESP-NOW v2.0 大包, 约 25fps)
├── cams3/wifi/       ← 发送端固件 (UnitCamS3, AP+HTTP MJPEG, 兼容官方 App, 约 12fps)
├── release/          ← M5Burner 发行件 (merged 镜像 + 封面 + sha256 + 发行说明)
├── tools/            ← 辅助脚本 (封面生成 / 蓝噪抖动表)
└── docs/             ← 设计报告 / 协议 / 版式核对图
```

## Tech Stack / 技术栈

- **Framework**: Arduino (ESP32-S3) + PlatformIO
- **Display**: LovyanGFX (M5GFX), 内置 efontCN_12 中文字库
- **Link**: ESP-NOW (12KB 帧 → 9 包) / AP-HTTP MJPEG
- **Storage**: SD (SPI) + NVS (偏好/相框/滤镜记忆)
- **Image**: TJpgDec 解码, 24-bit BMP 编码, 分级抖动调色

## License

MIT
