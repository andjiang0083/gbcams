/**
 * GBCAMS 接收端 — Dual-mode: ESP-NOW / AP-HTTP MJPEG
 *
 * 自动识别 CAMS3 固件形态(零配置, 全自动, 永不 reboot):
 *   1. DETECT_ESPNOW: STA ch6 听 ESP-NOW 帧 1s
 *   2. DETECT_SCAN:   WiFi scan 全信道 3s, 找 "UnitCamS3-WiFi"
 *      ├─ 找到 → MODE_HTTP (连 AP → 自动调参 QVGA/q8 → 拉 MJPEG 流)
 *      └─ 没找到 → 显示"等待 CAMS3..." → 回到 DETECT_ESPNOW (循环)
 *   3. 运行中断流 → 回到探测循环 (用户可能换了固件)
 *
 * 设计要点 (吸取 v0.7.10~19 历史教训):
 *   - 永不 reboot, 纯软切换 (探测循环幂等)
 *   - 双缓冲消除回调↔渲染竞态 (v0.7.19)
 *   - beacon 加 500ms 启动延迟 (v0.7.26 BeaconPeer 干扰教训)
 *   - HTTP 解析用 Content-Length 状态机 (不用 SOI/EOI)
 *   - WiFi/SD 初始化加超时, 失败不死循环
 */

#include <Arduino.h>
#include <WiFi.h>
#include "ESP32_NOW.h"
#include "Global.h"
#include "UIManager.h"
#include "MjpegHttpClient.h"

#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

// 版本号由 platformio.ini build_flags 注入 (git describe)
// 经典字符串化: 传 APP_VERSION_RAW=v0.8.0 无引号, 这里转成字符串
#ifndef APP_VERSION_RAW
#define APP_VERSION_RAW dev
#endif
#define _STR(x) #x
#define _XSTR(x) _STR(x)
#define APP_VERSION _XSTR(APP_VERSION_RAW)

// ════════════════════════════════════════
// Constants
// ════════════════════════════════════════
#define ESPNOW_CHANNEL   6
#define HEADER_LEN       8
#define PKT_DATA_MAX     1450      // v2.0 大包 (与 cams3 v0.0.8 同步; 重组公式 offset=idx*PKT_DATA_MAX 自动跟随)

#define FRAME_TIMEOUT_MS 300
#define ENTER_DEBOUNCE_MS 350
// QVGA q8 JPEG 一般 <15KB, 32KB 缓冲足够; 双缓冲共 64KB
// (无 PSRAM 设备内部 RAM 紧张, 64KB×2 会导致 SD 注册 OOM)
#define MAX_FRAME_SIZE   (32 * 1024)

// 探测时序 (ESP-NOW 为主 — espnow 固件不开 AP, scan 仅兜底)
#define DETECT_ESPNOW_MS     10000  // 听 ESP-NOW 时长 (主探测路径)
#define DETECT_SCAN_MS       3000   // scan 最长时间
#define RETRY_WAIT_MS        1000   // 未发现时显示等待时长
#define BEACON_INTERVAL_MS   3000   // beacon 重发间隔
#define BEACON_START_DELAY_MS 500   // 进 ESP-NOW 后延迟发 beacon
#define ESPNOW_STALL_MS      10000  // ESP-NOW 10s 无帧 → 重新探测
#define HTTP_RECONNECT_MAX   2      // HTTP 断流重连次数

// v0.8.7: 协议版本 — 两端启动各打印一次, 现场对齐 (改包长/包头时同步 cams3/)
#define GBCAM_PROTO_VER  "2.0"

