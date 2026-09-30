// ============================================================
// GBCAMS — 双 ESP32 架构
// Cardputer (ESP32-S3FN8 无 PSRAM) + CAMS3 (OV5640+PSRAM)
// Canvas 内部RAM双缓冲，WiFi通信
// ============================================================
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>
#include "harness.h"

#define WIFI_SSID "zhuzhuhome"
#define WIFI_PASS "1234567890"
#define W 240
#define H 135

// ── Canvas 双缓冲（内部 RAM，无 PSRAM） ───────────────────
static M5Canvas canvas(&M5.Display);
static bool g_canvasOK = false;

// ── 全局 ──────────────────────────────────────────────────
enum Mode { M_MONITOR, M_CAPTURE, M_HELP };
static Mode g_mode = M_MONITOR;
static bool g_wifiOK = false;
static int g_rxLen = 0;

static WebServer server(80);
static HardwiredTestHarness hrns;
static bool g_hrnsReady = false;

// ── WiFi ───────────────────────────────────────────────────
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(200);
  g_wifiOK = (WiFi.status() == WL_CONNECTED);
}

static uint16_t fb[W * H];
static uint16_t* snapFb() {
  if (g_canvasOK && canvas.getBuffer()) {
    memcpy(fb, (uint16_t*)canvas.getBuffer(), W * H * 2);
  }
  return fb;
}

void initHarness() {
  if (!g_wifiOK) return;
  server.on("/", []() { server.send(200, "text/plain", "Cardputer Camera"); });
  hrns.begin(server, snapFb);
  server.begin();
  g_hrnsReady = true;
}

// ── 键盘 ──────────────────────────────────────────────────
static uint32_t g_prevMask = 0;
int getKey() {
  M5Cardputer.Keyboard.isChange();
  auto ks = M5Cardputer.Keyboard.keysState();
  if (ks.word.empty()) { g_prevMask = 0; return 0; }
  for (auto k : ks.word) {
    if (k == '+' || k == '=') return '+';
    if (k == '-' || k == '_') return '-';
    if (k == '\r' || k == '\n') return 13;
  }
  uint32_t mask = 0;
  for (auto k : ks.word) {
    char c = (k >= 'A' && k <= 'Z') ? k + 32 : k;
    if (c >= 'a' && c <= 'z') mask |= 1UL << (c - 'a');
    if (c >= '0' && c <= '9') mask |= 1UL << (26 + (c - '0'));
  }
  uint32_t edge = mask & ~g_prevMask;
  g_prevMask = mask;
  for (auto k : ks.word) {
    char c = (k >= 'A' && k <= 'Z') ? k + 32 : k;
    uint32_t bit = 0;
    if (c >= 'a' && c <= 'z') bit = 1UL << (c - 'a');
    if (c >= '0' && c <= '9') bit = 1UL << (26 + (c - '0'));
    if (bit && (edge & bit)) return k;
  }
  return 0;
}

// ── 渲染 ──────────────────────────────────────────────────
static void pStr(int x, int y, const char* s, uint16_t c, uint16_t b) {
  canvas.setTextColor(c, b); canvas.setCursor(x, y); canvas.print(s);
}
static void pFmt(int x, int y, uint16_t c, uint16_t b, const char* f, ...) {
  char buf[80]; va_list ap; va_start(ap, f); vsnprintf(buf, 80, f, ap); va_end(ap);
  canvas.setTextColor(c, b); canvas.setCursor(x, y); canvas.print(buf);
}

void drawMonitor() {
  if (g_canvasOK) canvas.fillSprite(TFT_BLACK);
  else { M5.Display.fillRect(0, 0, W, H, TFT_BLACK); return; }

  canvas.fillRect(0, 0, W, 10, TFT_NAVY);
  pFmt(2, 1, TFT_WHITE, TFT_NAVY, "CAMS3 CAM");
  int y = 28;
  pStr(10, y, "=== Dual ESP32 ===", TFT_WHITE, TFT_BLACK); y += 14;
  pStr(10, y, "CAMS3: ESP32-S3+OV5640 5MP", TFT_CYAN, TFT_BLACK); y += 10;
  pStr(10, y, "Cardputer: Stamp-S3A (no PSRAM)", TFT_CYAN, TFT_BLACK); y += 10;
  pFmt(10, y, TFT_WHITE, TFT_BLACK, "WiFi: %s", g_wifiOK ? WiFi.localIP().toString().c_str() : "N/A"); y += 10;
  pFmt(10, y, TFT_WHITE, TFT_BLACK, "Last RX: %d bytes", g_rxLen); y += 10;
  pStr(10, y, "Enter to snap via WiFi", TFT_GREEN, TFT_BLACK);
  canvas.fillRect(0, H - 12, W, 12, TFT_DARKGREY);
  pStr(2, H - 11, "[Enter] Snap [C]apture [H]elp +/-", TFT_WHITE, TFT_DARKGREY);
  canvas.pushSprite(0, 0);
}

