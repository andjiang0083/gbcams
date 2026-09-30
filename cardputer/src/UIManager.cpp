#include "UIManager.h"
#include "Global.h"

#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <algorithm>

M5Canvas canvas(&M5Cardputer.Display);
M5Canvas mainCanvas(&M5Cardputer.Display);

int UIManager::filterMode = 0;
int UIManager::exposureEV  = 0;
const char* UIManager::modeLabel = "";
uint16_t    UIManager::modeLabelColor = TFT_CYAN;

// ── EV brightness factors (256× scale) — -3 to +3 ──
static const uint16_t ev_fac[7] = {90, 128, 181, 256, 362, 512, 724};

// ── 4×4 Bayer ordered dither matrix (0-15) — per-pixel dither ──
static const int s_bayer[4][4] = {
  {12, 4,14, 6},
  { 0, 8, 2,10},
  {15, 7,13, 5},
  { 3,11, 1, 9}
};
// ── 8×8 Bayer ordered dither matrix (0-63) — smoother gradients ──
// Standard recursive Bayer pattern; 64 thresholds vs 16 of 4×4.
// Used by GB/Classical filters for finer simulated shades.
static const int s_bayer8[8][8] = {
  { 0,32, 8,40, 2,34,10,42},
  {48,16,56,24,50,18,58,26},
  {12,44, 4,36,14,46, 6,38},
  {60,28,52,20,62,30,54,22},
  { 3,35,11,43, 1,33, 9,41},
  {51,19,59,27,49,17,57,25},
  {15,47, 7,39,13,45, 5,37},
  {63,31,55,23,61,29,53,21}
};
// Per-channel shifts for GBC uncorrelated dither
static const int s_bayer_g[4][4] = {{ 6,14, 8, 0},{ 2,10, 4,12},{ 9, 1, 7,15},{ 5,13,11, 3}};
static const int s_bayer_b[4][4] = {{11, 3, 9, 1},{ 7,15, 5,13},{14, 6,12, 4},{10, 2, 8, 0}};

// ── Palettes ──
#define FRAME_INSET 4   // v0.8.5: 拍照时内容四周留白给相框 (letterbox)

// ── v0.8.7: 取景缩放 ──
// 1.125f = 160×120 整幅放进 240×135 (左右各留 30px) → 所见即所得, 不裁画面
// 1.5f   = 填满宽度, 但上下各裁掉 ~22px (≈25% 画面) — v0.8.6 及以前的行为,
//          取景看到的构图与保存的照片 (整幅 160×120) 不一致
// 保存的照片始终是完整帧, 不因这里的选择丢像素; 想回退只改这一行
#define VIEW_SCALE 1.125f
// v0.8.8: 画面靠左 180×135 (160×120 @1.125) + 右侧 60px 侧栏 — 原"黑边"改成 HUD 功能区
#define SIDEBAR_X  180
#define VIEW_CX     90          // 画面中心 x (240→180: 0..179)
#define GB_LEVELS  8
#define CLS_LEVELS 4
#define BR_LEVELS  8
#define DMG_LEVELS 4
static uint16_t s_gb_palette[GB_LEVELS];
static uint16_t s_cls_palette[CLS_LEVELS];
static uint16_t s_br1_palette[BR_LEVELS];   // The Backrooms — 复古旧壁纸黄 (暗/浓郁)
static uint16_t s_br2_palette[BR_LEVELS];   // The Backrooms — 荧光灯黄白 (亮/刺眼)
static uint16_t s_dmg_palette[DMG_LEVELS];  // 真 DMG — 经典 4 阶绿 (#0F380F→#9BBC0F)

static void build_palettes() {
  for (int i = 0; i < GB_LEVELS; i++) {
    int v5 = i * 31 / (GB_LEVELS - 1);
    int v6 = i * 63 / (GB_LEVELS - 1);
    s_gb_palette[i] = (v5 << 11) | (v6 << 5) | v5;
  }
  for (int i = 0; i < CLS_LEVELS; i++) {
    int r5 = 2  + i * (19 - 2)  / (CLS_LEVELS - 1);
    int g6 = 14 + i * (46 - 14) / (CLS_LEVELS - 1);
    int b5 = 2;
    s_cls_palette[i] = (r5 << 11) | (g6 << 5) | b5;
  }
  // BR-1: 旧壁纸黄 — 暗褐浓郁, G 通道占优 (r5: 4→31, g6: 10→63, b5: 2→20)
  const int br1_r[BR_LEVELS] = { 4,  8, 12, 16, 20, 24, 28, 31};
  const int br1_g[BR_LEVELS] = {10, 18, 26, 34, 42, 50, 57, 63};
  const int br1_b[BR_LEVELS] = { 2,  4,  7, 10, 13, 16, 18, 20};
  // BR-2: 荧光灯黄白 — 亮白刺眼, 接近过曝 (r5: 8→31, g6: 16→63, b5: 4→28)
  const int br2_r[BR_LEVELS] = { 8, 12, 16, 20, 24, 27, 30, 31};
  const int br2_g[BR_LEVELS] = {16, 24, 32, 40, 48, 54, 60, 63};
  const int br2_b[BR_LEVELS] = { 4,  8, 12, 16, 20, 23, 26, 28};
  for (int i = 0; i < BR_LEVELS; i++) {
    s_br1_palette[i] = (br1_r[i] << 11) | (br1_g[i] << 5) | br1_b[i];
    s_br2_palette[i] = (br2_r[i] << 11) | (br2_g[i] << 5) | br2_b[i];
  }

  // DMG: 经典 Game Boy 4 阶绿 (暗→亮: #0F380F / #306230 / #8BAC0F / #9BBC0F)
  // RGB888→RGB565 精确换算: r5=round(R*31/255), g6=round(G*63/255), b5=round(B*31/255)
  const int dmg_r[DMG_LEVELS] = { 1,  5, 16, 18};
  const int dmg_g[DMG_LEVELS] = {13, 24, 42, 46};
  const int dmg_b[DMG_LEVELS] = { 1,  5,  1,  1};
  for (int i = 0; i < DMG_LEVELS; i++)
    s_dmg_palette[i] = (dmg_r[i] << 11) | (dmg_g[i] << 5) | dmg_b[i];
}

// ════════════════════════════════════════
// v0.8.7: 键盘边沿检测
// 旧实现的坑: isChange() 门 + isKeyPressed() → 按住键每轮 loop 都触发 (v0.8.4i 已记录
// "松键也触发一次"), 动作键 (滤镜/相框/人像/移文件) 因此会反复执行, 相册里甚至会
// 一边导航一边搬文件。改为: 每轮一次 keyboard update → 位掩码上升沿; 导航键额外连发。
// ════════════════════════════════════════
static const char s_tracked[] = "0123456789abcdefghijklmnopqrstuvwxyz,/+=-";
#define K_BIT_ENTER     (1ULL << 44)
#define K_BIT_BACKSPACE (1ULL << 45)
#define K_BIT_TAB       (1ULL << 46)

static uint64_t s_prev_mask = 0, s_edge_mask = 0;
static uint32_t s_repeat_next_ms[47] = {0};

static uint64_t buildKeyMask() {
  uint64_t m = 0;
  auto &kb = M5Cardputer.Keyboard;
  for (int i = 0; s_tracked[i]; i++) {
    char c = s_tracked[i];
    bool down = kb.isKeyPressed(c);
    if (c == '+') down = down || kb.isKeyPressed('=');   // 同一物理键
    if (c == '-') down = down || kb.isKeyPressed('_');
    if (down) m |= (1ULL << i);
  }
  if (kb.isKeyPressed(KEY_ENTER))     m |= K_BIT_ENTER;
  if (kb.isKeyPressed(KEY_BACKSPACE)) m |= K_BIT_BACKSPACE;
  if (kb.isKeyPressed(KEY_TAB))       m |= K_BIT_TAB;
  return m;
}
static int trackedBit(char c) {
  if (c == '=') c = '+';
  const char *p = strchr(s_tracked, c);
  if (!p || *p == '\0') return -1;
  return (int)(p - s_tracked);
}
static uint64_t specialBit(UIManager::SpecialKey k) {
  return (k == UIManager::K_ENTER)     ? K_BIT_ENTER :
         (k == UIManager::K_BACKSPACE) ? K_BIT_BACKSPACE : K_BIT_TAB;
}
// 上升沿触发一次; 之后按住时每 ms 毫秒再触发一次
static bool repeatForBit(uint64_t b, uint32_t ms) {
  int i = __builtin_ctzll(b);
  if (s_edge_mask & b) { s_repeat_next_ms[i] = millis() + ms; return true; }
  if ((s_prev_mask & b) && (int32_t)(millis() - s_repeat_next_ms[i]) >= 0) {
    s_repeat_next_ms[i] = millis() + ms; return true;
  }
  return false;
}

void UIManager::pollKeys() {
  M5Cardputer.update();
  uint64_t mask = buildKeyMask();
  s_edge_mask = mask & ~s_prev_mask;
  s_prev_mask = mask;
}
bool UIManager::keyEdge(char c) {
  int b = trackedBit(c);
  return b >= 0 && (s_edge_mask & (1ULL << b)) != 0;
}
bool UIManager::keyEdgeSpecial(SpecialKey k) { return (s_edge_mask & specialBit(k)) != 0; }
bool UIManager::keyEdgeRepeat(char c, uint32_t ms) {
  int b = trackedBit(c); if (b < 0) return false;
  return repeatForBit(1ULL << b, ms);
}
bool UIManager::keyEdgeRepeatSpecial(SpecialKey k, uint32_t ms) {
  return repeatForBit(specialBit(k), ms);
}

// ── Weighted luminance from RGB565 pixel ──
static inline uint32_t luma_of(uint16_t c) {
  int r5 = (c >> 11) & 0x1F;
  int g6 = (c >> 5)  & 0x3F;
  int b5 =  c        & 0x1F;
  return r5 * 299U + g6 * 587U + b5 * 114U;  // 0…49784
}

// ── SD Card state ──
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

static bool s_sd_avail = false;

// ── v0.8.7: sprite 分配结果 (无 PSRAM 机型上 160×120+240×135 sprite = 128KB,
//    与 64KB 帧缓冲同处内部 RAM; 旧版不检查返回值 → 分配失败即空指针解引用) ──
static bool s_canvas_ok = true;

// ── Capture feedback state ──
static uint32_t s_cap_time = 0;
static char     s_cap_name[24] = "";

bool UIManager::frameOn  = false;
int  UIManager::frameIdx = 0;
int  UIManager::viewHelp = -1;     // v0.8.8: 取景帮助页 (-1 = 关闭)

// ── v0.8.8: 人像模式 (v0.8.6 的 applyPortrait — Sobel 描边 + posterize + 对比拉伸) 整块删除。
//    用户真机判定为"失败产物": 效果不成立, 且白吃 3-6ms/帧。相机模式 v 键一并释放。