// ── v0.8.7: 诊断计数 (事件式: 只在发生后的 5s 内提示, 不做常驻 UI) ──
volatile uint32_t g_drop_frames     = 0;   // 丢帧总数 (超时 + 超限)
volatile uint32_t g_drop_timeout    = 0;   // v0.8.8: 其中"缺包超时被整帧丢弃"
volatile uint32_t g_soft_frames     = 0;   // v0.8.8: 软接收补帧 (只缺 1 块, 用上一帧内容补齐)
volatile uint32_t g_oversize_frames = 0;   // 帧超过 MAX_FRAME_SIZE 被丢弃
volatile uint32_t g_jpeg_fail       = 0;   // drawJpg 解码失败
volatile uint32_t g_last_drop_ms    = 0;   // 最近一次掉帧时刻 (HUD 事件提示用)
char g_boot_info[48] = "";                 // 启动信息 (版本 + PSRAM), 等待画面显示

// ════════════════════════════════════════
// Modes
// ════════════════════════════════════════
enum CamMode : uint8_t {
  MODE_DETECT_ESPNOW = 0,
  MODE_DETECT_SCAN,
  MODE_WAIT_RETRY,
  MODE_ESPNOW,
  MODE_HTTP,
};

static CamMode s_mode = MODE_DETECT_ESPNOW;
static unsigned long s_mode_start = 0;      // 进入当前模式的时刻
static unsigned long s_last_frame_ms = 0;   // 最近收到完整帧的时刻
static bool s_sd_inited = false;
static uint32_t s_search_tick = 0;          // 等待画面动画帧计数

// ════════════════════════════════════════
// Frame buffers (double-buffer, v0.7.19)
// ════════════════════════════════════════
enum BufState : int { BUF_IDLE = 0, BUF_READY = 1, BUF_RENDERING = 2 };

static uint8_t    s_frame_buf[2][MAX_FRAME_SIZE];
static volatile int     s_fill_idx  = 0;    // 回调写入的缓冲
static volatile int     s_ready_idx = -1;   // 待渲染缓冲 (-1 = 无)
static volatile size_t  s_ready_len = 0;

// 每帧重组状态 — 🔴 v0.8.7 所有权归 ESP-NOW 回调 (Core 0); 主循环只读,
// 需要中止时置 s_frame_abort_req 由回调执行 (旧版主循环直写这些变量 → 跨核竞态:
// 清零撞上"收齐"判定会丢帧; 且非 volatile, 去年读值可能被编译器缓存进寄存器)
static uint32_t s_last_frame_id = 0;
static volatile bool     s_frame_active  = false;
static volatile int      s_recv_pkts     = 0;
static volatile int      s_total_pkts    = 0;
static volatile size_t   s_recv_len      = 0;
static volatile unsigned long s_frame_start_ms = 0;
static uint32_t s_seen_mask = 0;          // v0.8.7: 包序号位图 (去重; total ≤ 32 时精确)
static bool     s_frame_bad = false;      // v0.8.7: 超限/非法包 → 整帧丢弃
static volatile bool s_frame_abort_req = false;   // 主循环 → 回调: 请求中止当前帧

// 最近渲染帧 (供拍照使用一致帧)
static uint8_t* s_last_render_buf = s_frame_buf[0];
static size_t   s_last_render_len = 0;

// FPS
float currentFps = 0;
static unsigned long s_fps_last = 0;
static int s_fps_cnt = 0;

// 拍照冻结标志 (v0.8.1) — captureFrame 期间拒绝新帧写入
volatile bool capFreeze = false;

// ════════════════════════════════════════
// 模式相关对象
// ════════════════════════════════════════
static MjpegHttpClient* s_mjpeg = nullptr;
static int s_http_reconnect = 0;

// ESP-NOW beacon 发送端 (Cardputer 宣告自己)
struct BeaconPeer : public ESP_NOW_Peer {
  BeaconPeer() : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr) {}
  bool begin() { return add(); }
  void remove_peer() { remove(); }   // 包装 protected remove()
  void send_beacon() { uint8_t b = 0x55; send(&b, 1); }
};
static BeaconPeer* s_beacon = nullptr;
static unsigned long s_beacon_last = 0;

