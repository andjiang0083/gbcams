/*
 * Hardwired Test Harness  v1.1
 * ============================
 * Reusable remote test framework for ESP32+WebServer projects.
 *
 * Drop this ONE header into any project, add 3 lines of glue, and
 * you instantly get remote screenshot + button simulation + debug.
 *
 * v1.1: btnClicked(id) now returns the action ("click"/"long")
 *       via btnActionStr(id) — allows distinguishing tap from hold.
 *
 * ── Integration ──────────────────────────────────────────
 *   #include "harness.h"
 *   HardwiredTestHarness hrns;            // 1. instantiate
 *   void setup() {
 *     hrns.begin(server, framebuffer_fn); // 2. init
 *   }
 *   void loop() {
 *     hrns.tick();
 *     if (hrns.btnClicked(HRNS_BTN_A))
 *       handleButtonA(hrns.btnActionStr(HRNS_BTN_A));
 *   }
 *
 * ── HTTP Endpoints ──────────────────────────────────────
 *   GET  /hrns/bmp   → screen BMP (240×135, ~65KB)
 *   POST /hrns/btn   → simulate button press
 *     Content-Type: application/json
 *     Body: {"btn":0, "action":"click"}   (btn: 0=A, 1=B)
 *     Body: {"btn":1, "action":"long"}
 *   GET  /hrns/debug → JSON state dump
 *   POST /hrns/reset → ESP.restart()
 *   POST /hrns/data  → inject data (calls onData callback)
 *   POST /hrns/frame → set frame index
 *
 * ── PC Side ─────────────────────────────────────────────
 *   python3 control.py <ip> snap        # screenshot
 *   python3 control.py <ip> btn a       # press BtnA
 *   python3 control.py <ip> btn b long  # BtnB long press
 *   python3 control.py <ip> debug       # JSON state
 *   python3 control.py <ip> reset       # restart device
 *   python3 control.py <ip> frame 2     # jump to frame
 *
 * ── RAM / Flash ─────────────────────────────────────────
 *   RAM: ~240 bytes  Flash: ~2.5KB
 */
#pragma once
#include <Arduino.h>
#include <WebServer.h>

#ifndef HRNS_BMP_W
  #define HRNS_BMP_W 240
#endif
#ifndef HRNS_BMP_H
  #define HRNS_BMP_H 135
#endif

#define HRNS_BTN_A 0
#define HRNS_BTN_B 1

class HardwiredTestHarness;
static HardwiredTestHarness* g_hrns = nullptr;

class HardwiredTestHarness {
public:
  HardwiredTestHarness() {}

  void begin(WebServer& server, uint16_t* (*getFb)()) {
    _server = &server;
    _getFb = getFb;
    g_hrns = this;
    server.on("/hrns/bmp",   HTTP_GET,  _wrap_bmp);
    server.on("/hrns/btn",   HTTP_POST, _wrap_btn);
    server.on("/hrns/debug", HTTP_GET,  _wrap_debug);
    server.on("/hrns/reset", HTTP_POST, _wrap_reset);
    server.on("/hrns/data",  HTTP_POST, _wrap_data);
    server.on("/hrns/frame", HTTP_POST, _wrap_frame);
  }

  void tick() {}

  // Returns true once per simulated press, returns the action ("click"/"long")
  bool btnClicked(int id) {
    if (id < 0 || id > 1) return false;
    if (_btn[id].pending) { _btn[id].pending = false; return true; }
    return false;
  }

  // After btnClicked returns true, call this to get the action type
  const char* btnActionStr(int id) {
    if (id < 0 || id > 1) return "click";
    return _btn[id].isLong ? "long" : "click";
  }

  void onData(void (*cb)(const String&)) { _dataCb = cb; }
  void onSetFrame(void (*cb)(int)) { _setFrameCb = cb; }

private:
  struct BtnState { bool pending = false; bool isLong = false; };
  BtnState _btn[2];
  WebServer* _server = nullptr;
  uint16_t* (*_getFb)() = nullptr;
  void (*_dataCb)(const String&) = nullptr;
  void (*_setFrameCb)(int) = nullptr;

  static void _wrap_bmp()   { if (g_hrns) g_hrns->_handleBmp(); }
  static void _wrap_btn()   { if (g_hrns) g_hrns->_handleBtn(); }
  static void _wrap_debug() { if (g_hrns) g_hrns->_handleDebug(); }
  static void _wrap_reset() { if (g_hrns) g_hrns->_handleReset(); }
  static void _wrap_data()  { if (g_hrns) g_hrns->_handleData(); }
  static void _wrap_frame() { if (g_hrns) g_hrns->_handleFrame(); }