// ── v0.8.5: 像素相框 — 画在 canvas 上 (取景显示 + 拍照 BMP 都带框) ──
static void drawFrameOverlay() {
  if (!UIManager::frameOn) return;
  const int w = canvas.width(), h = canvas.height();   // 160×120

  switch (UIManager::frameIdx) {
  case 0: {  // DMG Classic — 4px black border + white dot matrix + top strip
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (x < 4 || x >= w - 4 || y < 4 || y >= h - 4)
          canvas.drawPixel(x, y, 0x0000);
    for (int x = 6; x < w - 6; x += 6) { canvas.drawPixel(x, 3, TFT_WHITE); canvas.drawPixel(x, h - 4, TFT_WHITE); }
    for (int y = 6; y < h - 6; y += 6) { canvas.drawPixel(3, y, TFT_WHITE); canvas.drawPixel(w - 4, y, TFT_WHITE); }
    break; }
  case 1: {  // GBC Purple — 5px purple + 1px white inner
    uint16_t purple = 0x780F;   // GBC purple
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (x < 5 || x >= w - 5 || y < 5 || y >= h - 5)
          canvas.drawPixel(x, y, purple);
    for (int x = 5; x < w - 5; x++) { canvas.drawPixel(x, 4, TFT_WHITE); canvas.drawPixel(x, h - 5, TFT_WHITE); }
    for (int y = 5; y < h - 5; y++) { canvas.drawPixel(4, y, TFT_WHITE); canvas.drawPixel(w - 5, y, TFT_WHITE); }
    break; }
  case 2: {  // CRT Retro — 2px black + double white lines + bold corners
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (x < 3 || x >= w - 3 || y < 3 || y >= h - 3)
          canvas.drawPixel(x, y, 0x0000);
    for (int x = 3; x < w - 3; x++) {
      canvas.drawPixel(x, 2, TFT_WHITE); canvas.drawPixel(x, h - 3, TFT_WHITE);
      canvas.drawPixel(x, 5, TFT_WHITE); canvas.drawPixel(x, h - 6, TFT_WHITE);
    }
    for (int y = 3; y < h - 3; y++) {
      canvas.drawPixel(2, y, TFT_WHITE); canvas.drawPixel(w - 3, y, TFT_WHITE);
      canvas.drawPixel(5, y, TFT_WHITE); canvas.drawPixel(w - 6, y, TFT_WHITE);
    }
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++)   // bold corners
      canvas.drawPixel(x, y, 0x0000), canvas.drawPixel(w-1-x, y, 0x0000),
      canvas.drawPixel(x, h-1-y, 0x0000), canvas.drawPixel(w-1-x, h-1-y, 0x0000);
    break; }
  case 3: {  // Polaroid — white border + wide bottom strip
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (x < 5 || x >= w - 5 || y < 5 || y >= h - 14)
          canvas.drawPixel(x, y, TFT_WHITE);
    break; }
  }
}

// ── v0.8.8: 右侧 60px 侧栏 HUD ──
// 画面 180×135 靠左, 侧栏 x=180..239。每帧由 finishFilter 先清这一条 ——
// v0.8.7 "黑边里残留上一帧滤镜名/FPS" 的根因就是没人清这条带。
// 版式 (x=184 起): 滤镜短名 / FRM+mini 预览 / EV / 模式 / FPS / DROP(事件) / CAPT(事件) / h help
static void drawOverlay(const char *label, uint16_t labelColor) {
  const int x = SIDEBAR_X + 4;
  char buf[24];
  mainCanvas.setTextSize(1);
  mainCanvas.setFont(&fonts::Font0);          // 帮助页可能留下 efont 状态 → 侧栏强制回内置字体

  // 1) 滤镜短名 (mode 0 = 原图)
  mainCanvas.setTextColor(label ? labelColor : TFT_DARKGREY, TFT_BLACK);
  mainCanvas.setCursor(x, 2);
  mainCanvas.print(label ? label : "NORMAL");

  // 2) 相框 FRM1-4 + mini 示意 (20×14)
  if (UIManager::frameOn) {
    snprintf(buf, sizeof(buf), "FRM%d", UIManager::frameIdx + 1);
    mainCanvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    mainCanvas.setCursor(x, 16); mainCanvas.print(buf);
    int mx = x, my = 28;
    switch (UIManager::frameIdx) {
      case 0:   // DMG: 黑底白点边
        mainCanvas.fillRect(mx, my, 20, 14, 0x0000);
        mainCanvas.drawRect(mx, my, 20, 14, TFT_WHITE);
        for (int yy = my + 2; yy < my + 12; yy += 6)
          for (int xx = mx + 2; xx < mx + 18; xx += 6) mainCanvas.drawPixel(xx, yy, TFT_WHITE);
        break;
      case 1:   // GBC: 紫框 + 白内边
        mainCanvas.fillRect(mx, my, 20, 14, 0x780F);
        mainCanvas.drawRect(mx + 1, my + 1, 18, 12, TFT_WHITE);
        break;
      case 2:   // CRT: 双白线
        mainCanvas.drawRect(mx, my, 20, 14, TFT_WHITE);
        mainCanvas.drawRect(mx + 2, my + 2, 16, 10, TFT_WHITE);
        break;
      case 3:   // Polaroid: 白框 + 暗区
        mainCanvas.fillRect(mx, my, 20, 14, TFT_WHITE);
        mainCanvas.drawRect(mx + 5, my + 2, 10, 8, TFT_BLACK);
        break;
    }
  }

  // 3) EV
  if (UIManager::exposureEV != 0) {
    snprintf(buf, sizeof(buf), "EV%+d", UIManager::exposureEV);
    mainCanvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    mainCanvas.setCursor(x, 46); mainCanvas.print(buf);
  }

  // 4) 模式 (ESP-NOW / AP) — v0.8.8 修 v0.8.7 遗留: modeLabel 一直被赋值却从没画出来过
  if (UIManager::modeLabel && UIManager::modeLabel[0]) {
    mainCanvas.setTextColor(UIManager::modeLabelColor, TFT_BLACK);
    mainCanvas.setCursor(x, 58); mainCanvas.print(UIManager::modeLabel);
  }

  // 5) FPS
  snprintf(buf, sizeof(buf), "%.1fFPS", currentFps);
  mainCanvas.setTextColor(TFT_YELLOW, TFT_BLACK);
  mainCanvas.setCursor(x, 70); mainCanvas.print(buf);

  // 6) 掉帧事件提示 (最近 5s 内出现过才显示 — 常驻会变噪音)
  if (g_last_drop_ms && millis() - g_last_drop_ms < 5000) {
    snprintf(buf, sizeof(buf), "DROP %u", (unsigned)g_drop_frames);
    mainCanvas.setTextColor(TFT_RED, TFT_BLACK);
    mainCanvas.setCursor(x, 88); mainCanvas.print(buf);
  }

  // 7) 拍照反馈 (1.5s; 文件名截到 9 字符 — 侧栏只有 56px)
  if (millis() - s_cap_time < 1500) {
    char cb[10];
    strncpy(cb, s_cap_name, 9); cb[9] = 0;
    mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    mainCanvas.setCursor(x, 106); mainCanvas.print(cb);
  }

  // 8) 常驻帮助提示 (dim — 取景 help 的可发现性)
  mainCanvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  mainCanvas.setCursor(x, 124); mainCanvas.print("H HELP");
}