// ════════════════════════════════════════
// ESP-NOW Callback
// ════════════════════════════════════════
void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  int buf = s_fill_idx;           // 读一次, 本地使用 (软接收交换缓冲时要用)
  // v0.8.7: 中止请求由回调执行 (单侧所有权 — 主循环不再直写重组状态)
  // v0.8.8: 超时不再无条件丢整帧 — 只缺 1 块就"软接收"补一帧出去 (缺块保留上一帧内容)。
  // 15 fps 下丢一包 = 画面静止 60ms, 那正是用户报的"突然卡一下"; 软接收把这次空挡消掉。
  if (s_frame_abort_req) {
    s_frame_abort_req = false;
    bool soft = !s_frame_bad && s_total_pkts > 0 && s_recv_len > 1000 &&
                s_recv_pkts + 1 >= s_total_pkts;
    if (soft) {
      if (s_ready_idx < 0) {
        s_ready_len = s_recv_len;
        s_ready_idx = buf;
        s_fill_idx  = 1 - buf;
        s_last_frame_ms = millis();
        g_soft_frames++;
      }
    } else if (s_recv_pkts > 0) {           // 有内容却没到齐 → 记一次真丢帧
      g_drop_frames++; g_drop_timeout++; g_last_drop_ms = millis();
    }
    s_frame_active = false;
    s_recv_len = 0; s_recv_pkts = 0; s_total_pkts = 0;
  }
  if (capFreeze) return;          // v0.8.1: 拍照期间冻结 — 帧状态会在下一帧 frame_id 变化时自愈
  if (len < HEADER_LEN) return;

  uint32_t frame_id;
  uint16_t total, idx;
  memcpy(&frame_id, data, 4);
  memcpy(&total, data + 4, 2);
  memcpy(&idx, data + 6, 2);
  size_t payload = len - HEADER_LEN;

  if (frame_id != s_last_frame_id || !s_frame_active) {
    // 新帧: 丢弃未完成的半帧
    s_last_frame_id = frame_id;
    s_frame_active = true;
    s_frame_bad    = false;
    s_seen_mask    = 0;
    s_recv_len = 0;
    s_recv_pkts = 0;
    s_total_pkts = total;
    s_frame_start_ms = millis();
  }

  if (total == 0 || idx >= total) return;   // v0.8.7: 非法包头直接丢 (旧版 idx>=total 也计入收齐)

  size_t offset = (size_t)idx * PKT_DATA_MAX;
  if (offset + payload > MAX_FRAME_SIZE) {
    // v0.8.7: 超限 → 整帧标记丢弃 (旧版跳过 memcpy 但仍推进计数与长度 → 长度虚高的
    // 半帧被当作"收齐"交给 drawJpg, 静默失败, 没有任何诊断信息)
    if (!s_frame_bad) {
      s_frame_bad = true;
      g_oversize_frames++;
      g_drop_frames++;                 // v0.8.8: 侧栏 DROP 显示总丢帧数
      g_last_drop_ms = millis();
    }
    return;
  }

  // v0.8.7: 包去重 — 同一 idx 重复到达不再重复计数 (旧版计数会提前满足"收齐"且丢块)
  bool first = true;
  if (idx < 32) {
    uint32_t bit = 1UL << idx;
    if (s_seen_mask & bit) first = false;
    else                   s_seen_mask |= bit;
  }
  if (payload > 0) memcpy(s_frame_buf[buf] + offset, data + HEADER_LEN, payload);
  if (first) s_recv_pkts++;
  size_t end = offset + payload;
  if (end > s_recv_len) s_recv_len = end;

  // 帧收齐 → 交换缓冲
  if (s_recv_pkts >= s_total_pkts && s_recv_len > 1000 && !s_frame_bad) {
    if (s_ready_idx < 0) {
      s_ready_len = s_recv_len;
      s_ready_idx = buf;
      s_fill_idx  = 1 - buf;
      s_last_frame_ms = millis();
    }
    s_recv_len = 0;
    s_recv_pkts = 0;
    s_frame_active = false;
  }
}

