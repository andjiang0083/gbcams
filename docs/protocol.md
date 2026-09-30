# GBCAMS — 通信协议设计文档

> 文档写于项目早期（当时叫 "Cardputer GameBoy Cam"），2026-09-30 起统一为 **GBCAMS**。

> 版本: v0.1 (2026-08-01)
> 状态: 设计已确认,进入实现

## 1. 系统架构

```
┌─────────────┐   协议自动识别    ┌──────────────┐
│  CAMS3      │ ◄──────────────► │  Cardputer   │
│  (发送端)    │    ESP-NOW 或     │  (接收端)     │
│             │    WiFi HTTP      │              │
└─────────────┘                   └──────────────┘

CAMS3 两种固件形态(用户二选一刷入):
  1. 原厂 WiFi 固件 (UnitCamS3-UserDemo) — 买来即用, AP + HTTP MJPEG
  2. 自定义 ESP-NOW 固件 (cams3/espnow)   — 高性能取景, 54Mbps 单播
```

## 2. 设计目标

1. **零配置自动识别**: 用户只刷 Cardputer 固件, 开机自动判断 CAMS3 是哪种固件
2. **不 reboot、不死循环**: 探测循环纯软切换, 吸取 v0.7.10~19 auto-detect 教训
3. **确定性识别**: 用"信号存在性"判断, 不用"超时猜测"
4. **取景帧率优先**: 一切以帧率最高为准则

## 3. 两种固件的可识别信号(事实基础)

| 固件 | 信号 | 信道 | 识别方法 |
|------|------|------|---------|
| 原厂 WiFi 固件 | AP `UnitCamS3-WiFi` 广播 | 1 | WiFi scan 精确匹配 SSID |
| ESP-NOW 固件 | ESP-NOW 视频帧广播 (CamBroadcaster) | 6 | 收包 + 帧头校验 |

**关键**: 两者信道不同(1 vs 6)、信号形态完全不同(beacon vs ESP-NOW 帧),
不会误判。CAMS3 上电后 1~3s 内必发出其中一种信号。

## 4. 自动识别状态机 (Cardputer 端)

```
                  开机
                   │
                   ▼
        ┌─────────────────────┐
        │  DETECT_ESPNOW       │  STA ch6, 听 ESP-NOW 1s
        │  (无帧 → 下一步)      │
        └──────────┬──────────┘
                   │ 超时无帧
                   ▼
        ┌─────────────────────┐
        │  DETECT_SCAN         │  WiFi scan 全信道 3s
        │  找 "UnitCamS3-WiFi" │
        └────┬───────────┬────┘
             │           │
      找到 AP│           │没找到
             ▼           ▼
   ┌──────────────┐  ┌────────────────────────────┐
   │ MODE_HTTP    │  │ 显示 "等待 CAMS3..." 1s     │
   │ 连 AP        │  │ └→ 回到 DETECT_ESPNOW        │
   │ /status 验证 │  └────────────────────────────┘
   │ 自动调参     │
   │ /stream 拉流 │
   └──────────────┘
             │
      流断开重连×2失败
             ▼
       回到 DETECT_ESPNOW (重新探测, 永不 reboot)

运行中:
  MODE_ESPNOW: 10s 无帧 → 重新探测 (用户可能换了固件)
  MODE_HTTP:   stream 断开, 重连 2 次失败 → 重新探测
```

### 状态枚举

```cpp
enum CamMode {
  MODE_DETECT_ESPNOW,   // 听 ESP-NOW 帧 (STA ch6)
  MODE_DETECT_SCAN,     // WiFi scan 找 UnitCamS3-WiFi
  MODE_WAIT_RETRY,      // 未发现, 显示等待, 稍后重试
  MODE_ESPNOW,          // ESP-NOW 取景 (单播 54Mbps)
  MODE_HTTP,            // WiFi HTTP MJPEG 取景
};
```

### 各模式切换的软切换要求

- **MODE_DETECT_ESPNOW → MODE_ESPNOW**: 无需切换, 同一 STA ch6 状态
- **MODE_HTTP ↔ 其他**: `s_mjpeg->end()` + `WiFi.mode(WIFI_STA)` 重设
- **MODE_ESPNOW → MODE_DETECT_SCAN**: `ESP_NOW.end()` + `WiFi.mode(WIFI_STA)` + scan
- **MODE_DETECT_SCAN → MODE_ESPNOW**: 重新 `ESP_NOW.begin()` + 重新注册回调 + 重新建 peer

⚠️ 所有切换都必须走完整初始化(参考 `switchToEspNow()` 完整重设原则),
不能只改状态变量。