  void _handleBmp() {
    uint16_t* fb = _getFb ? _getFb() : nullptr;
    if (!fb) { _server->send(500, "text/plain", "ERR_NO_FB"); return; }
    int rowSize   = (HRNS_BMP_W * 2 + 3) & ~3;
    int pixelSize = rowSize * HRNS_BMP_H;
    int fileSize  = 54 + pixelSize;
    String bmp;
    bmp.reserve(fileSize);
    for (int i = 0; i < fileSize; i++) bmp += (char)0;
    int w = HRNS_BMP_W;
    uint32_t h = HRNS_BMP_H;
    bmp[0]='B'; bmp[1]='M';
    memcpy(&bmp[2], &fileSize, 4);
    bmp[10]=54; bmp[14]=40;
    memcpy(&bmp[18], &w, 4);
    memcpy(&bmp[22], &h, 4);
    bmp[26]=1; bmp[28]=16;
    memcpy(&bmp[34], &pixelSize, 4);
    int o = 54;
    for (int y = HRNS_BMP_H - 1; y >= 0; y--) {
      for (int x = 0; x < HRNS_BMP_W; x++) {
        uint16_t px = fb[y * HRNS_BMP_W + x];
        bmp[o++] = (char)( px        & 0xFF);
        bmp[o++] = (char)((px >> 8)  & 0xFF);
      }
      o += rowSize - HRNS_BMP_W * 2;
    }
    _server->send(200, "image/bmp", bmp);
  }

  void _handleBtn() {
    if (!_server->hasArg("plain")) {
      _server->send(400, "text/plain", "ERR_NO_BODY");
      return;
    }
    String body = _server->arg("plain");
    if (body.length() == 0) {
      _server->send(400, "text/plain", "ERR_EMPTY");
      return;
    }
    int btn = -1;
    char action[16] = "";
    auto findInt = [&](const char* key) -> int {
      String s = String("\"") + key + "\":";
      int pos = body.indexOf(s);
      if (pos < 0) return -1;
      const char* p = body.c_str() + pos + s.length();
      while (*p == ' ' || *p == '\t' || *p == '\"' || *p == '\n') p++;
      return atoi(p);
    };
    auto findStr = [&](const char* key, char* out, int maxLen) {
      String s1 = String("\"") + key + "\":\"";
      String s2 = String("\"") + key + "\": \"";
      int pos = body.indexOf(s1);
      if (pos < 0) pos = body.indexOf(s2);
      if (pos < 0) return;
      const char* p = body.c_str() + pos + (s1.length() > body.length() - pos ? 0 : s1.length());
      if (*p == '\"') p++;
      while (*p == ' ') p++;
      int i = 0;
      while (*p && *p != '\"' && i < maxLen - 1) out[i++] = *p++;
      out[i] = '\0';
    };
    btn = findInt("btn");
    findStr("action", action, sizeof(action));
    if (btn < 0 || btn > 1) {
      _server->send(400, "text/plain", "ERR_BTN_RANGE");
      return;
    }
    _btn[btn].pending = true;
    _btn[btn].isLong = (strcmp(action, "long") == 0);
    _server->send(200, "text/plain", "OK");
  }

  void _handleDebug() {
    String j = "{";
    j += "\"uptime\":"         + String(millis()) + ",";
    j += "\"free_heap\":"      + String(ESP.getFreeHeap()) + ",";
    j += "\"min_free_heap\":"  + String(ESP.getMinFreeHeap()) + ",";
    j += "\"btn_a_pending\":"  + String(_btn[0].pending ? "1" : "0") + ",";
    j += "\"btn_b_pending\":"  + String(_btn[1].pending ? "1" : "0") + ",";
    j += "\"w\":"             + String(HRNS_BMP_W) + ",";
    j += "\"h\":"             + String(HRNS_BMP_H);
    j += "}";
    _server->send(200, "application/json", j);
  }

  void _handleReset() {
    _server->send(200, "text/plain", "OK_RESETTING");
    delay(100);
    ESP.restart();
  }

  void _handleData() {
    if (!_server->hasArg("plain")) {
      _server->send(400, "text/plain", "ERR_NO_BODY");
      return;
    }
    if (_dataCb) _dataCb(_server->arg("plain"));
    _server->send(200, "text/plain", "OK");
  }

  void _handleFrame() {
    if (!_server->hasArg("plain")) {
      _server->send(400, "text/plain", "ERR_NO_BODY");
      return;
    }
    String body = _server->arg("plain");
    int pos = body.indexOf("\"frame\":");
    if (pos < 0) {
      _server->send(400, "text/plain", "ERR_NO_FRAME");
      return;
    }
    const char* p = body.c_str() + pos + 8;
    while (*p == ' ' || *p == '\t') p++;
    int f = atoi(p);
    if (_setFrameCb) _setFrameCb(f);
    _server->send(200, "text/plain", "OK");
  }
};