// ════════════════════════════════════════
// 模式切换 (纯软切换, 永不 reboot)
// ════════════════════════════════════════

// SD 初始化只做一次, 放到首次进入取景模式后 (避免干扰 WiFi 射频校准)
static void ensureSD() {
  if (!s_sd_inited) {
    UIManager::initSD();
    s_sd_inited = true;
  }
}

// 停止 ESP-NOW 相关 (离开 MODE_ESPNOW 时调用)
static void teardownEspNow() {
  if (s_beacon) {
    s_beacon->remove_peer();
    s_beacon = nullptr;
  }
  ESP_NOW.end();
}

// 停止 HTTP 相关 (离开 MODE_HTTP 时调用)
static void teardownHttp() {
  if (s_mjpeg) {
    s_mjpeg->end();
    delete s_mjpeg;
    s_mjpeg = nullptr;
  }
  s_http_reconnect = 0;
  WiFi.disconnect();
}

// 自动调参 (仅在首次进入 HTTP 模式时执行一次, 重连不重复)
// ⚠️ quality 合法范围 10~63 (esp32-camera), 低于 10 会让相机抓帧失败!
static void tuneCameraParams() {
  static bool tuned = false;
  if (tuned) return;
  tuned = true;

  WiFiClient c;
  if (!c.connect("192.168.4.1", 80)) return;
  c.setTimeout(2);
  // QVGA (framesize=6) + quality 10 — 帧小一半, 传输更稳
  c.printf("GET /api/v1/control?var=framesize&val=6 HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n");
  while (c.available()) c.read();
  c.stop();
  delay(50);
  if (!c.connect("192.168.4.1", 80)) return;
  c.setTimeout(2);
  c.printf("GET /api/v1/control?var=quality&val=10 HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n");
  while (c.available()) c.read();
  c.stop();
  delay(50);
}

// 启动 HTTP 模式 (进入 MODE_HTTP 时调用)
static void setupHttpMode() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // 首次先 begin 连接 (探测阶段 WiFi 是 STA 但未关联 AP)
  WiFi.begin("UnitCamS3-WiFi", "");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 5000) {
      // 5s 未连上 → 放弃, 回探测循环
      Serial.printf("[http] WiFi connect timeout, status=%d\n", WiFi.status());
      return;
    }
    delay(100);
  }
  Serial.printf("[http] WiFi connected, IP=%s, RSSI=%d\n",
    WiFi.localIP().toString().c_str(), WiFi.RSSI());

  tuneCameraParams();

  s_mjpeg = new MjpegHttpClient(s_frame_buf[0], MAX_FRAME_SIZE);
  if (!s_mjpeg || !s_mjpeg->begin()) {
    if (s_mjpeg) { delete s_mjpeg; s_mjpeg = nullptr; }
    return;
  }
  s_mjpeg->onFrameReady = [](size_t len) {
    // HTTP 模式单缓冲: MJPEG 解析与渲染同核, 无竞态
    s_ready_len = len;
    s_ready_idx = 0;
    s_last_frame_ms = millis();
  };

  s_mode = MODE_HTTP;
  s_mode_start = millis();
  s_http_reconnect = 0;
  UIManager::modeLabel = "AP";
  UIManager::modeLabelColor = TFT_YELLOW;
  ensureSD();
  Serial.println("[mode] HTTP MJPEG active");
}

// ════════════════════════════════════════
// 探测循环
// ════════════════════════════════════════