// ── v0.8.7: 取景渲染的公共首尾 (原来 6 份渲染函数各自复制一遍; 改一处要同步 6 处) ──
// 首: 按 inset 清留白 + 解码 JPEG 到 canvas (取景 0.5× → 160×120; 拍照缩进用自动适配)
static bool beginFilter(uint8_t *jpeg_buf, size_t jpeg_len, int in) {
  if (in) canvas.fillRect(0, 0, 160, 120, 0x0000);
  return canvas.drawJpg(jpeg_buf, jpeg_len, in, in, 160 - 2 * in, 120 - 2 * in, 0, 0,
                        in ? 0.0f : 0.5f);
}
// 尾: 清侧栏 → 画面靠左缩放上屏 → 侧栏 HUD → 刷屏
// 🔴 v0.8.8: v0.8.7 的黑边残影根因在这里 — 1.125× 时画面只覆盖 x=0..179,
// x=180..239 会保留上一帧内容 (而滤镜名/FPS 恰好画在那条带里) → 必须先清。
static void finishFilter(const char *label, uint16_t color) {
  mainCanvas.fillRect(SIDEBAR_X, 0, mainCanvas.width() - SIDEBAR_X, mainCanvas.height(), TFT_BLACK);
  canvas.pushRotateZoom(&mainCanvas, VIEW_CX, mainCanvas.height() / 2, 0, VIEW_SCALE, VIEW_SCALE);
  drawOverlay(label, color);
  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ── 1×1 GB / Classical — per-pixel luminance + 8×8 Bayer dither + EV ──
// 8×8 Bayer (64 thresholds) simulates finer shade steps than the old 4×4 —
// smoother gradients on the limited GB palette, closer to real LCD dither.
// v0.8.5: inset=true → 内容缩进 (FRAME_INSET 留白给相框), 拍照时用
static bool renderPaletteDithered(uint8_t *jpeg_buf, size_t jpeg_len,
                                  const uint16_t *palette, int levels,
                                  const char *label, uint16_t labelColor,
                                  bool inset = false) {
  const int W = 160, H = 120;
  const int in = inset ? FRAME_INSET : 0;
  if (!beginFilter(jpeg_buf, jpeg_len, in)) return false;
  int ev = UIManager::exposureEV;
  uint16_t f = ev_fac[ev + 3];
  const bool fine = (levels > 4);   // 8×8 Bayer for ≥5 shades, 4×4 for coarse palettes
  const int DM = fine ? 8 : 4;      // dither matrix dim
  const int DN = fine ? 64 : 16;    // sub-levels
  for (int y = in; y < H - in; y++) {
    int by = (y & (DM - 1));
    for (int x = in; x < W - in; x++) {
      uint32_t l = luma_of(canvas.readPixel(x, y)) * f / 256;
      if (l > 49784) l = 49784;  // clamp — EV can push past max luminance
      uint32_t s = l * (levels - 1) * DN / 49784U;   // sub-levels per dither matrix
      int base = s / DN;
      int frac = s % DN;
      int thr = fine ? s_bayer8[by][x & 7] : s_bayer[by][x & 3];
      int idx = base + (base < levels - 1 && frac > thr);
      canvas.drawPixel(x, y, palette[idx]);
    }
  }
  finishFilter(label, labelColor);
  return true;
}

// ── GBC-1: 4×4×4 ≈ 56色, per-channel 8×8 Bayer dither + EV ──
// 8×8 Bayer (64 thresholds) per channel, decorrelated via channel offsets
// (R=0, G=21, B=42) — smoother gradients while keeping 56-color character.
static bool renderGBC(uint8_t *jpeg_buf, size_t jpeg_len, bool inset = false) {
  const int in = inset ? FRAME_INSET : 0;
  if (!beginFilter(jpeg_buf, jpeg_len, in)) return false;
  int ev = UIManager::exposureEV;
  uint16_t f = ev_fac[ev + 3];
  const int LV = 4;
  const int R_STEP = 31 / (LV - 1);
  const int G_STEP = 63 / (LV - 1);
  const int B_STEP = 31 / (LV - 1);
  const int W = 160, H = 120;
  for (int y = in; y < H - in; y++) {
    int by = y & 7;
    for (int x = in; x < W - in; x++) {
      int bx = x & 7;
      uint16_t c = canvas.readPixel(x, y);
      int r5 = ((c >> 11) & 0x1F) * f / 256;
      int g6 = ((c >> 5)  & 0x3F) * f / 256;
      int b5 = ( c        & 0x1F) * f / 256;
      if (r5 > 31) r5 = 31; if (g6 > 63) g6 = 63; if (b5 > 31) b5 = 31;
      // 64 sub-levels per channel with 8×8 Bayer thresholds (offset-decorrelated)
      uint32_t rs = (uint32_t)r5 * 192U / 31U;
      int thr_r = (s_bayer8[by][bx] + 0)  & 63;
      int r = rs / 64 + (rs / 64 < 3 && (rs % 64) > thr_r);
      uint32_t gs = (uint32_t)g6 * 192U / 63U;
      int thr_g = (s_bayer8[by][bx] + 21) & 63;
      int g = gs / 64 + (gs / 64 < 3 && (gs % 64) > thr_g);
      uint32_t bs = (uint32_t)b5 * 192U / 31U;
      int thr_b = (s_bayer8[by][bx] + 42) & 63;
      int b = bs / 64 + (bs / 64 < 3 && (bs % 64) > thr_b);
      canvas.drawPixel(x, y, (r * R_STEP << 11) | (g * G_STEP << 5) | (b * B_STEP));
    }
  }
  finishFilter("GBC-1", TFT_MAGENTA);
  return true;
}

// ── GBC-2: 4×4×4 ≈ 56色, Floyd-Steinberg serpentine error diffusion ──
// Error diffusion produces the most natural continuous shading (no fixed
// pattern), preserving the 56-color palette while simulating finer tones.
// Serpentine scan (L→R on even rows, R→L on odd) breaks worm artifacts.
// Note: blue-noise LUT approach failed (per-channel decorrelated thresholds
// produced color speckle at 4 levels) — error diffusion is deterministic
// per-channel, keeping hue continuous. Buffers: 3ch × 2 rows × int16.
static bool renderGBC2(uint8_t *jpeg_buf, size_t jpeg_len, bool inset = false) {
  const int in = inset ? FRAME_INSET : 0;
  if (!beginFilter(jpeg_buf, jpeg_len, in)) return false;
  int ev = UIManager::exposureEV;
  uint16_t f = ev_fac[ev + 3];
  const int LV = 4;
  const int R_STEP = 31 / (LV - 1);   // 10
  const int G_STEP = 63 / (LV - 1);   // 21
  const int B_STEP = 31 / (LV - 1);   // 10
  const int W = 160, H = 120;
  // Error diffusion buffers: [2 rows][W+2], index x+1 == pixel x (guard cols)
  static int16_t eR[2][W + 2], eG[2][W + 2], eB[2][W + 2];
  memset(eR, 0, sizeof(eR));
  memset(eG, 0, sizeof(eG));
  memset(eB, 0, sizeof(eB));
  for (int y = in; y < H - in; y++) {
    int cur = y & 1, nxt = cur ^ 1;
    memset(eR[nxt], 0, sizeof(eR[nxt]));   // clear next row before accumulation
    memset(eG[nxt], 0, sizeof(eG[nxt]));
    memset(eB[nxt], 0, sizeof(eB[nxt]));
    if ((y & 1) == 0) {
      // serpentine: left → right
      for (int x = in; x < W - in; x++) {
        uint16_t c = canvas.readPixel(x, y);
        int r5 = ((c >> 11) & 0x1F) * f / 256 + eR[cur][x + 1];
        int g6 = ((c >> 5)  & 0x3F) * f / 256 + eG[cur][x + 1];
        int b5 = ( c        & 0x1F) * f / 256 + eB[cur][x + 1];
        // quantize to 4 levels (0..LV-1), clamp
        int rq = (r5 * (LV - 1) + 15) / 31; if (rq > LV - 1) rq = LV - 1;
        int gq = (g6 * (LV - 1) + 31) / 63; if (gq > LV - 1) gq = LV - 1;
        int bq = (b5 * (LV - 1) + 15) / 31; if (bq > LV - 1) bq = LV - 1;
        if (rq < 0) rq = 0; if (gq < 0) gq = 0; if (bq < 0) bq = 0;
        // diffusion errors (FS weights: 7/16 right, 3/16 down-left, 5/16 down, 1/16 down-right)
        int re = r5 - rq * R_STEP;
        eR[cur][x + 2]     += re * 7 / 16;
        eR[nxt][x]         += re * 3 / 16;
        eR[nxt][x + 1]     += re * 5 / 16;
        eR[nxt][x + 2]     += re * 1 / 16;
        int ge = g6 - gq * G_STEP;
        eG[cur][x + 2]     += ge * 7 / 16;
        eG[nxt][x]         += ge * 3 / 16;
        eG[nxt][x + 1]     += ge * 5 / 16;
        eG[nxt][x + 2]     += ge * 1 / 16;
        int be = b5 - bq * B_STEP;
        eB[cur][x + 2]     += be * 7 / 16;
        eB[nxt][x]         += be * 3 / 16;
        eB[nxt][x + 1]     += be * 5 / 16;
        eB[nxt][x + 2]     += be * 1 / 16;
        canvas.drawPixel(x, y, (rq * R_STEP << 11) | (gq * G_STEP << 5) | (bq * B_STEP));
      }
    } else {
      // serpentine: right → left (mirror FS weights)
      for (int x = W - 1 - in; x >= in; x--) {
        uint16_t c = canvas.readPixel(x, y);
        int r5 = ((c >> 11) & 0x1F) * f / 256 + eR[cur][x + 1];
        int g6 = ((c >> 5)  & 0x3F) * f / 256 + eG[cur][x + 1];
        int b5 = ( c        & 0x1F) * f / 256 + eB[cur][x + 1];
        int rq = (r5 * (LV - 1) + 15) / 31; if (rq > LV - 1) rq = LV - 1;
        int gq = (g6 * (LV - 1) + 31) / 63; if (gq > LV - 1) gq = LV - 1;
        int bq = (b5 * (LV - 1) + 15) / 31; if (bq > LV - 1) bq = LV - 1;
        if (rq < 0) rq = 0; if (gq < 0) gq = 0; if (bq < 0) bq = 0;
        // mirrored weights: 7/16 left, 3/16 down-right, 5/16 down, 1/16 down-left
        int re = r5 - rq * R_STEP;
        eR[cur][x]         += re * 7 / 16;
        eR[nxt][x + 2]     += re * 3 / 16;
        eR[nxt][x + 1]     += re * 5 / 16;
        eR[nxt][x]         += re * 1 / 16;
        int ge = g6 - gq * G_STEP;
        eG[cur][x]         += ge * 7 / 16;
        eG[nxt][x + 2]     += ge * 3 / 16;
        eG[nxt][x + 1]     += ge * 5 / 16;
        eG[nxt][x]         += ge * 1 / 16;
        int be = b5 - bq * B_STEP;
        eB[cur][x]         += be * 7 / 16;
        eB[nxt][x + 2]     += be * 3 / 16;
        eB[nxt][x + 1]     += be * 5 / 16;
        eB[nxt][x]         += be * 1 / 16;
        canvas.drawPixel(x, y, (rq * R_STEP << 11) | (gq * G_STEP << 5) | (bq * B_STEP));
      }
    }
  }
  finishFilter("GBC-2", TFT_MAGENTA);
  return true;
}

// ── The Backrooms — 后室滤镜 (BR-1/BR-2) ──
// 低保真录像带风格: 黄色调色板 + 8×8 Bayer + 噪点 + 荧光灯闪烁 + VHS 行抖动
// 轻强度: 噪点 ~3%, 闪烁 ±4%, 行抖动 0-2px/10行 (无暗角 — 不牺牲可视面积)
static uint32_t s_br_rng = 0x9E3779B9U;   // LCG state (deterministic per boot)

static inline uint32_t br_rand() {        // xorshift — fast, good enough for grain
  s_br_rng ^= s_br_rng << 13;
  s_br_rng ^= s_br_rng >> 17;
  s_br_rng ^= s_br_rng << 5;
  return s_br_rng;
}

static bool renderBR(uint8_t *jpeg_buf, size_t jpeg_len,
                     const uint16_t *palette, const char *label, bool inset = false) {
  const int in = inset ? FRAME_INSET : 0;
  if (!beginFilter(jpeg_buf, jpeg_len, in)) return false;
  int ev = UIManager::exposureEV;
  uint16_t f = ev_fac[ev + 3];
  const int LV = BR_LEVELS;
  const int W = 160, H = 120;
  // 荧光灯闪烁: 每帧亮度因子 0.96~1.04 (±4%)
  uint32_t flick = br_rand();
  uint16_t ff = 246 + (flick & 21);        // 246..266 (×256 scale ≈ 0.961..1.039)
  // VHS 磁头干扰带: 整帧 1~3 条, 每条位置/宽度/偏移量随机 (深浅不同)
  int nband = 1 + (int)(br_rand() % 3);    // 1..3 bands
  int b_y0[3], b_h[3], b_jit[3];
  for (int i = 0; i < nband; i++) {
    b_y0[i]  = (int)(br_rand() % H);       // 随机起始行
    b_h[i]   = 1 + (int)(br_rand() % 3);   // 1..3 行宽
    b_jit[i] = (int)(br_rand() % 4);       // 0..3 px 偏移 (深浅)
  }
  for (int y = in; y < H - in; y++) {
    int by = y & 7;
    // 当前行属于哪条干扰带 (最多一条)
    int jit = 0;
    for (int i = 0; i < nband; i++)
      if (y >= b_y0[i] && y < b_y0[i] + b_h[i]) { jit = b_jit[i]; break; }
    for (int x = in; x < W - in; x++) {
      uint32_t l = luma_of(canvas.readPixel(x, y)) * f / 256 * ff / 256;
      if (l > 49784) l = 49784;
      // 噪点 ~1.5%: 分档混合 (大亮/中亮/中暗/大暗) — 有层次更真实
      if ((br_rand() & 0xFF) < 4) {
        int r = (int)(br_rand() % 100);
        int n;
        if      (r < 20) n =  25 + (int)(br_rand() % 15);   // 20%: 大亮点 +25..+39
        else if (r < 50) n =  10 + (int)(br_rand() % 10);   // 30%: 中亮点 +10..+19
        else if (r < 70) n = -(10 + (int)(br_rand() % 10)); // 20%: 中暗点
        else             n = -(25 + (int)(br_rand() % 15)); // 30%: 大暗点
        if (n > 0) { l += (uint32_t)n * 49784U / 100U; } else { l -= (uint32_t)(-n) * 49784U / 100U; }
        if (l > 49784) l = 49784;
      }
      uint32_t s = l * (LV - 1) * 64 / 49784U;
      int base = s / 64;
      int frac = s % 64;
      int idx = base + (base < LV - 1 && frac > s_bayer8[by][x & 7]);
      int dx = x + jit; if (dx >= W) dx = W - 1;
      canvas.drawPixel(dx, y, palette[idx]);
    }
  }
  finishFilter(label, TFT_YELLOW);
  return true;
}

// ── GBA: 15-bit (32k色), 1×1 per-pixel 6→5 bit dither + EV ──
static bool renderGBA(uint8_t *jpeg_buf, size_t jpeg_len, bool inset = false) {
  const int in = inset ? FRAME_INSET : 0;
  if (!beginFilter(jpeg_buf, jpeg_len, in)) return false;
  int ev = UIManager::exposureEV;
  uint16_t f = ev_fac[ev + 3];
  const int W = 160, H = 120;
  for (int y = in; y < H - in; y++) {
    int by = y & 3;
    for (int x = in; x < W - in; x++) {
      int bx = x & 3;
      uint16_t c = canvas.readPixel(x, y);
      int r5 = ((c >> 11) & 0x1F) * f / 256;
      int g6 = ((c >> 5)  & 0x3F) * f / 256;
      int b5 = ( c        & 0x1F) * f / 256;
      if (r5 > 31) r5 = 31; if (g6 > 63) g6 = 63; if (b5 > 31) b5 = 31;
      // G 6→5 bit with dither: 0-63 → 0-496 (31 × 16)
      uint32_t gs = (uint32_t)g6 * 496U / 63U;
      int g5 = gs / 16;
      int g5d = g5 + (g5 < 31 && (gs % 16) > s_bayer_g[by][bx]);
      // Expand 5-bit G back to 6-bit for RGB565 output
      int g6_out = (g5d << 1) | (g5d >> 4);
      canvas.drawPixel(x, y, (r5 << 11) | (g6_out << 5) | b5);
    }
  }
  finishFilter("GBA", TFT_CYAN);
  return true;
}

// ════════════════════════════════════════
// Public API
// ════════════════════════════════════════

void UIManager::init() {
  build_palettes();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(BLACK);
  // v0.8.7: 检查分配结果 — 失败就明确报出来 (旧版失败即空指针, 表现为刷机后黑屏/乱码)
  bool ok1 = canvas.createSprite(160, 120);
  bool ok2 = mainCanvas.createSprite(240, 135);
  if (!ok1 || !ok2) {
    s_canvas_ok = false;
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, 30);
    M5Cardputer.Display.printf("SPRITE ALLOC FAIL\ncanvas=%d main=%d\nfree heap %u KB",
                                (int)ok1, (int)ok2, (unsigned)(ESP.getFreeHeap() / 1024));
    Serial.printf("[init] sprite alloc FAIL canvas=%d main=%d free=%uKB\n",
                  (int)ok1, (int)ok2, (unsigned)(ESP.getFreeHeap() / 1024));
  }
}

void UIManager::displayLine(String line, bool reset) {
  if (!s_canvas_ok) return;   // v0.8.7: sprite 分配失败 → 什么都别画 (会空指针)
  static int y = 0;
  if (reset) { mainCanvas.fillSprite(TFT_BLACK); y = 0; }
  mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  mainCanvas.setTextSize(1);
  mainCanvas.setCursor(10, y);
  mainCanvas.println(line);
  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
  y += 12;
}