void drawCapture() {
  if (g_canvasOK) canvas.fillSprite(TFT_BLACK);
  else { M5.Display.fillRect(0, 0, W, H, TFT_BLACK); return; }

  canvas.fillRect(0, 0, W, 10, TFT_NAVY);
  pFmt(2, 1, TFT_WHITE, TFT_NAVY, "SNAP");
  int y = 20;
  pStr(10, y, "Connect to CAMS3 AP via WiFi", TFT_WHITE, TFT_BLACK); y += 14;
  pFmt(10, y, TFT_WHITE, TFT_BLACK, "Last JPEG: %d bytes", g_rxLen); y += 10;
  pStr(10, y, "Press [Enter] to capture", TFT_GREEN, TFT_BLACK);
  canvas.fillRect(0, H - 12, W, 12, TFT_DARKGREY);
  pStr(2, H - 11, "[Enter] Snap [M]onitor [H]elp", TFT_WHITE, TFT_DARKGREY);
  canvas.pushSprite(0, 0);
}

void drawHelp() {
  if (g_canvasOK) canvas.fillSprite(TFT_NAVY);
  else { M5.Display.fillRect(0, 0, W, H, TFT_NAVY); return; }

  canvas.setTextColor(TFT_WHITE, TFT_NAVY);
  int y = 4;
  canvas.setCursor(4, y); canvas.print("Cardputer + CAMS3"); y += 10; y += 2;
  canvas.setCursor(4, y); canvas.print("Enter = WiFi camera snap"); y += 9;
  canvas.setCursor(4, y); canvas.print("C = Capture mode"); y += 9;
  canvas.setCursor(4, y); canvas.print("M = Monitor mode"); y += 9;
  canvas.setCursor(4, y); canvas.print("H = Help"); y += 9;
  canvas.setCursor(4, y); canvas.print("+/- = Brightness"); y += 9;
  y += 2;
  if (g_wifiOK) pFmt(4, y, TFT_GREEN, TFT_NAVY, "IP: %s", WiFi.localIP().toString().c_str());
  canvas.fillRect(0, H - 12, W, 12, TFT_DARKGREY);
  pStr(2, H - 11, "[any] back", TFT_WHITE, TFT_DARKGREY);
  canvas.pushSprite(0, 0);
}

// ── Brightness ─────────────────────────────────────────────
static const uint8_t BRI[] = { 20, 60, 100 };
static int g_bri = 2;
void adjBri(int d) { g_bri += d; if (g_bri < 0) g_bri = 0; if (g_bri >= 3) g_bri = 2; M5.Display.setBrightness(BRI[g_bri]); }

// ── WiFi Snap ──────────────────────────────────────────────
void doSnap() {
  WiFiClient c;
  if (!c.connect("192.168.4.1", 80, 3000)) return;
  c.print("GET /snap HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n");
  unsigned long t0 = millis();
  while (!c.available() && millis() - t0 < 3000) delay(1);
  char buf[256];
  while (c.available()) {
    int n = c.read((uint8_t*)buf, 255);
    if (n <= 0) break; buf[n] = '\0';
    char* s = strstr(buf, "\r\n\r\n");
    if (s) break;
  }
  g_rxLen = 0;
  while (c.available() && g_rxLen < 50000) { c.read(); g_rxLen++; }
  c.stop();
}

// ── Setup ──────────────────────────────────────────────────
void setup() {
  delay(300);
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5Cardputer;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  cfg.external_amp = false;
  M5Cardputer.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(100);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::Font0);

  // Canvas — 从内部 RAM 分配双缓冲
  g_canvasOK = canvas.createSprite(W, H);

  initWiFi();
  initHarness();

  // Splash
  if (g_canvasOK) canvas.fillSprite(TFT_BLACK);
  else M5.Display.fillRect(0, 0, W, H, TFT_BLACK);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setTextSize(2);
  canvas.setCursor(15, 25); canvas.print("CAMS3 CAM");
  canvas.setTextSize(1);
  canvas.setCursor(15, 50); canvas.print("Stamp-S3A no PSRAM");
  canvas.setCursor(15, 65); canvas.print("Enter to snap via WiFi");
  if (g_wifiOK) {
    canvas.setCursor(15, 80); canvas.print(WiFi.localIP().toString().c_str());
  }
  if (g_canvasOK) canvas.pushSprite(0, 0);
  delay(2000);
}

// ── Loop ───────────────────────────────────────────────────
void loop() {
  M5Cardputer.update();
  if (g_hrnsReady) { server.handleClient(); hrns.tick(); }

  int k = getKey();
  if (k == 'c' || k == 'C') g_mode = (g_mode == M_CAPTURE) ? M_MONITOR : M_CAPTURE;
  else if (k == 'm' || k == 'M') g_mode = M_MONITOR;
  else if (k == 'h' || k == 'H') g_mode = (g_mode == M_HELP) ? M_MONITOR : M_HELP;
  else if (k == '+') adjBri(1);
  else if (k == '-') adjBri(-1);
  else if (k == 13) doSnap();

  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 50) return;
  last = now;

  switch (g_mode) {
    case M_MONITOR: drawMonitor(); break;
    case M_CAPTURE: drawCapture(); break;
    case M_HELP:    drawHelp();    break;
  }
}