// 进入 ESP-NOW 监听态 (探测或正式模式的公共前置)
// 保证 WiFi STA ch6 + ESP_NOW 已就绪, 并启动 beacon 宣告自己
// (协议第 2 步: Cardputer boot 后主动发 beacon → CAMS3 切单播 54Mbps。
//  1 字节广播 beacon 几乎必成功 — 不依赖概率性的广播完整帧)
// 返回 true 成功; false 失败 → 调用方应转 WAIT_RETRY (避免空转卡死)
static bool enterEspNowListen() {
  if (s_beacon) {
    s_beacon->remove_peer();
    s_beacon = nullptr;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_CHANNEL);
  unsigned long t0 = millis();
  while (!WiFi.STA.started()) {
    if (millis() - t0 > 5000) break;
    delay(10);
  }
  if (!WiFi.STA.started()) {
    Serial.println("[espnow] enterEspNowListen: WiFi STA FAIL");
    return false;
  }
  WiFi.setSleep(false);
  if (!ESP_NOW.begin()) {
    Serial.println("[espnow] enterEspNowListen: ESP_NOW.begin FAIL");
    return false;
  }
  ESP_NOW.onNewPeer(on_espnow_recv, nullptr);

  // 探测阶段就发 beacon (500ms 延迟, v0.7.26 教训)
  static BeaconPeer beacon;
  s_beacon = &beacon;
  s_beacon_last = millis() + BEACON_START_DELAY_MS;
  beacon.begin();
  return true;
}

static void detectEspNowTick() {
  // 听 ESP-NOW 帧: 收到即确认 → 切换到完整 ESP-NOW 模式
  if (s_ready_idx >= 0) {
    // beacon 已由 enterEspNowListen 启动, 这里只需补状态
    s_mode = MODE_ESPNOW;
    s_mode_start = millis();
    UIManager::modeLabel = "ESP-NOW";
    UIManager::modeLabelColor = TFT_CYAN;
    ensureSD();
    Serial.println("[mode] detect→ESP-NOW");
    return;
  }
  // 监听超时 → 直接重试 (espnow 固件为主, 不做 WiFi scan)
  if (millis() - s_mode_start >= DETECT_ESPNOW_MS) {
    s_mode = MODE_WAIT_RETRY;
    s_mode_start = millis();
  }
}

static void detectScanTick() {
  // 全信道 scan 找 UnitCamS3-WiFi (AP 固件) — espnow 固件不开 AP, 扫不到属正常
  // 扫描是阻塞的 (1-3s), 期间 ESP-NOW 收不到帧 → 扫描后必须恢复 ESP-NOW 监听
  static int scan_count = 0;
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == "UnitCamS3-WiFi") { found = true; break; }
  }
  WiFi.scanDelete();

  if (found) {
    Serial.println("[mode] scan found UnitCamS3-WiFi → HTTP");
    setupHttpMode();
    if (s_mode == MODE_HTTP) { scan_count = 0; return; }  // 成功
    // 连 AP 失败 → 回探测
    Serial.println("[mode] HTTP connect FAIL");
    s_mode = MODE_WAIT_RETRY;
    s_mode_start = millis();
    return;
  }

  // 没找到 → 回 ESP-NOW 监听 (不空转 scan)
  // 每 3 次 scan 失败后延长等待, 减少盲区时间
  Serial.println("[mode] scan: no AP found (espnow? retrying listen)");
  scan_count++;
  if (scan_count >= 3) {
    // 连续 3 次没找到 AP → 大概率是 espnow 固件, 放弃 scan, 纯 ESP-NOW 监听
    scan_count = 0;
    if (enterEspNowListen()) {
      s_mode = MODE_DETECT_ESPNOW;
      s_mode_start = millis();
      return;
    }
  }
  s_mode = MODE_WAIT_RETRY;
  s_mode_start = millis();
}

// ════════════════════════════════════════
// Keyboard
// ════════════════════════════════════════
static unsigned long s_last_enter_ms = 0;

