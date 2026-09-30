/**
 * CAMS3-5MP — ESP-NOW MJPEG Sender v0.0.8
 *
 * Replaces WiFi AP + HTTP MJPEG streaming with direct ESP-NOW unicast.
 * Automatically discovers Cardputer via a beacon handshake, then
 * switches to unicast with 54Mbps PHY rate.
 *
 * Packet format:
 *   [frame_id:4][total_pkts:2][pkt_index:2][jpeg_data:0..1450]
 *
 * v0.0.8: ESP-NOW v2.0 大包 — PKT_DATA_MAX 240→1450 (12KB 帧 52→9 包,
 * 帧丢失暴露面降 ~4-5×)。pacing 200→280µs (1450B@54Mbps 空中时长 ≈222µs)。
 * 库零改动 (Peer::send 自动查 getMaxDataLen, v2.0=1470)。
 *
 * Discovery protocol:
 *   1. CAMS3 starts, sends initial frames via broadcast
 *   2. Cardputer sends a 1-byte beacon (0x55) after boot
 *   3. CAMS3 receives beacon → adds Cardputer as unicast peer @ 54Mbps
 *   4. All subsequent frames use unicast → rate config works
 *   5. Cardputer re-sends beacon every 3s until it receives video data
 *
 * Changelog:
 *   v0.0.2 — Initial ESP-NOW (broadcast, 1Mbps)
 *   v0.0.3 — Deprecated rate APIs + flow control
 *   v0.0.4 — WiFi TX rate set + inter-packet pacing
 *   v0.0.5 — Unicast peer discovery + guaranteed 54Mbps via real peer
 *   v0.0.6 — Fix: LED heartbeat now differentiates connected vs searching
 *          — Fix: deduplicated send_frame logic (common free function)
 *          — Fix: LED heartbeat non-blocking (no delay() in main loop)
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "ESP32_NOW.h"
#include <esp_mac.h>

// ── Camera Pin Config (PY260 / BF3005) ──
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   21
#define XCLK_GPIO_NUM    11
#define SIOD_GPIO_NUM    17
#define SIOC_GPIO_NUM    41
#define Y9_GPIO_NUM      13
#define Y8_GPIO_NUM      4
#define Y7_GPIO_NUM      10
#define Y6_GPIO_NUM      5
#define Y5_GPIO_NUM      7
#define Y4_GPIO_NUM      16
#define Y3_GPIO_NUM      15
#define Y2_GPIO_NUM      6
#define VSYNC_GPIO_NUM   42
#define HREF_GPIO_NUM    18
#define PCLK_GPIO_NUM    12

#define LED_PIN          14

// ── ESP-NOW Config ──
#define ESPNOW_CHANNEL   6
// v0.0.9: 协议标识 — 与 cardputer/src/main.cpp 的 GBCAM_PROTO_VER 保持一致
// (两端启动各打印一次, 现场可一眼对齐; 改动包长/包头时两处同步)
#define GBCAM_PROTO_VER "2.0"
#define PKT_DATA_MAX  1450      // v2.0 大包 (上限 1470, 留 20B 余量; v0.0.8)
#define HEADER_LEN       8           // frame_id:4 + total:2 + index:2
#define BEACON_MAGIC     0x55        // Cardputer's "I'm here" beacon

// ── 54Mbps PHY Rate Config ──
static const esp_now_rate_config_t s_rate54 = {
  .phymode = WIFI_PHY_MODE_11G,
  .rate    = WIFI_PHY_RATE_54M,
  .ersu    = false,
  .dcm     = false
};

// ── LED blink timing (non-blocking state machine) ──
#define BLINK_FAST_MS     200    // 200ms on/off = 5 blinks in 2s period
#define BLINK_PERIOD_MS   2000   // blink state machine period (2s cycle)

// ── Forward declarations ──
class CamUnicastPeer;

// ── Broadcast Peer (used until Cardputer is discovered) ──
class CamBroadcaster : public ESP_NOW_Peer {
public:
  CamBroadcaster(uint8_t ch)
    : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, ch, WIFI_IF_STA, nullptr) {}
  ~CamBroadcaster() { remove(); }
  bool begin() { return add(); }
  void remove_peer() { remove(); }

  /// Send a frame split into ESP-NOW packets.
  int send_frame(const uint8_t *jpeg, size_t len, uint32_t frame_id) {
    uint16_t total = (len + PKT_DATA_MAX - 1) / PKT_DATA_MAX;
    uint8_t pkt[HEADER_LEN + PKT_DATA_MAX];
    size_t offset = 0;
    int sent = 0;
    for (uint16_t i = 0; i < total; i++) {
      size_t chunk = (len - offset > PKT_DATA_MAX) ? PKT_DATA_MAX : len - offset;
      memcpy(pkt, &frame_id, 4);
      memcpy(pkt + 4, &total, 2);
      memcpy(pkt + 6, &i, 2);
      memcpy(pkt + HEADER_LEN, jpeg + offset, chunk);
      if (send(pkt, HEADER_LEN + chunk) == 0) break;
      sent++;
      offset += chunk;
      delayMicroseconds(280);   // v0.0.8: pacing > 1450B@54Mbps 空中时长(≈222µs), 防队列积压
    }
    return sent;
  }
};

// ── Unicast Peer (created after discovery, with 54Mbps rate) ──
class CamUnicastPeer : public ESP_NOW_Peer {
public:
  CamUnicastPeer(const uint8_t *mac, uint8_t ch)
    : ESP_NOW_Peer(mac, ch, WIFI_IF_STA, nullptr, (esp_now_rate_config_t*)&s_rate54) {}
  ~CamUnicastPeer() { remove(); }
  bool begin() { return add(); }

  /// Send a frame split into ESP-NOW packets.
  int send_frame(const uint8_t *jpeg, size_t len, uint32_t frame_id) {
    uint16_t total = (len + PKT_DATA_MAX - 1) / PKT_DATA_MAX;
    uint8_t pkt[HEADER_LEN + PKT_DATA_MAX];
    size_t offset = 0;
    int sent = 0;
    for (uint16_t i = 0; i < total; i++) {
      size_t chunk = (len - offset > PKT_DATA_MAX) ? PKT_DATA_MAX : len - offset;
      memcpy(pkt, &frame_id, 4);
      memcpy(pkt + 4, &total, 2);
      memcpy(pkt + 6, &i, 2);
      memcpy(pkt + HEADER_LEN, jpeg + offset, chunk);
      if (send(pkt, HEADER_LEN + chunk) == 0) break;
      sent++;
      offset += chunk;
      delayMicroseconds(280);   // v0.0.8: pacing > 1450B@54Mbps 空中时长(≈222µs), 防队列积压
    }
    return sent;
  }
};

// ── Peer state ──
static CamBroadcaster *s_broadcaster = nullptr;
static CamUnicastPeer *s_unicast = nullptr;
static bool            s_cardputer_known = false;
static uint8_t         s_cardputer_mac[6] = {0};

// ── Globals ──
static unsigned long s_frameCount = 0;
static unsigned long s_t0 = 0;
static bool g_camOK = false;

// ── LED blink state machine ──
// When unicast (connected): fast blink, dark most of the time (short bright bursts)
// When broadcast (searching): slow blink, even duty cycle
static enum { LED_IDLE, LED_BURST_ON, LED_BURST_OFF } s_led_state = LED_IDLE;
static unsigned long s_led_timer = 0;
static int s_led_burst_count = 0;  // how many fast blinks in this period

static void led_blink_tick(unsigned long now) {
  // Period: 2 seconds
  if (now - s_led_timer < BLINK_PERIOD_MS) {
    // Handle in-period transitions
    if (s_cardputer_known) {
      // Connected mode: 5 fast bursts in 2 seconds (each 200ms on, 200ms off)
      if (s_led_state == LED_BURST_ON && now - s_led_timer >= 200) {
        digitalWrite(LED_PIN, LOW);
        s_led_state = LED_BURST_OFF;
        s_led_timer = now;
        s_led_burst_count++;
      } else if (s_led_state == LED_BURST_OFF && now - s_led_timer >= 200) {
        if (s_led_burst_count < 5) {
          digitalWrite(LED_PIN, HIGH);
          s_led_state = LED_BURST_ON;
          s_led_timer = now;
        } else {
          // Stay dark until next period
        }
      }
    } else {
      // Searching mode: slow on/off, 1s each
      if (s_led_state == LED_BURST_ON && now - s_led_timer >= 1000) {
        digitalWrite(LED_PIN, LOW);
        s_led_state = LED_BURST_OFF;
        s_led_timer = now;
      } else if (s_led_state == LED_BURST_OFF && now - s_led_timer >= 1000) {
        digitalWrite(LED_PIN, HIGH);
        s_led_state = LED_BURST_ON;
        s_led_timer = now;
      }
    }
    return;
  }

  // Start new period
  s_led_timer = now;
  s_led_burst_count = 0;
  digitalWrite(LED_PIN, HIGH);
  s_led_state = LED_BURST_ON;
}

// ── ESP-NOW Discovery Callback ──
// Fires for every packet from an unknown sender.
// Catches Cardputer's beacon and extracts its MAC.
void on_discovery(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  if (s_cardputer_known) return;  // already discovered

  // Look for Cardputer's beacon (1-byte magic)
  if (len >= 1 && data[0] == BEACON_MAGIC) {
    memcpy(s_cardputer_mac, info->src_addr, 6);
    s_cardputer_known = true;
    Serial.printf("[disc] Cardputer found: " MACSTR "\n", MAC2STR(s_cardputer_mac));

    // Switch from broadcast to unicast with 54Mbps rate
    if (s_broadcaster) s_broadcaster->remove_peer();
    static CamUnicastPeer peer(s_cardputer_mac, ESPNOW_CHANNEL);
    s_unicast = &peer;
    if (peer.begin()) {
      Serial.println("[disc] Unicast peer added @ 54Mbps");
    } else {
      Serial.println("[disc] Unicast peer add FAIL");
    }
  }
}

// ── Setup ──
void setup() {
  Serial.begin(115200); delay(100);
  // v0.0.9: 版本串此前一直停在 v0.0.7 (实际已是 v0.0.8) — 改为与 tag 同步, 并打印协议/包长
  Serial.printf("\n[GBCAMS] UnitCamS3 ESP-NOW Sender v0.0.9 (proto %s, PKT_DATA_MAX %d)\n",
                GBCAM_PROTO_VER, PKT_DATA_MAX);
  Serial.printf("PSRAM: %d KB\n", ESP.getPsramSize() / 1024);

  // Reset camera module
  pinMode(RESET_GPIO_NUM, OUTPUT);
  digitalWrite(RESET_GPIO_NUM, LOW); delay(50);
  digitalWrite(RESET_GPIO_NUM, HIGH); delay(200);

  // Camera config
  camera_config_t config = {};
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = -1;
  config.sccb_i2c_port = 1;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.frame_size    = FRAMESIZE_QVGA;
  config.jpeg_quality  = 8;
  config.fb_count      = 3;
  config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location   = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  Serial.printf("cam_init = 0x%x\n", err);
  if (err == ESP_OK) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
      s->set_framesize(s, FRAMESIZE_QVGA);
      s->set_quality(s, 8);
      // ── 暗光增强调参 (方案A, v0.0.7) ──
      // mega_ccm 驱动只支持以下寄存器 (其余 set_* 是 dummy 假函数):
      //   AGC_MODE_REG(0x30) / MANUAL_AGC_REG(0x31) / MANUAL_EXP_H/L(0x33/0x34)
      //   BRIGHTNESS(0x22) / CONTRAST(0x23) / SATURATION(0x24) / AWB_MODE(0x26)
      // 提亮链路: 关AGC自动 → 手动增益 → 拉长曝光 → 亮度偏置 → 高对比度
      if (s->set_aec2)         s->set_aec2(s, 0);           // 关 AGC 自动 (手动增益生效前提, mega_ccm 的 set_agc_mode 装在 aec2 槽位)
      if (s->set_agc_gain)     s->set_agc_gain(s, 14);       // 模拟增益 14/30 (暗光提亮, 噪点可控)
      if (s->set_aec_value)    s->set_aec_value(s, 800);     // 曝光 800 行 (默认300, 拉长2.6x)
      if (s->set_brightness)   s->set_brightness(s, 5);      // 亮度偏置 5/8 (中高)
      if (s->set_contrast)     s->set_contrast(s, 4);        // 对比度 4/6 (灰阶层次)
      if (s->set_saturation)   s->set_saturation(s, 0);      // 去饱和 (GB 灰阶, 减少色噪)
      if (s->set_hmirror)      s->set_hmirror(s, 0);         // 保持默认
      if (s->set_vflip)        s->set_vflip(s, 0);           // 保持默认
      // 报告实际生效的参数
      Serial.printf("[cam] agc_gain=%d aec=%d bright=%d contrast=%d sat=%d\n",
        s->status.agc_gain, s->status.aec_value,
        s->status.brightness, s->status.contrast, s->status.saturation);
      g_camOK = true;
      Serial.printf("Camera OK! PID=0x%04x\n", s->id.PID);
    }
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, g_camOK ? HIGH : LOW);

  // ── WiFi STA for ESP-NOW ──
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_CHANNEL);
  WiFi.setSleep(false);
  // 🔴 v0.0.9: 原来是 `while (!WiFi.STA.started()) delay(10);` 无界阻塞 —
  // 射频校准失败时固件永久卡在 setup (LED 亮, 无流, 无任何日志), 接收端只看到"等待 CAMS3"。
  // 改为 5s 超时 + 重新初始化重试 (与 Cardputer 接收端同款处理)。
  {
    unsigned long t0 = millis();
    while (!WiFi.STA.started()) {
      if (millis() - t0 > 5000) {
        Serial.println("[wifi] STA start timeout — re-init (5s/轮)");
        WiFi.mode(WIFI_OFF); delay(200);
        WiFi.mode(WIFI_STA);
        WiFi.setChannel(ESPNOW_CHANNEL);
        WiFi.setSleep(false);
        t0 = millis();
      }
      delay(10);
    }
  }
  Serial.printf("STA MAC: %s, Channel: %d\n",
    WiFi.macAddress().c_str(), ESPNOW_CHANNEL);

  // ── ESP-NOW Init ──
  if (!ESP_NOW.begin()) {
    Serial.println("ESP-NOW init FAIL");
    return;
  }
  Serial.printf("ESP-NOW version=%d, max_data=%d\n",
    ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());

  // ── Register discovery callback ──
  ESP_NOW.onNewPeer(on_discovery, nullptr);

  // ── Start in broadcast mode (switches to unicast on discovery) ──
  static CamBroadcaster broadcaster(ESPNOW_CHANNEL);
  s_broadcaster = &broadcaster;
  if (!broadcaster.begin()) {
    Serial.println("Broadcast peer add FAIL");
  } else {
    Serial.println("Broadcast mode active — waiting for Cardputer beacon...");
  }

  s_t0 = millis();
  s_led_timer = millis();
  s_led_state = LED_BURST_ON;
  digitalWrite(LED_PIN, HIGH);
}

// ── Main Loop ──
void loop() {
  if (!g_camOK) { delay(100); return; }

  unsigned long now = millis();

  // LED blink (non-blocking state machine)
  led_blink_tick(now);

  // Capture frame
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { delay(1); return; }

  static uint32_t frame_id = 1;

  if (s_cardputer_known && s_unicast) {
    s_unicast->send_frame(fb->buf, fb->len, frame_id++);
  } else if (s_broadcaster) {
    s_broadcaster->send_frame(fb->buf, fb->len, frame_id++);
  }
  esp_camera_fb_return(fb);
  s_frameCount++;

  // Serial stats every 5s
  if (now - s_t0 >= 5000) {
    Serial.printf("[cam] %u frames/5s = %.1f fps [%s]\n",
      (unsigned)s_frameCount, s_frameCount / 5.0f,
      s_cardputer_known ? "UNICAST 54M" : "BROADCAST");
    s_frameCount = 0;
    s_t0 = now;
  }
}