void UIManager::clear() {
  if (!s_canvas_ok) return;
  mainCanvas.fillSprite(TFT_BLACK);
  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ── 连接等待画面 ──
// stage: 0=ESP-NOW 监听  1=WiFi 扫描  2=重试等待
// tick: 递增帧计数 (驱动脉冲动画)
void UIManager::drawSearching(int stage, uint32_t tick) {
  if (!s_canvas_ok) return;
  mainCanvas.fillSprite(TFT_BLACK);
  int cx = mainCanvas.width() / 2;

  // 标题: CONNECTING + 动态点 (… → 视 tick 增加)
  mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  mainCanvas.setTextSize(2);
  const char *dots = (tick % 4 == 0) ? "" : (tick % 4 == 1) ? "." :
                     (tick % 4 == 2) ? ".." : "...";
  String title = "CONNECTING" + String(dots);
  int tw = title.length() * 12;   // textSize2 ≈ 12px/char
  mainCanvas.setCursor(cx - tw / 2, 40);
  mainCanvas.println(title);

  // 脉冲方块 (CAMS3 图标): 周期 8 帧, 大小 6→18→6
  int sz = 6 + (tick % 8 < 4 ? (tick % 8) * 3 : (7 - tick % 8) * 3);
  uint16_t col = (tick % 8 < 4) ? TFT_CYAN : TFT_GREEN;
  mainCanvas.fillRect(cx - sz / 2, 82 - sz / 2, sz, sz, col);

  // 阶段文字 (底部)
  mainCanvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  mainCanvas.setTextSize(1);
  const char *stageText = (stage == 0) ? "ESP-NOW listen..." :
                          (stage == 1) ? "WiFi scan..." :
                                          "retry...";
  int sw = strlen(stageText) * 6;
  mainCanvas.setCursor(cx - sw / 2, 118);
  mainCanvas.println(stageText);

  // v0.8.7: 启动信息行 (版本 + PSRAM) — 现场无串口时唯一的机型/内存确认途径
  if (g_boot_info[0]) {
    mainCanvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    mainCanvas.setCursor(4, 126);
    mainCanvas.print(g_boot_info);
  }

  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

bool UIManager::renderFrame(uint8_t *jpeg_buf, size_t jpeg_len, bool inset) {
  const int in = inset ? FRAME_INSET : 0;
  if (!beginFilter(jpeg_buf, jpeg_len, in)) return false;
  int ev = exposureEV;
  if (ev != 0) {
    uint16_t f = ev_fac[ev + 3];
    const int W = 160, H = 120;
    for (int y = in; y < H - in; y++) {
      for (int x = in; x < W - in; x++) {
        uint16_t c = canvas.readPixel(x, y);
        int r5 = ((c >> 11) & 0x1F) * f / 256;
        int g6 = ((c >> 5)  & 0x3F) * f / 256;
        int b5 = ( c        & 0x1F) * f / 256;
        if (r5 > 31) r5 = 31; if (g6 > 63) g6 = 63; if (b5 > 31) b5 = 31;
        canvas.drawPixel(x, y, (r5 << 11) | (g6 << 5) | b5);
      }
    }
  }
  finishFilter(nullptr, 0);
  return true;
}

bool UIManager::renderFrameGB(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderPaletteDithered(jpeg_buf, jpeg_len,
    s_gb_palette, GB_LEVELS, "GB", TFT_WHITE);
}

bool UIManager::renderFrameClassical(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderPaletteDithered(jpeg_buf, jpeg_len,
    s_cls_palette, CLS_LEVELS, "CLASS", TFT_GREEN);
}

bool UIManager::renderFrameGBC(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderGBC(jpeg_buf, jpeg_len);   // GBC-1: 8×8 Bayer
}

bool UIManager::renderFrameGBC2(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderGBC2(jpeg_buf, jpeg_len);  // GBC-2: blue-noise
}

bool UIManager::renderFrameGBA(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderGBA(jpeg_buf, jpeg_len);
}

bool UIManager::renderFrameBR1(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderBR(jpeg_buf, jpeg_len, s_br1_palette, "BR-1");   // 旧壁纸黄
}

bool UIManager::renderFrameBR2(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderBR(jpeg_buf, jpeg_len, s_br2_palette, "BR-2");   // 荧光灯黄白
}

// ── DMG: 真 4 阶 Game Boy (Lite) — classic green, 4×4 Bayer ──
// 真 DMG 屏幕只有 4 档亮度 (2bit), 用 4×4 Bayer (16 阈值) 模拟亚像素过渡
bool UIManager::renderFrameDMG(uint8_t *jpeg_buf, size_t jpeg_len) {
  return renderPaletteDithered(jpeg_buf, jpeg_len,
    s_dmg_palette, DMG_LEVELS, "DMG", TFT_DARKGREEN);
}

// ════════════════════════════════════════
// SD Card — Capture
// ════════════════════════════════════════

bool UIManager::initSD() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  s_sd_avail = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
  if (s_sd_avail) {
    SD.mkdir("/gbcam");
    Serial.printf("SD: /gbcam/ ready\n");
  } else {
    Serial.println("SD: no card");
  }
  return s_sd_avail;
}

// ── Save a 64×40 downsampled BMP thumbnail from canvas (160×120) ──
// Used by gallery to render thumbnails fast (no full-size JPEG decode).
static void saveThumbBMP(uint32_t num) {
  const int TW = 64, TH = 40;
  char tpath[32];
  snprintf(tpath, sizeof(tpath), "/gbcam/THUM%04lu.BMP", num);  // match CAPT%04lu
  File f = SD.open(tpath, FILE_WRITE);
  if (!f) return;

  uint32_t rowSize = TW * 3;
  uint32_t padding = (4 - (rowSize % 4)) % 4;
  uint32_t fileSize = 54 + (rowSize + padding) * TH;

  uint8_t hdr[54] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = fileSize; hdr[3] = fileSize >> 8; hdr[4] = fileSize >> 16; hdr[5] = fileSize >> 24;
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = TW; hdr[19] = TW >> 8;
  hdr[22] = TH; hdr[23] = TH >> 8;
  hdr[26] = 1;
  hdr[28] = 24;
  f.write(hdr, 54);

  // Downsample 160×120 → 64×40 uniform center sampling
  // v0.8.1: 行缓冲批量写 (原逐像素 f.write×3 → 每行 1 次 write, 快门阻塞明显缩短)
  uint8_t row[64 * 3 + 4];
  for (int ty = TH - 1; ty >= 0; ty--) {          // BMP bottom-up
    memset(row, 0, sizeof(row));                  // padding 清零
    int o = 0;
    for (int tx = 0; tx < TW; tx++) {
      int sx = (tx * 160 + 80) / 64;              // 2.5× center
      int sy = (ty * 120 + 60) / 40;              // 3× center
      uint16_t c = canvas.readPixel(sx, sy);
      uint8_t r5 = (c >> 11) & 0x1F;
      uint8_t g6 = (c >> 5)  & 0x3F;
      uint8_t b5 =  c        & 0x1F;
      row[o++] = (b5 << 3) | (b5 >> 2);
      row[o++] = (g6 << 2) | (g6 >> 4);
      row[o++] = (r5 << 3) | (r5 >> 2);
    }
    f.write(row, rowSize + padding);
  }
  f.close();
  Serial.printf("Saved THUM: %s  (%dx%d)\n", tpath, TW, TH);
}

// ── Save canvas (160×120 RGB565) as 24-bit BMP ──
// Reads pixels directly from canvas during write.
// No snapshot — avoids malloc failures and is fast enough
// that rendering corruption is extremely rare.
static void saveCanvasAsBMP(const char *path) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;

  int w = 160, h = 120;
  uint32_t rowSize = w * 3;
  uint32_t padding = (4 - (rowSize % 4)) % 4;
  uint32_t fileSize = 54 + (rowSize + padding) * h;

  uint8_t hdr[54] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = fileSize; hdr[3] = fileSize >> 8; hdr[4] = fileSize >> 16; hdr[5] = fileSize >> 24;
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = w; hdr[19] = w >> 8;
  hdr[22] = h; hdr[23] = h >> 8;
  hdr[26] = 1;
  hdr[28] = 24;
  f.write(hdr, 54);

  // v0.8.1: 行缓冲批量写 (原逐像素 f.write×3 → 每行 1 次 write)
  uint8_t row[160 * 3 + 4];
  for (int y = h - 1; y >= 0; y--) {
    memset(row, 0, sizeof(row));                  // padding 清零
    int o = 0;
    for (int x = 0; x < w; x++) {
      uint16_t c = canvas.readPixel(x, y);
      uint8_t r5 = (c >> 11) & 0x1F;
      uint8_t g6 = (c >> 5)  & 0x3F;
      uint8_t b5 =  c        & 0x1F;
      row[o++] = (b5 << 3) | (b5 >> 2);
      row[o++] = (g6 << 2) | (g6 >> 4);
      row[o++] = (r5 << 3) | (r5 >> 2);
    }
    f.write(row, rowSize + padding);
  }
  f.close();
  Serial.printf("Saved BMP: %s  (%dx%d)\n", path, w, h);
}

// ── v0.8.7: 单一分派点 ──
// 取景 (inset=false) 与 拍照重渲染 (inset=true) 共用同一张 mode→函数表 — 旧版 main.cpp 与
// renderInsetForCapture 各维护一份, 加/改滤镜必须在两处同步 (P2 隐患)。
bool UIManager::renderFiltered(uint8_t *jpeg_buf, size_t jpeg_len, int mode, bool inset) {
  if (!s_canvas_ok) return false;
  switch (mode) {
    case 0:  return renderFrame(jpeg_buf, jpeg_len, inset);
    case 1:  return renderPaletteDithered(jpeg_buf, jpeg_len, s_gb_palette,  GB_LEVELS,  "GB",    TFT_WHITE,     inset);
    case 2:  return renderPaletteDithered(jpeg_buf, jpeg_len, s_cls_palette, CLS_LEVELS, "CLASS", TFT_GREEN,     inset);
    case 3:  return renderGBC(jpeg_buf, jpeg_len, inset);                       // GBC-1: 8×8 Bayer
    case 4:  return renderGBC2(jpeg_buf, jpeg_len, inset);                      // GBC-2: blue-noise
    case 5:  return renderGBA(jpeg_buf, jpeg_len, inset);
    case 6:  return renderBR(jpeg_buf, jpeg_len, s_br1_palette, "BR-1", inset); // 旧壁纸黄
    case 7:  return renderBR(jpeg_buf, jpeg_len, s_br2_palette, "BR-2", inset); // 荧光灯黄白
    case 8:  return renderPaletteDithered(jpeg_buf, jpeg_len, s_dmg_palette, DMG_LEVELS, "DMG", TFT_DARKGREEN, inset);
  }
  return renderFrame(jpeg_buf, jpeg_len, inset);
}