// v0.8.5/6/7: 相框+人像状态 NVS 记忆 (重启保留)
// 🔴 v0.8.7: 旧版每次按键直接写 3 个 key — 按住键会以 loop 频率反复写 flash (NVS 有磨损上限),
// 且三键非原子 (掉电可能 frameOn 与 frameIdx 不一致)。改为"脏标志 + 单 blob 原子写 + 停手后落盘"。
struct PrefBlob {
  uint8_t ver;         // 2 = v0.8.8 (人像模式已移除); 1 = v0.8.7 (多一个 portraitOn)
  uint8_t frameOn;
  uint8_t frameIdx;
} __attribute__((packed));
#define PREFS_DWELL_MS 3000    // 停手 3s 后落盘

static bool     s_prefs_dirty    = false;
static uint32_t s_prefs_dirty_ms = 0;

static void saveFramePrefs() {     // 只标脏 — 真正的写入推迟到 flushFramePrefs()
  s_prefs_dirty = true;
  s_prefs_dirty_ms = millis();
}

static void flushFramePrefs(bool force = false) {
  if (!s_prefs_dirty) return;
  if (!force && millis() - s_prefs_dirty_ms < PREFS_DWELL_MS) return;
  Preferences prefs; prefs.begin("gbcam", false);
  PrefBlob b = { 2,
                 (uint8_t)(UIManager::frameOn ? 1 : 0),
                 (uint8_t)UIManager::frameIdx };
  prefs.putBytes("prefs", &b, sizeof(b));
  prefs.end();
  s_prefs_dirty = false;
}

static void loadFramePrefs() {
  Preferences prefs; prefs.begin("gbcam", true);
  // v0.8.8: 读原始字节 (3 字节新 blob / 4 字节旧 blob) — 旧版按 struct 长度直接读,
  // 结构一变就会越界; 兼容 v0.8.7 的 4 字节 blob 与更早的 3 个独立 key
  uint8_t raw[4] = {0, 0, 0, 0};
  size_t  n = prefs.getBytesLength("prefs");
  if ((n == 3 || n == 4) && prefs.getBytes("prefs", raw, n) == n &&
      raw[0] >= 1 && raw[0] <= 2) {
    UIManager::frameOn  = raw[1] ? true : false;
    UIManager::frameIdx = raw[2];
  } else {
    UIManager::frameOn  = prefs.getBool("frameOn", false);
    UIManager::frameIdx = prefs.getInt("frameIdx", 0);
  }
  prefs.end();
  if (UIManager::frameIdx < 0 || UIManager::frameIdx > 3) UIManager::frameIdx = 0;
}

static void handleCameraKeys() {
  // 🔴 v0.8.7: 全部改走 UIManager 边沿检测 (pollKeys() 已在 loop 顶部调用)。
  // 旧版是 isChange() 门 + isKeyPressed() → 按住键每轮都触发 (v0.8.4i 已记录松键也触发),
  // 表现为模式反复翻转 + 每次翻转写 NVS。
  // v0.8.8: 取景帮助页 (h) — 打开时接管键盘: 只认 h/q 关闭, Tab 下页 / e 上页
  if (UIManager::viewHelp >= 0) {
    if (UIManager::keyEdge('h') || UIManager::keyEdge('q')) {
      UIManager::viewHelp = -1;
      UIManager::clear();                  // 下一帧立即重绘取景
    } else if (UIManager::keyEdgeSpecial(UIManager::K_TAB)) {
      if (UIManager::viewHelp < 2) { UIManager::viewHelp++; UIManager::renderViewHelp(); }
    } else if (UIManager::keyEdge('e')) {
      if (UIManager::viewHelp > 0) { UIManager::viewHelp--; UIManager::renderViewHelp(); }
    }
    return;
  }
  if (UIManager::keyEdge('h')) { UIManager::viewHelp = 0; UIManager::renderViewHelp(); return; }

  if (UIManager::keyEdge('r')) { flushFramePrefs(true); UIManager::enterGallery(); return; }

  if (UIManager::keyEdgeSpecial(UIManager::K_ENTER)) {
    unsigned long now = millis();
    if (now - s_last_enter_ms > ENTER_DEBOUNCE_MS) {
      s_last_enter_ms = now;
      UIManager::captureFrame(s_last_render_buf, s_last_render_len, UIManager::filterMode);
    }
    return;
  }

  for (char c = '1'; c <= '9'; c++) {
    if (UIManager::keyEdge(c)) { UIManager::filterMode = c - '1'; return; }   // 1..9 → 0..8
  }

  // v0.8.5: 像素相框 — 0=开关, ,=左切, /=右切 (与 1-9 滤镜正交)
  if (UIManager::keyEdge('0')) { UIManager::frameOn = !UIManager::frameOn; saveFramePrefs(); }
  if (UIManager::keyEdge(',')) { UIManager::frameIdx = (UIManager::frameIdx + 3) % 4; saveFramePrefs(); }
  if (UIManager::keyEdge('/')) { UIManager::frameIdx = (UIManager::frameIdx + 1) % 4; saveFramePrefs(); }
  // v0.8.8: 人像模式整块移除 (v0.8.6 的失败产物 — 用户判定"失败产物"并要求删除)
  // 相机模式 v 键释放, 不做任何事; 相册模式 v 仍是"全部/收藏"视图切换

  // EV: 上升沿 + 按住连发 (120ms/步, 纯内存, 不写 NVS)
  if (UIManager::exposureEV < 3  && UIManager::keyEdgeRepeat('+', 120)) UIManager::exposureEV++;
  if (UIManager::exposureEV > -3 && UIManager::keyEdgeRepeat('-', 120)) UIManager::exposureEV--;
}