## 5. ESP-NOW 协议 (cams3/espnow ↔ cardputer)

### 5.1 包格式

```
[frame_id:4][total_pkts:2][pkt_index:2][jpeg_data:0..240]
```

| 字段 | 大小 | 说明 |
|------|------|------|
| frame_id | 4B | 帧序号, 递增, 接收端以此识别新帧 |
| total_pkts | 2B | 本帧总包数 |
| pkt_index | 2B | 当前包序号 (0-based) |
| jpeg_data | ≤240B | JPEG 分片 |

- 配置: `PKT_DATA_MAX=240`, `HEADER_LEN=8`, `ESPNOW_CHANNEL=6`
- 接收端缓冲: `MAX_FRAME_SIZE = 64KB`

### 5.2 发现协议 (beacon 握手 → 单播 54Mbps)

```
CAMS3 (ESP-NOW 固件)                Cardputer
    │  broadcast 视频帧 (ch6)          │
    │───────────────────────────────►│  (探测阶段收到帧 → MODE_ESPNOW)
    │                                 │
    │  ◄──────────── beacon 0x55 ────│  Cardputer 确认后发送 (1字节)
    │                                 │
    │  收到 beacon → 移除广播 peer     │
    │  → 添加单播 peer @ 54Mbps       │
    │  unicast 视频帧 (54Mbps)        │
    │───────────────────────────────►│
```

**Cardputer 端 beacon 发送要求**:
- 进入 MODE_ESPNOW 后, `delay(500)` 再发(给 ESP-NOW 稳定时间,
  吸取 v0.7.26 BeaconPeer 干扰教训)
- 每 3s 重发, 直到收到视频数据 (收到帧后停止)

**CAMS3 端**: 已有完整实现 (v0.0.6), 无需改动。

### 5.3 帧重组 (接收端)

- 3 态缓冲区: `BUF_IDLE / BUF_READY / BUF_RENDERING`
- 🔴 帧完成信号必须加 `if (s_buf_state == BUF_IDLE)` 守卫 (v0.0.5 修复, 防竞态)
- 帧超时 300ms 丢弃半帧
- 收齐条件: `s_recv_pkts >= s_total_pkts`

## 6. WiFi HTTP 协议 (原厂固件 ↔ cardputer)

### 6.1 端点 (全部在 http://192.168.4.1)

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/v1/stream` | GET | MJPEG 流, boundary=`123456789000000000000987654321`, HTTP chunked |
| `/api/v1/capture` | GET | 单帧 JPEG |
| `/api/v1/control?var=X&val=Y` | GET | 调参 |
| `/api/v1/status` | GET | 传感器状态 JSON |

### 6.2 开机自动调参 (进入 MODE_HTTP 时)

```cpp
// 一次性请求, 失败静默重试, 不影响主流程
GET /api/v1/control?var=framesize&val=6    // QVGA 320×240
GET /api/v1/control?var=quality&val=8      // 平衡画质/帧率
```

原厂默认 VGA(640×480) + quality 12 → 帧大、帧率 ~10fps;
调成 QVGA + q8 后帧小一半、帧率相当, 无线更稳。

### 6.3 MJPEG 流解析

- **用 Content-Length 状态机** (可信边界), 不用 SOI/EOI 搜索
- v0.7.27 的 `MjpegHttpClient` 是 SOI/EOI 版本 → 需要升级为 Content-Length 状态机
- 参考 `references/mjpeg-stream-parser.md` 的完整实现

## 7. 已知历史坑 (实现时必须避开)

| 坑 | 对策 |
|----|------|
| v0.7.10~19 auto-detect 无限 reboot 循环 | 永不 reboot, 纯软切换; 探测循环幂等 |
| `s_auto_detect_done` RAM 标志重启失效 | 不需要该标志 |
| WiFi init 卡死 (`while(!started())`) | 所有等待加 5s 超时 |
| 全局静态 `WiFiClient`/`MjpegHttpClient` | 一律堆分配 (`new`) |
| SD SPI 干扰 WiFi 射频校准 | SD init 移到模式确定之后 |
| BeaconPeer 干扰广播接收 (v0.7.26) | beacon 加 500ms 启动延迟 |
| drawJpg 失败静默 | 检查返回值, 失败跳过渲染 |
| 帧完成竞态 | `if (s_buf_state == BUF_IDLE)` 守卫 |

## 8. 版本管理

- 版本横幅编译时注入 git tag (修掉 "v0.7.9 显示 v0.0.4" 滞后问题)
- release 构建后自动验证 bin 指纹 (grep 版本字符串)