void UIManager::captureFrame(uint8_t *jpeg_buf, size_t jpeg_len, int mode) {
  // ── v0.8.1: 冻结帧接收 — 快门期间 (快门音+SD写入) 新帧不会覆盖捕获缓冲
  // ESP-NOW 回调 / HTTP 解析器都检查 capFreeze, 解冻后帧状态自愈
  capFreeze = true;

  // ── Shutter sound ──
  M5Cardputer.Speaker.tone(1800, 25);
  delay(35);
  M5Cardputer.Speaker.tone(1200, 60);
  delay(20);
  M5Cardputer.Speaker.end();
  // ──

  if (!s_sd_avail) {
    // Show brief "NO SD" feedback
    strcpy(s_cap_name, "NO SD!");
    s_cap_time = millis();
    capFreeze = false;
    return;
  }

  // Next capture number: max(NVS counter, largest existing CAPT on SD) + 1.
  // NVS counter alone desyncs when SD card is swapped or NVS cleared —
  // then new photos get lower numbers than old ones and sort to the back.
  Preferences prefs;
  prefs.begin("gbcam", false);
  uint32_t num = prefs.getUInt("count", 0) + 1;

  uint32_t sd_max = 0;
  File root = SD.open("/gbcam");
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      String n = f.name();
      if (n.startsWith("CAPT")) {
        uint32_t v = (uint32_t)atoi(n.c_str() + 4);
        if (v > sd_max) sd_max = v;
      }
      f = root.openNextFile();
    }
    root.close();
  }
  // v0.8.4: 收藏的照片被移到 /gbcam/FAV/ — 编号也要算, 否则误用已存在的编号
  File fav = SD.open("/gbcam/FAV");
  if (fav && fav.isDirectory()) {
    File f = fav.openNextFile();
    while (f) {
      String n = f.name();
      if (n.startsWith("CAPT")) {
        uint32_t v = (uint32_t)atoi(n.c_str() + 4);
        if (v > sd_max) sd_max = v;
      }
      f = fav.openNextFile();
    }
    fav.close();
  }
  if (sd_max >= num) num = sd_max + 1;
  prefs.putUInt("count", num);
  prefs.end();

  char path[32];
  // v0.8.4: 滤镜模式也会存 JPG 原图 (与效果 BMP 同编号) — 相册里按 A 可对比原图/效果
  if (mode == 0) {
    // Save raw JPEG (320×240, from CAMS3 buffer)
    snprintf(path, sizeof(path), "/gbcam/CAPT%04lu.JPG", num);
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.write(jpeg_buf, jpeg_len);
      f.close();
      Serial.printf("Saved JPG: %s  (%u bytes)\n", path, (unsigned)jpeg_len);
    }
    // v0.8.5/6: 相框或人像开启 → normal 也存 BMP (重渲染缩进或直接取景 canvas; JPG 原图照存)
    if (UIManager::frameOn) {                 // v0.8.8: 人像模式已移除
      if (UIManager::frameOn) {
        if (!UIManager::renderFiltered(jpeg_buf, jpeg_len, 0, true)) {   // v0.8.7: 渲染失败不写半成品
          strcpy(s_cap_name, "RENDER FAIL"); s_cap_time = millis();
          capFreeze = false; return;
        }
        drawFrameOverlay();
      }
      snprintf(path, sizeof(path), "/gbcam/CAPT%04lu.BMP", num);
      saveCanvasAsBMP(path);
    }
  } else {
    // Save JPEG original + processed canvas as 24-bit BMP (160×120)
    snprintf(path, sizeof(path), "/gbcam/CAPT%04lu.JPG", num);
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.write(jpeg_buf, jpeg_len);
      f.close();
      Serial.printf("Saved JPG: %s  (%u bytes)\n", path, (unsigned)jpeg_len);
    }
    // v0.8.5: 相框开启 → 重渲染缩进版 + 画框; 未开框直接用取景 canvas
    if (UIManager::frameOn) {
      if (!UIManager::renderFiltered(jpeg_buf, jpeg_len, mode, true)) {
        strcpy(s_cap_name, "RENDER FAIL"); s_cap_time = millis();
        capFreeze = false; return;
      }
      drawFrameOverlay();
    }
    snprintf(path, sizeof(path), "/gbcam/CAPT%04lu.BMP", num);
    saveCanvasAsBMP(path);
  }

  // Thumbnail for fast gallery preview (64×40 BMP, same capture number)
  saveThumbBMP(num);

  // Set feedback text (will auto-clear after 1.5 s via drawOverlay)
  const char *fname = strrchr(path, '/');
  snprintf(s_cap_name, sizeof(s_cap_name), " %s", fname ? fname + 1 : path);
  s_cap_time = millis();

  capFreeze = false;   // v0.8.1: 快门完成 → 恢复接收
}

// ════════════════════════════════════════
// Gallery — Photo Review
// ════════════════════════════════════════

UIManager::GState UIManager::galleryState = GALLERY_NONE;

// ── Gallery state ──
// v0.8.4: 条目模型 — 同编号 JPG+BMP 合并为一"张照片"; 双格式可 A 切换对比
struct GEntry {
  uint32_t num;      // capture number
  bool     hasJpg;   // original JPEG exists (both normal & filter captures)
  bool     hasBmp;   // effect BMP exists (filter captures only)
  uint32_t jpgSz, bmpSz;
  bool     inFav;    // files live in /gbcam/FAV/
};
static std::vector<GEntry> s_g_photos;
static int  s_g_sel     = 0;
static int  s_g_page    = 0;
static int  s_g_view    = -1;
static int  s_g_confch  = 0;   // 0=delete, 1=cancel
static int  s_g_prev_st = UIManager::GALLERY_BROWSING;
static int  s_g_page_loaded = -1;  // -1 = no page rendered yet
// v0.8.4: 新功能状态
static bool s_g_showfav  = false;       // v: 全部 ↔ 收藏视图
static bool s_g_multi    = false;       // x: 多选批量删除模式
static std::vector<uint8_t> s_g_marks;  // 勾选标记 (跟随 s_g_photos)
static bool s_g_slide    = false;       // P: 幻灯片
static uint32_t s_g_slide_t = 0;
static bool s_g_info     = false;       // I: 元数据覆盖层
static bool s_g_show_eff = true;        // viewer: true=效果BMP false=原图JPG (A 切换)
static int  s_g_help_page = 0;          // h: 帮助页 0=BROWSE 1=VIEWER

// ── v0.8.7: 查看页缓存 ──
// 旧版 galleryUpdate 每 30ms 调一次 _renderViewer, 每次都从 SD 重读整张照片 (BMP 57KB)
// 并重新解码 → 静止看图时也是 ~30 次/秒 SD 读 + 解码, 纯浪费 (还拖慢按键响应)。
// 现在用"画面指纹"判断: 指纹不变直接跳过 (不碰 SD, 也不重复刷屏)。
struct ViewKey { int idx; bool eff; bool info; bool slide; bool fav; int tot; };
static ViewKey s_view_key = { -2, false, false, false, false, 0 };
static void _invalidateViewer() { s_view_key.idx = -2; }

static const int TPP   = 6;   // thumbs per page
static const int TH_W  = 70, TH_H = 54;
static const int GX[3] = {5, 84, 163};
static const int GY[2] = {12, 68};

static uint8_t *s_filebuf = nullptr;
static size_t   s_fbsize  = 0;

static uint8_t* _fb(size_t sz) {
  if (s_filebuf && s_fbsize < sz) { free(s_filebuf); s_filebuf = nullptr; }
  if (!s_filebuf) { s_filebuf = (uint8_t*)malloc(sz); s_fbsize = sz; }
  return s_filebuf;
}
static void _fbfree() { if (s_filebuf) { free(s_filebuf); s_filebuf = nullptr; s_fbsize = 0; } }

// Forward declarations for gallery rendering helpers
static void drawBmpTo(M5Canvas &target, const char *path,
                       int destX, int destY, int destW, int destH,
                       int srcX, int srcY, int srcW, int srcH);

void UIManager::enterGallery() {
  if (!s_sd_avail) return;

  // v0.8.4: 双目录扫描 (/gbcam + /gbcam/FAV), 同编号 JPG+BMP 合并为一"张照片"
  struct Raw { uint32_t num; bool jpg; uint32_t sz; bool fav; };
  std::vector<Raw> raws;
  auto scanDir = [&](const char *dir, bool fav) {
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f) {
      String n = f.name();
      size_t sz = f.size();
      bool isJpg = n.endsWith(".JPG");
      if (n.startsWith("CAPT") && (isJpg || n.endsWith(".BMP")) && sz > 512) {
        uint8_t m[2];
        bool magic = (f.read(m, 2) == 2) &&
                     (isJpg ? (m[0] == 0xFF && m[1] == 0xD8) : (m[0] == 'B' && m[1] == 'M'));
        if (magic)
          raws.push_back({(uint32_t)atoi(n.c_str() + 4), isJpg, (uint32_t)sz, fav});
      }
      f = root.openNextFile();
    }
    root.close();
  };
  scanDir("/gbcam", false);
  scanDir("/gbcam/FAV", true);
  if (raws.empty()) return;

  // Merge same-number entries (JPG+BMP = one photo)
  std::sort(raws.begin(), raws.end(), [](const Raw &a, const Raw &b) { return a.num > b.num; });
  std::vector<GEntry> photos;
  for (auto &r : raws) {
    if (!photos.empty() && photos.back().num == r.num) {
      GEntry &e = photos.back();
      if (r.jpg) { e.hasJpg = true; if (r.sz > e.jpgSz) e.jpgSz = r.sz; }
      else       { e.hasBmp = true; if (r.sz > e.bmpSz) e.bmpSz = r.sz; }
      if (r.fav) e.inFav = true;
    } else {
      photos.push_back({r.num, r.jpg, !r.jpg, r.jpg ? r.sz : 0, r.jpg ? 0u : r.sz, r.fav});
    }
  }
  // FAV-only view keeps favorited photos only
  if (s_g_showfav) {
    std::vector<GEntry> favs;
    for (auto &e : photos) if (e.inFav) favs.push_back(e);
    if (favs.empty()) return;      // FAV empty → stay in ALL view
    photos.swap(favs);
  }
  s_g_photos.swap(photos);
  s_g_marks.assign(s_g_photos.size(), 0);
  s_g_sel = 0; s_g_page = 0; s_g_view = -1; s_g_page_loaded = -1;
  s_g_multi = false; s_g_slide = false; s_g_info = false;
  galleryState = GALLERY_BROWSING;
}

static const char* _dir(const GEntry &e) { return e.inFav ? "/gbcam/FAV" : "/gbcam"; }
static void _capPath(char *out, size_t outSz, const GEntry &e, const char *ext) {
  snprintf(out, outSz, "%s/CAPT%04lu.%s", _dir(e), e.num, ext);
}

// v0.8.4: 删除一"张照片"全部文件 (JPG + BMP + THUM, 含 FAV 里同名件)
static void deletePhotoFiles(const GEntry &e) {
  char p[64];
  if (e.hasJpg) { _capPath(p, sizeof(p), e, "JPG"); if (SD.exists(p)) SD.remove(p); }
  if (e.hasBmp) { _capPath(p, sizeof(p), e, "BMP"); if (SD.exists(p)) SD.remove(p); }
  snprintf(p, sizeof(p), "%s/THUM%04lu.BMP", _dir(e), e.num);
  if (SD.exists(p)) SD.remove(p);
}

// v0.8.4: 收藏/取消收藏 = 三件套跨目录移动 (JPG+BMP+THUM)
static void movePhotoFiles(const GEntry &e, bool toFav) {
  if (toFav && !SD.exists("/gbcam/FAV")) SD.mkdir("/gbcam/FAV");
  const char *src = toFav ? "/gbcam" : "/gbcam/FAV";
  const char *dst = toFav ? "/gbcam/FAV" : "/gbcam";
  char a[64], b[64];
  if (e.hasJpg) {
    snprintf(a, sizeof(a), "%s/CAPT%04lu.JPG", src, e.num);
    snprintf(b, sizeof(b), "%s/CAPT%04lu.JPG", dst, e.num);
    if (SD.exists(a)) SD.rename(a, b);
  }
  if (e.hasBmp) {
    snprintf(a, sizeof(a), "%s/CAPT%04lu.BMP", src, e.num);
    snprintf(b, sizeof(b), "%s/CAPT%04lu.BMP", dst, e.num);
    if (SD.exists(a)) SD.rename(a, b);
  }
  snprintf(a, sizeof(a), "%s/THUM%04lu.BMP", src, e.num);
  snprintf(b, sizeof(b), "%s/THUM%04lu.BMP", dst, e.num);
  if (SD.exists(a)) SD.rename(a, b);
}

// Parse JPEG dimensions from SOF0/SOF2 header (for metadata overlay)
static void jpegSize(const uint8_t *buf, size_t sz, uint16_t &w, uint16_t &h) {
  w = h = 0;
  for (size_t i = 2; i + 9 < sz; ) {
    if (buf[i] != 0xFF) { i++; continue; }
    uint8_t m = buf[i + 1];
    if (m == 0xC0 || m == 0xC2) { h = (buf[i+5] << 8) | buf[i+6]; w = (buf[i+7] << 8) | buf[i+8]; return; }
    if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { i += 2; continue; }
    uint16_t seg = (buf[i+2] << 8) | buf[i+3];
    if (seg < 2) return;
    i += 2 + seg;
  }
}

