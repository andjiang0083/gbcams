# CAMS3 原厂固件 WiFi 兼容性文档

> 来源: https://github.com/m5stack/UnitCamS3-UserDemo (branch: `unitcams3-5mp`)
> 本地副本: ~/esp32/cams3-fw-source (已删除, 本文档为永久记录)
> 提取日期: 2026-08-01

## 1. 硬件

- **传感器**: PY260 / BF3005 (5MP)
- **SoC**: ESP32-S3 + 8MB PSRAM
- **LED**: GPIO 14 (状态灯)
- **Camera 复位**: GPIO 21 (软件拉低50ms再拉高)

## 2. WiFi AP 配置 (server_ap.cpp)

```cpp
String ap_ssid = "UnitCamS3-WiFi";
WiFi.softAP(ap_ssid, emptyString, 1, 0, 1, false);
//                SSID      密码      ch 隐藏 最大连接
```

| 项 | 值 |
|----|-----|
| SSID | `UnitCamS3-WiFi` |
| 密码 | 无 (open) |
| 信道 | **1** |
| 隐藏 | 否 (可见) |
| 最大连接 | 1 |
| IP | 192.168.4.1 |
| 端口 | 80 |

## 3. REST API (api_camera.cpp)

### 3.1 端点总览

| 端点 | Handler | 说明 |
|------|---------|------|
| `GET /api/v1/stream` | `streamJpg` | MJPEG 流 (multipart/x-mixed-replace) |
| `GET /api/v1/capture` | `sendJpg` | 单帧 JPEG |
| `GET /api/v1/bmp` | `sendBMP` | 单帧 BMP |
| `GET /api/v1/control?var=X&val=Y` | `setCameraVar` | 设置传感器参数 |
| `GET /api/v1/status` | `getCameraStatus` | 传感器状态 JSON |

### 3.2 MJPEG 流格式 (/api/v1/stream)

```
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace;boundary=123456789000000000000987654321
Transfer-Encoding: chunked

\r\n--123456789000000000000987654321\r\n
Content-Type: image/jpeg\r\n
Content-Length: <N>\r\n
\r\n
<JPEG data N bytes>
\r\n--123456789000000000000987654321\r\n
Content-Type: image/jpeg\r\n
Content-Length: <M>\r\n
...
```

- boundary 常量: `PART_BOUNDARY = "123456789000000000000987654321"`
- `_chunked = true` → **HTTP chunked transfer encoding**
- 每帧有 `Content-Length` → 可信帧边界
- ⚠️ 原厂 stream handler 的 `_content()` 返回 `maxLen` 而非实际写入字节数
  → 已知性能 bug (实测 3fps 级)。**HTTP 取景建议用我们自己的 cams3/wifi 固件**,
  原厂固件仅作"买来即用"的兼容目标。

### ⚠️ 3.2.1 实测发现 (2026-08-01): 原厂 stream 可能完全空流

用 Cardputer v0.8.0 (Content-Length 状态机) 实测原厂固件 `/api/v1/stream`,
**在相机被非法参数 (quality=8) 搞坏后断电重启, 流依然是空的**:

```
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace;boundary=123456789000000000000987654321
Connection: close
Transfer-Encoding: chunked
\r\n\r\n
0   \r\n        ← chunked 终止块! 没有任何 JPEG 帧
HTTP/1.1 200 OK ← 重连后重复响应头
```

**结论**: 原厂 AsyncJpegStreamResponse 在异常状态下发送"空流"
(响应头 + `"0"` 终止块 + 重复响应头), 不产生任何帧数据。
此状态断电重启无法恢复 → **原厂固件 stream 不可靠, 取景必须用
我们自己的 cams3/wifi 固件 (实测 12fps) 或 espnow 固件 (实测 25fps)**。

### 3.3 /api/v1/control 支持参数

| var | 类型 | 说明 |
|-----|------|------|
| framesize | int | 帧尺寸枚举 |
| quality | int | JPEG 质量 |
| contrast / brightness / saturation / sharpness | int | 图像调整 |
| gainceiling | int | 增益上限 |
| colorbar | int | 测试彩条 |
| awb / awb_gain | int | 白平衡 |
| agc / agc_gain | int | 自动增益 |
| aec / aec2 / aec_value | int | 自动曝光 |
| hmirror / vflip | int | 镜像/翻转 |
| denoise / dcw / bpc / wpc / raw_gma / lenc | int | 降噪等 |
| special_effect | int | 特效 |
| wb_mode | int | 白平衡模式 |
| ae_level | int | 曝光等级 |

⚠️ quality 无钳制 → 客户端传 quality=2 时帧极大 (每帧 20KB+)
→ **Cardputer 端开机调参必须钳制 quality ≥ 10**。

### 3.4 /api/v1/status JSON

```json
{
  "framesize": 8, "quality": 12, "brightness": 0, "contrast": 0,
  "saturation": 0, "sharpness": 0, "special_effect": 0, "wb_mode": 0,
  "awb": 1, "awb_gain": 1, "aec": 1, "aec2": 0, "denoise": 1,
  "ae_level": 0, "aec_value": 300, "agc": 1, "agc_gain": 30,
  "gainceiling": 5, "bpc": 0, "wpc": 1, "raw_gma": 1, "lenc": 1,
  "hmirror": 0, "vflip": 0, "dcw": 1, "colorbar": 0
}
```

## 4. 相机默认配置 (hal_camera.cpp)

```cpp
config.xclk_freq_hz = XCLK_FREQ_HZ;   // 20MHz
config.pixel_format = PIXFORMAT_JPEG;
config.frame_size   = FRAMESIZE_VGA;  // 640×480 ← 默认偏大!
config.jpeg_quality = 12;
config.fb_count     = 1;              // 单缓冲 ← 帧率瓶颈
config.fb_location  = CAMERA_FB_IN_PSRAM;
config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
```

**优化点** (我们的 cams3/wifi 固件已改):
- `fb_count = 2~3` → 相机不等待发送, 帧率翻倍
- `FRAMESIZE_QVGA` + `quality 8` → 帧小、传输快
- 阻塞 WebServer + 单次 write 组包 → 14.8fps (替代原厂 AsyncWebServer 3fps)

## 5. 性能参考 (2026-07-14 实测)

| 配置 | CAMS3 FPS | drawJpg 时间 | JPEG 大小 |
|------|-----------|--------------|-----------|
| 96×96 + q20 | ~14fps | 8-9ms | ~2.7KB |
| 320×240 + q6 | ~10fps | 34-42ms | ~8-10KB |
| 320×240 + q4 | ~10fps | 17-30ms | ~7.7KB |

瓶颈在 CAMS3 编码端 (~10-14fps 上限), 不在 Cardputer 解码。
