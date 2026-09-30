#ifndef __UIMANAGER_H__
#define __UIMANAGER_H__

#include <M5Cardputer.h>
#include <Arduino.h>

extern M5Canvas canvas;
extern M5Canvas mainCanvas;

class UIManager {
public:
  // ── Core ──
  static void init();
  static void displayLine(String line, bool reset = false);
  static void clear();
  // 连接等待画面: stage 0=ESP-NOW监听 1=WiFi扫描 2=重试等待; tick 为动画帧计数
  static void drawSearching(int stage, uint32_t tick);
  static bool renderFrame(uint8_t *jpeg_buf, size_t jpeg_len, bool inset = false);
  static bool renderFrameGB(uint8_t *jpeg_buf, size_t jpeg_len);
  static bool renderFrameClassical(uint8_t *jpeg_buf, size_t jpeg_len);
  static bool renderFrameGBC(uint8_t *jpeg_buf, size_t jpeg_len);   // GBC-1: 8×8 Bayer
  static bool renderFrameGBC2(uint8_t *jpeg_buf, size_t jpeg_len);  // GBC-2: blue-noise
  static bool renderFrameGBA(uint8_t *jpeg_buf, size_t jpeg_len);
  static bool renderFrameBR1(uint8_t *jpeg_buf, size_t jpeg_len);  // Backrooms 旧壁纸黄
  static bool renderFrameBR2(uint8_t *jpeg_buf, size_t jpeg_len);  // Backrooms 荧光灯黄白
  static bool renderFrameDMG(uint8_t *jpeg_buf, size_t jpeg_len);  // 真 4 阶 DMG 绿
  // v0.8.7: 单一分派点 — 取景(false) 与 拍照重渲染(true) 共用, 避免两份 mode→函数表失同步
  static bool renderFiltered(uint8_t *jpeg_buf, size_t jpeg_len, int mode, bool inset = false);

  // ── v0.8.7: 键盘边沿检测 (loop 每轮调一次 pollKeys, 其余只查边沿) ──
  // 旧实现的坑: isChange() 门 + isKeyPressed() → 按住键反复触发 (松键也触发一次)
  enum SpecialKey : uint8_t { K_ENTER, K_BACKSPACE, K_TAB };
  static void pollKeys();                            // M5Cardputer.update() + 掩码刷新
  static bool keyEdge(char c);                       // 上升沿 (动作键)
  static bool keyEdgeRepeat(char c, uint32_t ms);    // 上升沿 + 按住连发 (导航/EV)
  static bool keyEdgeSpecial(SpecialKey k);
  static bool keyEdgeRepeatSpecial(SpecialKey k, uint32_t ms);

  // 0=normal 1=GB 2=Classical 3=GBC-1 4=GBC-2 5=GBA 6=BR-1 7=BR-2 8=DMG
  static int filterMode;
  // EV compensation -3..+3
  static int exposureEV;

  // ── Mode label (set by main.cpp: "ESP-NOW" / "AP") ──
  static const char* modeLabel;
  static uint16_t    modeLabelColor;

  // ── Pixel frame overlay (v0.8.5): 0 toggle, , / cycle ──
    static bool frameOn;   // switch
    static int  frameIdx;  // 0=DMG Classic 1=GBC Purple 2=CRT 3=Polaroid

    // ── v0.8.8: 取景帮助页 (h) — -1=关闭, 0=键位 1=对照表 2=状态; 打开时暂停取景渲染 ──
    static int  viewHelp;
    static void renderViewHelp();

  // ── SD Card Capture ──
  static bool initSD();
  static void captureFrame(uint8_t *jpeg_buf, size_t jpeg_len, int mode);

  // ── Gallery ──
  enum GState : uint8_t {
    GALLERY_NONE = 0,
    GALLERY_BROWSING,   // 3×2 thumbnail grid
    GALLERY_VIEWING,    // single photo full view
    GALLERY_CONFIRM,    // delete confirmation dialog
    GALLERY_HELP        // v0.8.4: full keymap help page (h)
  };
  static GState galleryState;

  static void enterGallery();       // start gallery mode
  static void galleryUpdate();      // called each main loop when active
  static void galleryHandleKeys();  // keyboard input when gallery active
  static void exitGallery();        // back to camera
};

#endif