// v0.8.4: 缩略图自愈 — 从主图文件生成缺省的 THUMxx.BMP (T 键)
static bool thumbExists(const GEntry &e) {
  char t[64]; snprintf(t, sizeof(t), "%s/THUM%04lu.BMP", _dir(e), e.num);
  return SD.exists(t);
}
static bool genThumbFromFile(const GEntry &e) {
  const int TW = 64, TH = 40;
  char src[64]; _capPath(src, sizeof(src), e, e.hasJpg ? "JPG" : "BMP");
  File f = SD.open(src);
  if (!f) return false;
  size_t sz = f.size();
  uint8_t *buf = _fb(sz);
  if (!buf) { f.close(); return false; }
  f.read(buf, sz); f.close();

  if (e.hasJpg) canvas.drawJpg(buf, sz, 0, 0, TW, TH, 0, 0, 0, 0);   // scaled decode
  else          drawBmpTo(canvas, src, 0, 0, TW, TH, 0, 0, 0, 0);

  char tp[64]; snprintf(tp, sizeof(tp), "%s/THUM%04lu.BMP", _dir(e), e.num);
  File tf = SD.open(tp, FILE_WRITE);
  if (!tf) return false;

  uint32_t rowSize = TW * 3;
  uint32_t padding = (4 - (rowSize % 4)) % 4;
  uint32_t fileSize = 54 + (rowSize + padding) * TH;
  uint8_t hdr[54] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = fileSize; hdr[3] = fileSize >> 8; hdr[4] = fileSize >> 16; hdr[5] = fileSize >> 24;
  hdr[10] = 54; hdr[14] = 40;
  hdr[18] = TW; hdr[19] = TW >> 8;
  hdr[22] = TH; hdr[23] = TH >> 8;
  hdr[26] = 1; hdr[28] = 24;
  tf.write(hdr, 54);

  uint8_t row[64 * 3 + 4];
  for (int ty = TH - 1; ty >= 0; ty--) {          // BMP bottom-up
    memset(row, 0, sizeof(row));
    int o = 0;
    for (int tx = 0; tx < TW; tx++) {
      uint16_t c = canvas.readPixel(tx, ty);
      uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
      row[o++] = (b5 << 3) | (b5 >> 2);
      row[o++] = (g6 << 2) | (g6 >> 4);
      row[o++] = (r5 << 3) | (r5 >> 2);
    }
    tf.write(row, rowSize + padding);
  }
  tf.close();
  return true;
}

void UIManager::exitGallery() {
  _invalidateViewer();   // v0.8.7
  galleryState = GALLERY_NONE;
  s_g_photos.clear(); s_g_marks.clear(); _fbfree(); clear();
}
// ── Thumbnail — renders directly to mainCanvas ──
static void _drawThumb(int col, int row, const GEntry &e) {
  int x = GX[col], y = GY[row];
  mainCanvas.fillRect(x, y, TH_W, TH_H - 11, 0x2124);

  // Fast path: 64×40 thumbnail (same capture number) → zero-scale drawBmp
  char thumb[64];
  snprintf(thumb, sizeof(thumb), "%s/THUM%04lu.BMP", _dir(e), e.num);
  if (SD.exists(thumb)) {
    drawBmpTo(mainCanvas, thumb, x + 3, y + 3, TH_W - 6, TH_H - 14, 0, 0, 0, 0);
    return;
  }

  // Fallback: decode full-size photo (old captures without thumbnail)
  if (e.hasJpg) {
    char path[64]; _capPath(path, sizeof(path), e, "JPG");
    File f = SD.open(path); if (!f) return;
    size_t sz = f.size();
    uint8_t* buf = _fb(sz);
    if (!buf) { f.close(); return; }
    f.read(buf, sz); f.close();
    mainCanvas.drawJpg(buf, sz, x + 3, y + 3, TH_W - 6, TH_H - 14, 0, 0, 0, 0);
  } else {
    char path[64]; _capPath(path, sizeof(path), e, "BMP");
    drawBmpTo(mainCanvas, path, x + 2, y + 2, TH_W - 4, TH_H - 14, 0, 0, 0, 0);
  }
}