// ════════════════════════════════════════
// Setup
// ════════════════════════════════════════
void setup() {
  M5Cardputer.begin();
  Serial.begin(115200);
  UIManager::init();
  loadFramePrefs();   // v0.8.5: 相框状态 (NVS, 不依赖 SD/WiFi)

  Serial.printf("GBCAMS %s (proto %s)\n", APP_VERSION, GBCAM_PROTO_VER);
  // v0.8.7: 启动信息行 — 现场没有串口时, 这是唯一能确认"版本 + 是否真有 PSRAM"的途径
  snprintf(g_boot_info, sizeof(g_boot_info), "%s · PSRAM %uKB",
           APP_VERSION, (unsigned)(ESP.getPsramSize() / 1024));
  Serial.printf("[boot] %s  free=%uKB\n", g_boot_info, (unsigned)(ESP.getFreeHeap() / 1024));

  // ── SD init 放到模式确定后 (避免干扰 WiFi 射频校准) ──
  // 这里先不初始化, 进入取景模式后再 init

  // ── 启动探测: 先听 ESP-NOW ──
  if (!enterEspNowListen()) {
    s_mode = MODE_WAIT_RETRY;    // 启动失败 → 重试循环 (不卡死)
  } else {
    s_mode = MODE_DETECT_ESPNOW;
  }
  s_mode_start = millis();
  s_search_tick = 0;

  s_fps_last = millis();
}

