#ifndef GLOBAL_H
#define GLOBAL_H

#include <Arduino.h>
#include <M5Cardputer.h>

extern float currentFps;
// v0.8.7: 删除悬空声明 `extern int exposureEV;` — 全项目无定义, 且与 UIManager::exposureEV
// (类内成员, 真正的真值源) 同名但不是一个东西, 属于双真值源陷阱。

// ── v0.8.7: 诊断计数 (定义在 main.cpp; UIManager 只读, 事件式提示) ──
extern volatile uint32_t g_drop_frames;      // 丢帧总数 (超时 + 超限, v0.8.8)
extern volatile uint32_t g_drop_timeout;     // v0.8.8: 其中缺包超时被整帧丢弃
extern volatile uint32_t g_soft_frames;      // v0.8.8: 软接收补帧 (只缺 1 块)
extern volatile uint32_t g_oversize_frames;  // 帧超限被丢弃
extern volatile uint32_t g_jpeg_fail;        // drawJpg 解码失败
extern volatile uint32_t g_last_drop_ms;     // 最近一次掉帧时刻
extern char g_boot_info[48];                 // 启动信息 (版本 + PSRAM)

// 拍照冻结标志 (v0.8.1): 快门期间冻结帧接收, 防止新帧覆盖捕获缓冲
// ESP-NOW 回调 + HTTP 解析器都检查它; captureFrame 设置/清除
extern volatile bool capFreeze;

#endif