// ── Browser page rendering ──
// Expensive thumbnail render happens ONCE per page (when pageChanged).
// Subsequent calls only update overlays (counter, borders, hint bar).
// mainCanvas retains its content between calls → no need for separate cache.
static void _renderBrowser() {
  int tot = s_g_photos.size();
  int ps  = s_g_page * TPP;
  bool pageChanged = (s_g_page != s_g_page_loaded);

  if (pageChanged) {
    // ── Full page render (expensive): thumbnails → SD → JPEG decode ──
    mainCanvas.fillSprite(TFT_BLACK);
    mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    mainCanvas.setTextSize(1);
    mainCanvas.setCursor(4, 2);
    mainCanvas.printf(s_g_showfav ? "FAVORITES  %d/%d" : "GALLERY  %d/%d", s_g_sel + 1, tot);
    for (int i = 0; i < TPP; i++) {
      int fi = ps + i; if (fi >= tot) break;
      M5Cardputer.update();  // keep keyboard alive during SD reads
      _drawThumb(i % 3, i / 3, s_g_photos[fi]);
    }
    s_g_page_loaded = s_g_page;
  }

  // ── Overlays (counter, borders, caption strips) — every frame ──
  // Counter text
  mainCanvas.fillRect(52, 2, 80, 8, TFT_BLACK);
  mainCanvas.setTextColor(s_g_multi ? TFT_RED : TFT_WHITE, TFT_BLACK);
  mainCanvas.setCursor(52, 2);
  if (s_g_multi) {
    int n = 0; for (uint8_t m : s_g_marks) if (m) n++;
    mainCanvas.printf("[%d] %d/%d", n, s_g_sel + 1, tot);
  } else {
    mainCanvas.printf("%d/%d", s_g_sel + 1, tot);
  }

  for (int i = 0; i < TPP; i++) {
    int fi = ps + i; if (fi >= tot) break;
    int col = i % 3, row = i / 3;
    int x = GX[col], y = GY[row];
    const GEntry &e = s_g_photos[fi];
    bool sel = (fi == s_g_sel);
    bool marked = s_g_multi && s_g_marks[fi];
    uint16_t bg = sel ? TFT_ORANGE : (marked ? TFT_RED : 0x2124);

    // Erase previous white border
    mainCanvas.fillRect(x - 2, y - 2, TH_W + 4, 2, 0x2124);
    mainCanvas.fillRect(x - 2, y + TH_H, TH_W + 4, 2, 0x2124);
    mainCanvas.fillRect(x - 2, y, 2, TH_H, 0x2124);
    mainCanvas.fillRect(x + TH_W, y, 2, TH_H, 0x2124);

    // Caption strip
    mainCanvas.fillRect(x, y + TH_H - 11, TH_W, 11, bg);

    // Outline + selection indicator
    mainCanvas.drawRect(x - 1, y - 1, TH_W + 2, TH_H + 2, bg);
    if (sel) mainCanvas.drawRect(x - 2, y - 2, TH_W + 4, TH_H + 4, TFT_WHITE);
    if (marked) mainCanvas.fillRect(x + TH_W - 9, y + 2, 7, 7, TFT_RED);  // check dot

    // Caption text: "0007 J32K" / "0007 B57K" / "0007 J+B"
    mainCanvas.setTextColor(TFT_WHITE, bg);
    mainCanvas.setCursor(x + 2, y + TH_H - 9);
    char cap[16];
    snprintf(cap, sizeof(cap), "%04lu %s%lu", (unsigned long)e.num,
             (e.hasJpg && e.hasBmp) ? "J+B " : (e.hasJpg ? "J " : "B "),
             (unsigned long)(e.hasJpg ? e.jpgSz : e.bmpSz) / 1024);
    mainCanvas.print(cap);
    if (e.inFav) mainCanvas.setTextColor(TFT_YELLOW, bg),
      mainCanvas.setCursor(x + TH_W - 10, y + TH_H - 9), mainCanvas.print("*");
  }

  // Hint bar
  mainCanvas.fillRect(0, mainCanvas.height() - 10, mainCanvas.width(), 10, TFT_BLACK);
  mainCanvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  mainCanvas.setCursor(4, mainCanvas.height() - 9);
  if (s_g_multi) {
    mainCanvas.print("ENTER mark  BS del marked  Q exit");
  } else {
    mainCanvas.print("A/D W/S nav  ENTER open  H help  Q quit");
  }

  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ── Draw a 24-bit BMP file on a target canvas, optionally scaled ──
static void drawBmpTo(M5Canvas &target, const char *path,
                       int destX, int destY, int destW, int destH,
                       int srcX, int srcY, int srcW, int srcH) {
  File f = SD.open(path);
  if (!f) return;
  uint8_t hdr[54];
  if (f.read(hdr, 54) != 54) { f.close(); return; }
  int bw = hdr[18] | (hdr[19] << 8);
  int bh = hdr[22] | (hdr[23] << 8);
  int bpp = hdr[28];
  if (bpp != 24) { f.close(); return; }

  uint32_t rowSize = ((bw * 3) + 3) & ~3;  // padded to 4 bytes

  // Clamp source region
  if (srcW <= 0) srcW = bw;
  if (srcH <= 0) srcH = bh;
  if (srcX + srcW > bw) srcW = bw - srcX;
  if (srcY + srcH > bh) srcH = bh - srcY;
  // 🔴 v0.8.7: 行缓冲固定 160×3 — 源宽必须夹到 160, 否则下面 srcCol 会读到 rowBuf 之外
  // (栈越界读: SD 上任何 >160px 宽的 BMP — 比如从电脑拷进来的图 — 就会花屏或崩溃)
  if (srcW > 160) srcW = 160;
  if (srcX + srcW > bw) srcW = bw - srcX;
  if (srcW <= 0 || srcH <= 0) { f.close(); return; }
  if (destW <= 0 || destH <= 0) { f.close(); return; }

  float scaleX = (float)destW / srcW;
  float scaleY = (float)destH / srcH;
  float scale  = min(scaleX, scaleY);
  if (scale <= 0.0f) { f.close(); return; }
  int outW = srcW * scale, outH = srcH * scale;
  int ox = destX + (destW - outW) / 2;
  int oy = destY + (destH - outH) / 2;

  uint8_t rowBuf[160 * 3 + 4];  // max row
  for (int srcRow = 0; srcRow < srcH; srcRow++) {
    // BMP is bottom-up: file row (bh-1-srcY-srcRow) is the bottom row
    int fileRow = bh - 1 - (srcY + srcRow);
    if (fileRow < 0 || fileRow >= bh) continue;

    f.seek(54 + fileRow * rowSize + srcX * 3);
    size_t readSize = (size_t)srcW * 3;   // v0.8.7: srcW 已夹到 160, 与 rowBuf 等宽
    if (f.read(rowBuf, readSize) != readSize) break;

    int destRow = oy + srcRow * scale;
    if (destRow >= target.height()) break;

    for (int c = 0; c < outW; c++) {
      int srcCol = c / scale;
      uint8_t r = rowBuf[srcCol * 3 + 2];
      uint8_t g = rowBuf[srcCol * 3 + 1];
      uint8_t b = rowBuf[srcCol * 3];
      uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      target.drawPixel(ox + c, destRow, color);
    }
  }
  f.close();
}

// ── Viewer (JPG or BMP, A 切换原图/效果) ──
static void _renderViewer(int idx) {
  if (idx < 0 || idx >= (int)s_g_photos.size()) return;
  const GEntry &e = s_g_photos[idx];
  // v0.8.7: 画面指纹 — 影响显示的全部状态都在里面, 未变化直接返回
  ViewKey k = { idx, (bool)(e.hasBmp && s_g_show_eff), s_g_info, s_g_slide, e.inFav,
                (int)s_g_photos.size() };
  if (s_view_key.idx == k.idx && s_view_key.eff == k.eff && s_view_key.info == k.info &&
      s_view_key.slide == k.slide && s_view_key.fav == k.fav && s_view_key.tot == k.tot) return;
  s_view_key = k;
  mainCanvas.fillSprite(TFT_BLACK);

  // v0.8.4: 双格式照片默认显示效果 BMP, A 切到原图 JPG; 单格式直接显示
  bool showEffect = (e.hasBmp && s_g_show_eff);
  if (!showEffect && !e.hasJpg) showEffect = true;   // BMP-only photo
  const char *ext = showEffect ? "BMP" : "JPG";
  char path[64]; _capPath(path, sizeof(path), e, ext);
  File f = SD.open(path); if (!f) return;
  size_t sz = f.size();

  if (ext[0] == 'J') {
    uint8_t* buf = _fb(sz);
    if (!buf) { f.close(); return; }
    f.read(buf, sz); f.close();
    // Decode at 0.5× to get 160×120 on canvas, then full-screen push
    if (canvas.drawJpg(buf, sz, 0, 0, 0, 0, 0, 0, 0.5f)) {
      canvas.pushRotateZoom(&mainCanvas,
        mainCanvas.width()/2, mainCanvas.height()/2, 0, 1.1f, 1.1f);
    }
  } else {
    f.close();
    // BMP: decode onto canvas (160×120), then pushRotateZoom to display
    drawBmpTo(canvas, path, 0, 0, 160, 120, 0, 0, 0, 0);
    canvas.pushRotateZoom(&mainCanvas,
      mainCanvas.width()/2, mainCanvas.height()/2, 0, 1.1f, 1.1f);
  }

  mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  mainCanvas.setCursor(4, 2); mainCanvas.printf("CAPT%04lu", (unsigned long)e.num);
  if (e.hasJpg && e.hasBmp) {
    mainCanvas.setCursor(4, mainCanvas.height() - 10);
    mainCanvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    mainCanvas.print(s_g_show_eff ? "[EFFECT] o:orig" : "[ORIG] o:eff ");
  }
  mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  mainCanvas.setCursor(mainCanvas.width() - 60, 2);
  mainCanvas.printf("%d/%d", idx+1, (int)s_g_photos.size());
  if (e.inFav) mainCanvas.setCursor(mainCanvas.width() - 78, 2),
    mainCanvas.setTextColor(TFT_YELLOW, TFT_BLACK), mainCanvas.print("★");
  if (s_g_slide) mainCanvas.setCursor(mainCanvas.width() - 60, 12),
    mainCanvas.setTextColor(TFT_GREEN, TFT_BLACK), mainCanvas.print("▶");
  mainCanvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  mainCanvas.setCursor(4, 11);
  mainCanvas.print("A/D nav  Q back  H help");

  // v0.8.4: I — metadata overlay
  if (s_g_info) {
    mainCanvas.fillRect(0, mainCanvas.height() - 32, mainCanvas.width(), 22, TFT_BLACK);
    mainCanvas.drawRect(0, mainCanvas.height() - 32, mainCanvas.width(), 22, TFT_DARKGREY);
    mainCanvas.setTextColor(TFT_CYAN, TFT_BLACK);
    mainCanvas.setCursor(4, mainCanvas.height() - 30);
    mainCanvas.printf("%04lu  %s", (unsigned long)e.num,
      (e.hasJpg && e.hasBmp) ? "JPG+BMP" : (e.hasJpg ? "JPG" : "BMP"));
    mainCanvas.setCursor(4, mainCanvas.height() - 20);
    char line2[40];
    if (ext[0] == 'J') {
      // JPEG original: parse SOF for real dimensions (320×240 from CAMS3)
      uint16_t jw = 0, jh = 0;
      File jf = SD.open(path); size_t jsz = jf.size();
      uint8_t* jb = _fb(jsz);
      if (jb) { jf.read(jb, jsz); jpegSize(jb, jsz, jw, jh); }
      jf.close();
      snprintf(line2, sizeof(line2), "J:%lukB %ux%u  B:%lukB 160x120", (unsigned long)e.jpgSz/1024, jw, jh, (unsigned long)e.bmpSz/1024);
    } else {
      snprintf(line2, sizeof(line2), "eff %lukB 160x120", (unsigned long)e.bmpSz/1024);
      if (e.hasJpg) {
        uint16_t jw = 0, jh = 0;
        char jp[64]; _capPath(jp, sizeof(jp), e, "JPG");
        File jf = SD.open(jp); size_t jsz = jf.size();
        uint8_t* jb = _fb(jsz);
        if (jb) { jf.read(jb, jsz); jpegSize(jb, jsz, jw, jh); }
        jf.close();
        snprintf(line2 + strlen(line2), sizeof(line2) - strlen(line2), "  J:%lukB %ux%u", (unsigned long)e.jpgSz/1024, jw, jh);
      }
    }
    mainCanvas.print(line2);
    mainCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ── Confirm dialog ──
static void _renderConfirm() {
  int dw = 180, dh = 50;
  int dx = (mainCanvas.width() - dw) / 2;
  int dy = (mainCanvas.height() - dh) / 2;
  mainCanvas.fillRect(dx, dy, dw, dh, TFT_BLACK);
  mainCanvas.drawRect(dx, dy, dw, dh, TFT_RED);
  mainCanvas.setTextColor(TFT_RED, TFT_BLACK);
  mainCanvas.setCursor(dx + 10, dy + 5);
  if (s_g_multi) {
    int n = 0; for (uint8_t m : s_g_marks) if (m) n++;
    mainCanvas.printf("Delete %d photo%s?", n, n == 1 ? "" : "s");
  } else {
    mainCanvas.print("Delete this photo?");
  }

  uint16_t cd = (s_g_confch == 0) ? TFT_RED : TFT_DARKGREY;
  mainCanvas.fillRect(dx + 10, dy + 25, 60, 18, cd);
  mainCanvas.setTextColor(TFT_WHITE, cd);
  mainCanvas.setCursor(dx + 14, dy + 27); mainCanvas.print("DELETE");

  uint16_t cc = (s_g_confch == 1) ? TFT_ORANGE : TFT_DARKGREY;
  mainCanvas.fillRect(dx + dw - 70, dy + 25, 60, 18, cc);
  mainCanvas.setTextColor(TFT_WHITE, cc);
  mainCanvas.setCursor(dx + dw - 66, dy + 27); mainCanvas.print("CANCEL");

  mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// v0.8.4: 删除后统一重新扫描刷新列表 (替代脆弱的索引修正)
static void _reloadAfterDelete() {
  _invalidateViewer();   // v0.8.7: 列表变了 → 查看页必须重绘
  bool wasFav = s_g_showfav;
  s_g_photos.clear(); s_g_marks.clear(); s_g_multi = false;
  UIManager::enterGallery();
  if (s_g_photos.empty()) {           // nothing left in current view
    if (wasFav) { s_g_showfav = false; UIManager::enterGallery(); }
    if (s_g_photos.empty()) UIManager::exitGallery();
  }
}

// ══════════════════════════════════════════════════════════════
// v0.8.8: 帮助页公共版式 — 中英双语
// 中文走 M5GFX 内置 efontCN_12 (GB2312, 12×12px), ASCII 走内置 6×8
// 版式: 橙色标题条(16px, 标题+页码) → 行区 (y=20 起, 行距 14, 最多 7 行) → 分隔线(117) → dim 底栏(120)
// 关闭只用 h/q (不用"任意键" — isChange 在松键也会触发, 任意键会让帮助页一闪而过)
// ══════════════════════════════════════════════════════════════
// v0.8.8 中英混排垂直对齐 — 数值来自两侧字库的实测墨水盒 (不是估的):
//   Font0 (内置 6×8 GLCD) 大写字母墨水 = y+1 .. y+6       中心 +3.5
//   efontCN_12 汉字 (char_y=-2, h=12, 字体 y_offset=-2) 墨水 = y+0 .. y+11   中心 +5.5
//   → 两边画在同一 y 时汉字天然低 2px, 故汉字上移 2px 使墨水中心重合
#define CJK_DY      (-2)
#define HELP_ROW0   20      // 汉字墨水自 y-2 起 → 首行 18, 在标题条(0..15)+分隔线(16) 下方
#define HELP_ROW_DY 14      // 汉字墨水 12px, 行间净空 2px
#define HELP_LINE_Y 117     // 分隔线 (末行汉字墨水到 113)
#define HELP_FOOT_Y 120     // 底栏文字 (汉字墨水 118..129, 屏高 135)
#define HELP_LINE   0x4A49

// 中英混排绘制 (按字节范围自动切字体); 返回下一个绘制 x
static int drawAuto(int x, int y, const char *s, uint16_t fg) {
  const uint8_t *p = (const uint8_t *)s;
  char seg[80];
  mainCanvas.setTextSize(1);                  // efont 忽略 size; Font0 用 1 (=6×8)
  while (*p) {
    bool cjk = (*p >= 0x80);
    int n = 0, w = 0;
    while (*p && ((*p >= 0x80) == cjk) && n < 76) {
      int bytes = 1;
      if (cjk) bytes = ((*p & 0xF0) == 0xE0) ? 3 : (((*p & 0xE0) == 0xC0) ? 2 : 1);
      for (int i = 0; i < bytes && *p; i++) seg[n++] = (char)*p++;
      w += (cjk && bytes == 3) ? 12 : 6;      // 汉字 12px 宽, 其余按 6px
    }
    seg[n] = 0;
    if (cjk) {
      mainCanvas.setFont(&fonts::efontCN_12);
      mainCanvas.setTextColor(fg, TFT_BLACK);
      mainCanvas.setCursor(x, y + CJK_DY);    // 与 Font0 墨水中心对齐 (推导见文件头注释)
      mainCanvas.print(seg);
      mainCanvas.setFont(&fonts::Font0);
    } else {
      mainCanvas.setFont(&fonts::Font0);
      mainCanvas.setTextColor(fg, TFT_BLACK);
      mainCanvas.setCursor(x, y);
      mainCanvas.print(seg);
    }
    x += w;
  }
  return x;
}

static void helpBegin(const char *zh, const char *en, int page, int total) {
  mainCanvas.fillSprite(TFT_BLACK);
  mainCanvas.fillRect(0, 0, mainCanvas.width(), 16, TFT_ORANGE);
  char t[40];
  snprintf(t, sizeof(t), "HELP %s %s", zh, en);
  drawAuto(4, 3, t, TFT_BLACK);        // 汉字墨水 1..12, 落在 0..15 的橙条内
  char pg[8]; snprintf(pg, sizeof(pg), "%d/%d", page, total);
  mainCanvas.setFont(&fonts::Font0);
  mainCanvas.setTextColor(TFT_BLACK, TFT_ORANGE);
  mainCanvas.setCursor(mainCanvas.width() - 4 - (int)strlen(pg) * 6, 4);
  mainCanvas.print(pg);
  mainCanvas.drawFastHLine(0, 16, mainCanvas.width(), HELP_LINE);
}

// 一行: 中文标签(橙, x=6) | 键位(黄, x=58) | 英文(暗灰, 右对齐)
static void helpRow(int i, const char *zh, const char *keys, const char *en) {
  int y = HELP_ROW0 + i * HELP_ROW_DY;
  if (zh && zh[0])     drawAuto(6,  y, zh, TFT_ORANGE);
  if (keys && keys[0]) drawAuto(58, y, keys, TFT_YELLOW);
  if (en && en[0]) {
    mainCanvas.setFont(&fonts::Font0);
    mainCanvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    mainCanvas.setCursor(mainCanvas.width() - 4 - (int)strlen(en) * 6, y);
    mainCanvas.print(en);
  }
}
// 整行 (对照表用): 从 x=6 起, 自带中英混排
static void helpLine(int i, const char *text) {
  drawAuto(6, HELP_ROW0 + i * HELP_ROW_DY, text, TFT_YELLOW);
}
static void helpFooter(int page, int total) {
  mainCanvas.drawFastHLine(0, HELP_LINE_Y, mainCanvas.width(), HELP_LINE);
  drawAuto(6, HELP_FOOT_Y, page < total - 1 ? "H 关闭 CLOSE    TAB 下一页 NEXT"
                                            : "H 关闭 CLOSE    E 上一页 PREV", TFT_DARKGREY);
}
static void helpEnd() { mainCanvas.pushSprite(&M5Cardputer.Display, 0, 0); }

// ── 相册帮助 (h) — 两页: BROWSE / VIEWER ──
static void _renderHelp() {
  const int page = (s_g_help_page == 0) ? 1 : 2;
  helpBegin("相册", "GALLERY", page, 2);
  if (s_g_help_page == 0) {
    helpRow(0, "移动", "A D 左右    W S 上下", "MOVE");
    helpRow(1, "翻页", "TAB 下页    E 上页",  "PAGE");
    helpRow(2, "打开", "ENTER",              "OPEN");
    helpRow(3, "删除", "BS    X 多选后 BS",    "DELETE");
    helpRow(4, "视图", "V  全部 / 收藏",       "VIEW");
    helpRow(5, "缩略图", "T  补齐本页",         "THUMBS");
  } else {
    helpRow(0, "翻看", "A 上一张   D 下一张", "BROWSE");
    helpRow(1, "对比", "O  原图 / 效果",      "COMPARE");
    helpRow(2, "信息", "I  编号 格式 尺寸",    "INFO");
    helpRow(3, "幻灯", "P  3 秒自动",         "SLIDESHOW");
    helpRow(4, "收藏", "F",                  "FAV");
    helpRow(5, "删除", "BS",                 "DELETE");
    helpRow(6, "返回", "Q  回列表",           "BACK");
  }
  helpFooter(page, 2);
  helpEnd();
}

// ── 取景帮助 (h) — v0.8.8 新增, 三页: 键位 / 对照表 / 状态 ──
void UIManager::renderViewHelp() {
  if (!s_canvas_ok) return;
  int pg = viewHelp < 0 ? 0 : (viewHelp > 2 ? 2 : viewHelp);
  helpBegin("取景", "VIEWFINDER", pg + 1, 3);
  if (pg == 0) {
    helpRow(0, "滤镜", "1 - 9",             "FILTER");
    helpRow(1, "相框", "0 开关   , / 换款",   "FRAME");
    helpRow(2, "曝光", "-  /  +",           "EV");
    helpRow(3, "快门", "ENTER",             "SHUTTER");
    helpRow(4, "相册", "R",                 "GALLERY");
    helpRow(5, "帮助", "H",                 "HELP");
  } else if (pg == 1) {
    helpLine(0, "滤镜 FILTER");
    helpLine(1, "1 Normal   2 GB      3 Classical");
    helpLine(2, "4 GBC-1    5 GBC-2   6 GBA");
    helpLine(3, "7 BR-1     8 BR-2    9 DMG");
    helpLine(4, "相框 FRAME   (0 开关,  , / 换款)");
    helpLine(5, "FRM1 DMG经典    FRM2 GBC紫框");
    helpLine(6, "FRM3 CRT双线    FRM4 拍立得");
  } else {
    char b[28];
    snprintf(b, sizeof(b), "%.1f", currentFps);                          helpRow(0, "帧率", b, "FPS");
    snprintf(b, sizeof(b), "T%u O%u", (unsigned)g_drop_timeout, (unsigned)g_oversize_frames);
                                                                         helpRow(1, "丢帧", b, "DROP");
    snprintf(b, sizeof(b), "%u", (unsigned)g_soft_frames);               helpRow(2, "软接", b, "SOFT");
    snprintf(b, sizeof(b), "%u", (unsigned)g_jpeg_fail);                 helpRow(3, "解码", b, "JPEGFAIL");
    snprintf(b, sizeof(b), "%uKB", (unsigned)(ESP.getFreeHeap() / 1024)); helpRow(4, "内存", b, "HEAP");
    snprintf(b, sizeof(b), "%uKB", (unsigned)(ESP.getPsramSize() / 1024)); helpRow(5, "PSRAM", b, "PSRAM");
    char vb[16]; int k = 0;
    while (g_boot_info[k] && g_boot_info[k] != ' ' && k < 15) { vb[k] = g_boot_info[k]; k++; }
    vb[k] = 0;
    helpRow(6, "版本", vb[0] ? vb : "dev", "VERSION");
  }
  helpFooter(pg + 1, 3);
  helpEnd();
}

void UIManager::galleryUpdate() {
  if (!s_canvas_ok) return;   // v0.8.7
  switch (galleryState) {
    case GALLERY_BROWSING: _renderBrowser();   break;
    case GALLERY_VIEWING:
      if (s_g_slide && !s_g_photos.empty() &&
          millis() - s_g_slide_t > 3000) {     // v0.8.4: 幻灯片 3s 翻页
        s_g_slide_t = millis();
        s_g_view = (s_g_view + 1) % (int)s_g_photos.size();
      }
      _renderViewer(s_g_view);   break;
    case GALLERY_CONFIRM:  _renderConfirm();   break;
    case GALLERY_HELP:     _renderHelp();      break;
    default: break;
  }
}

void UIManager::galleryHandleKeys() {
  if (galleryState == GALLERY_NONE) return;
  // v0.8.7: 键盘已在 loop() 顶部的 UIManager::pollKeys() 轮询过一次 (不再依赖 isChange)
  // 🔴 旧版: isChange() 门 + isKeyPressed() — 按住键每轮都触发 (松键也触发一次), 而这里
  // 的动作键包含"移文件/批量删/重扫/写卡", 一次误触发就是真实副作用。
  // 现在: 动作键只在上升沿; 导航键上升沿 + 按住 180ms 连发。

  int tot = s_g_photos.size();
  switch (galleryState) {
  case GALLERY_BROWSING:
    if (keyEdge('q')) {
      if (s_g_multi) { s_g_multi = false; s_g_page_loaded = -1; return; }  // exit multi first
      exitGallery(); return;
    }
    if (keyEdge('h')) {   // v0.8.4: help page
      s_g_prev_st = GALLERY_BROWSING; s_g_help_page = 0;
      galleryState = GALLERY_HELP; return;
    }
    if (keyEdgeRepeat('d', 180)) s_g_sel = min(s_g_sel + 1, tot - 1);
    if (keyEdgeRepeat('a', 180)) s_g_sel = max(s_g_sel - 1, 0);
    if (keyEdgeRepeat('s', 180)) s_g_sel = min(s_g_sel + 3, tot - 1);
    if (keyEdgeRepeat('w', 180)) s_g_sel = max(s_g_sel - 3, 0);
    if (keyEdgeSpecial(K_TAB)) {          // next page
      s_g_page = min(s_g_page + 1, (tot - 1) / TPP);
      s_g_sel = s_g_page * TPP;
    }
    if (keyEdge('e')) {                   // prev page (v0.8.4)
      s_g_page = max(s_g_page - 1, 0);
      s_g_sel = s_g_page * TPP;
    }
    s_g_page = s_g_sel / TPP;
    if (keyEdge('x')) {                   // multi-select (v0.8.4)
      if (s_g_multi) s_g_marks.assign(s_g_marks.size(), 0);    // toggling: clear marks
      s_g_multi = !s_g_multi;
      s_g_page_loaded = -1;
      return;
    }
    if (s_g_multi) {
      if (keyEdgeSpecial(K_ENTER)) {
        s_g_marks[s_g_sel] ^= 1;
        s_g_sel = min(s_g_sel + 1, tot - 1);
        return;
      }
      if (keyEdgeSpecial(K_BACKSPACE)) {  // delete marked
        int n = 0; for (uint8_t m : s_g_marks) if (m) n++;
        if (n == 0) return;
        s_g_confch = 0; s_g_prev_st = GALLERY_BROWSING;
        galleryState = GALLERY_CONFIRM;
        return;
      }
      break;  // nav keys above still apply in multi mode
    }
    if (keyEdge('v')) {                   // toggle ALL/FAV view (v0.8.4)
      s_g_showfav = !s_g_showfav;
      s_g_photos.clear(); s_g_marks.clear();
      enterGallery();
      if (s_g_photos.empty()) { s_g_showfav = !s_g_showfav; enterGallery(); }
      return;
    }
    if (keyEdge('t')) {                   // regenerate missing thumbs (v0.8.4)
      int ps = s_g_page * TPP;
      for (int i = 0; i < TPP; i++) {
        int fi = ps + i; if (fi >= (int)s_g_photos.size()) break;
        if (!thumbExists(s_g_photos[fi])) {
          M5Cardputer.update();
          genThumbFromFile(s_g_photos[fi]);
        }
      }
      s_g_page_loaded = -1;   // re-render page with new thumbs
      return;
    }
    if (keyEdgeSpecial(K_ENTER)) {
      s_g_view = s_g_sel; s_g_show_eff = true; s_g_info = false; s_g_slide = false;
      _invalidateViewer();               // v0.8.7: 强制首帧重绘
      galleryState = GALLERY_VIEWING;
    }
    if (keyEdgeSpecial(K_BACKSPACE)) {
      s_g_confch = 0; s_g_prev_st = GALLERY_BROWSING; galleryState = GALLERY_CONFIRM;
    }
    break;
  case GALLERY_VIEWING:
    if (keyEdge('q')) {
      s_g_slide = false; s_g_page_loaded = -1;  // force thumbnail re-render
      galleryState = GALLERY_BROWSING; return;
    }
    if (keyEdge('h')) {   // v0.8.4: help page
      s_g_slide = false; s_g_prev_st = GALLERY_VIEWING; s_g_help_page = 0;
      galleryState = GALLERY_HELP; return;
    }
    if (keyEdgeRepeat('d', 180)) { s_g_view = min(s_g_view + 1, tot - 1); s_g_slide_t = millis(); }
    if (keyEdgeRepeat('a', 180)) { s_g_view = max(s_g_view - 1, 0);       s_g_slide_t = millis(); }
    if (keyEdge('o')) {                   // v0.8.4: 原图↔效果 (小写, 大写键库不识别)
      const GEntry &e = s_g_photos[s_g_view];
      if (e.hasJpg && e.hasBmp) s_g_show_eff = !s_g_show_eff;
    }
    if (keyEdge('i')) s_g_info = !s_g_info;   // v0.8.4: 元数据
    if (keyEdge('p')) {                   // v0.8.4: 幻灯片
      s_g_slide = !s_g_slide; s_g_slide_t = millis();
    }
    if (keyEdge('f')) {                   // v0.8.4: 收藏 toggle
      GEntry &e = s_g_photos[s_g_view];
      movePhotoFiles(e, !e.inFav);
      s_g_photos.clear(); s_g_marks.clear();
      _invalidateViewer();                // v0.8.7
      enterGallery();
      if (s_g_photos.empty()) { if (s_g_showfav) { s_g_showfav = false; enterGallery(); } if (s_g_photos.empty()) exitGallery(); }
      else { s_g_view = 0; galleryState = GALLERY_BROWSING; s_g_page_loaded = -1; }
      return;
    }
    if (keyEdgeSpecial(K_BACKSPACE)) {
      s_g_confch = 0; s_g_prev_st = GALLERY_VIEWING; galleryState = GALLERY_CONFIRM;
    }
    break;
  case GALLERY_CONFIRM:
    if (keyEdge('a')) s_g_confch = 0;
    if (keyEdge('d')) s_g_confch = 1;
    if (keyEdgeSpecial(K_ENTER)) {
      if (s_g_confch == 0) {
        if (s_g_multi) {
          // Delete all marked photos (descending order to keep indices valid)
          for (int i = (int)s_g_marks.size() - 1; i >= 0; i--)
            if (s_g_marks[i]) deletePhotoFiles(s_g_photos[i]);
          _reloadAfterDelete();
        } else {
          int di = (s_g_prev_st == GALLERY_VIEWING) ? s_g_view : s_g_sel;
          if (di >= 0 && di < (int)s_g_photos.size()) {
            deletePhotoFiles(s_g_photos[di]);
            _reloadAfterDelete();
          } else {
            galleryState = GALLERY_BROWSING; s_g_page_loaded = -1;
          }
        }
      } else {
        galleryState = (s_g_prev_st == GALLERY_VIEWING) ? GALLERY_VIEWING : GALLERY_BROWSING;
        if (galleryState == GALLERY_VIEWING) { s_g_view = min(s_g_view, (int)s_g_photos.size() - 1); _invalidateViewer(); }
        s_g_page_loaded = -1;
      }
    }
    if (keyEdge('q')) {
      galleryState = (s_g_prev_st == GALLERY_VIEWING) ? GALLERY_VIEWING : GALLERY_BROWSING;
      if (galleryState == GALLERY_VIEWING) { s_g_view = min(s_g_view, (int)s_g_photos.size() - 1); _invalidateViewer(); }
      s_g_page_loaded = -1;
    }
    break;
  case GALLERY_HELP:                              // v0.8.4h: Tab/e 翻页, h/q/Enter 关闭
    if (keyEdgeSpecial(K_TAB)) s_g_help_page = 1;
    if (keyEdge('e'))          s_g_help_page = 0;
    if (keyEdge('h') || keyEdge('q') || keyEdgeSpecial(K_ENTER)) {
      galleryState = (UIManager::GState)s_g_prev_st;
      s_g_page_loaded = -1;
      _invalidateViewer();   // v0.8.7: 帮助页占用了 mainCanvas → 回查看页必须重绘
    }
    break;
  default: break;
  }
}