// ════════════════════════════════════════
// Main Loop
// ════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // v0.8.7: 键盘轮询集中一处 (M5Cardputer.update() + 边沿掩码), 在帧率门之外
  UIManager::pollKeys();

  // ── Gallery mode ──
  if (UIManager::galleryState != UIManager::GALLERY_NONE) {
    UIManager::galleryHandleKeys();
    UIManager::galleryUpdate();
    delay(30);
    return;
  }

  // ── Camera keyboard ──
  handleCameraKeys();
  flushFramePrefs();      // v0.8.7: 停手 3s 后落盘 (不再每次按键写 NVS)

  // ── beacon 重发 (探测 + 正式模式共用) ──
  // 关键: 探测阶段也必须持续发 beacon, 否则 CAMS3 永远等不到 → 死锁
  if (s_beacon && now - s_beacon_last >= BEACON_INTERVAL_MS) {
    s_beacon_last = now;
    s_beacon->send_beacon();
  }

  // ═══ 探测阶段 ═══
  switch (s_mode) {
  case MODE_DETECT_ESPNOW:
    detectEspNowTick();
    // 等待画面: ESP-NOW 监听阶段
    UIManager::drawSearching(0, s_search_tick++);
    delay(30);
    break;

  case MODE_DETECT_SCAN:
    detectScanTick();
    // 等待画面: WiFi 扫描阶段
    UIManager::drawSearching(1, s_search_tick++);
    delay(30);
    break;

  case MODE_WAIT_RETRY:
    // 等待画面: 重试等待阶段
    UIManager::drawSearching(2, s_search_tick++);
    if (now - s_mode_start > RETRY_WAIT_MS) {
      teardownEspNow();              // 关键: 清理旧 ESP_NOW 实例 (重复 begin 会失败)
      if (enterEspNowListen()) {
        s_mode = MODE_DETECT_ESPNOW;
        s_mode_start = now;
      } else {
        s_mode_start = now;          // 失败 → 继续等待重试
      }
    }
    delay(30);
    return;

  case MODE_ESPNOW:
    // ── 帧超时 (v0.8.7: 只置请求, 重组状态由回调清 — 消除跨核竞态) ──
    {
      static unsigned long s_last_abort_ms = 0;
      if (s_frame_active && s_recv_pkts > 0 && s_recv_pkts < s_total_pkts &&
          now - s_frame_start_ms > FRAME_TIMEOUT_MS && now - s_last_abort_ms > 500) {
        s_last_abort_ms = now;
        s_frame_abort_req = true;      // v0.8.8: 软/硬判定与计数都在回调里 (单处所有权)
      }
    }
    // ── 10s 无帧 → 重新探测 (用户可能换了固件) ──
    if (s_ready_idx < 0 && now - s_mode_start > ESPNOW_STALL_MS &&
        now - s_last_frame_ms > ESPNOW_STALL_MS) {
      teardownEspNow();
      s_mode = MODE_DETECT_SCAN;
      s_mode_start = now;
    }
    break;

  case MODE_HTTP:
    if (s_mjpeg) {
      s_mjpeg->update();
      if (!s_mjpeg->isConnected()) {
        // 断流 → 重连, 2 次失败 → 重新探测
        if (++s_http_reconnect > HTTP_RECONNECT_MAX) {
          teardownHttp();
          if (enterEspNowListen()) {
            s_mode = MODE_DETECT_ESPNOW;
          } else {
            s_mode = MODE_WAIT_RETRY;   // 失败 → 重试循环
          }
          s_mode_start = now;
        } else {
          s_mjpeg->begin();
        }
      }
    }
    break;

  default:
    if (enterEspNowListen()) {
      s_mode = MODE_DETECT_ESPNOW;
    } else {
      s_mode = MODE_WAIT_RETRY;   // 失败 → 重试循环
    }
    s_mode_start = now;
    break;
  }

  // ═══ 渲染 (两种模式共用) ═══
  // v0.8.8: 取景帮助页打开时不消费帧 (画面停在帮助页); 关闭后下一帧立即恢复
  if (s_ready_idx >= 0 && UIManager::viewHelp < 0) {
    int idx = s_ready_idx;
    size_t len = s_ready_len;
    s_ready_idx = -1;  // consume

    // v0.8.7: 单一分派点 (取景与拍照重渲染共用 — 旧版两份 mode→函数表容易失同步)
    bool rendered = UIManager::renderFiltered(s_frame_buf[idx], len, UIManager::filterMode, false);
    if (!rendered) g_jpeg_fail++;      // v0.8.7: drawJpg 失败累积 (HUD 事件提示)
    if (rendered) {
      s_fps_cnt++;
      s_last_render_buf = s_frame_buf[idx];
      s_last_render_len = len;
    }
  }

  // ═══ FPS counter ═══
  if (now - s_fps_last >= 1000) {
    currentFps = s_fps_cnt;
    s_fps_cnt = 0;
    s_fps_last = now;
  }
}
