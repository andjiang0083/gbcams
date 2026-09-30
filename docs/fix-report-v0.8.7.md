# GBCAMS v0.8.7 修复实施报告 + 真机验收清单

**日期**: 2026-09-30 (夜间改码/打包)
**包**: `release/cardputer/firmware.factory.bin` (1,380,208 B, merged, flash_mode=dio)
**子仓库**: cardputer `v0.8.7` (commit `4978854`) · cams3/espnow `v0.0.9` (commit `4e6291a`)
**状态**: 已构建完成, **未刷机** — 等你明天真机验证 (本清单全部不依赖串口, 用屏幕判定)

---

## 1. 今晚改了什么 (逐条对应 review 报告)

| # | review 项 | 改动 | 文件 |
|---|-----------|------|------|
| P0-1 | 重组状态跨核竞态 + 非 volatile | 状态改**回调独占**; 主循环超时只置 `s_frame_abort_req`, 由回调清理; 全部加 `volatile` | `cardputer/src/main.cpp` |
| P0-2 | HTTP 单缓冲撕裂 (花屏根因) | `_mpFeed()` 交帧后设 `_pendingFrame` **立即收手**, 剩余字节留 socket 下轮解析; `update()` 的 while 加条件 | `MjpegHttpClient.cpp/.h` |
| P0-3 | 按住键反复触发 + 每次写 3 个 NVS key | 键盘全改**位掩码边沿检测** (`pollKeys/keyEdge/keyEdgeRepeat`); NVS 改**单 blob 原子写 + 停手 3s 落盘** | `main.cpp`, `UIManager.cpp/.h` |
| P0-4 | `drawBmpTo()` 栈越界读 | `srcW` 夹到 160 (行缓冲宽度) + 二次边界修正 + `scale<=0` 守卫 | `UIManager.cpp` |
| P1-1 | 帧超限仍被当"收齐" | 超 `MAX_FRAME_SIZE` → 整帧标 `s_frame_bad` 丢弃并计数, 收齐判定排除它 | `main.cpp` |
| P1-2 | 无重复包去重 | `s_seen_mask` 位图, 只首次到达才 `s_recv_pkts++` | `main.cpp` |
| P1-3 | 查看页每 30ms 重读 SD+解码 | 画面指纹 `ViewKey{idx,eff,info,slide,fav,tot}`, 未变化**不碰 SD 不刷屏**; 各跳转点显式失效 | `UIManager.cpp` |
| P1-4 | `createSprite` 未检查返回值 | 检查 + 失败画红字错误页 (含 free heap) + 全局 `s_canvas_ok` 守卫 | `UIManager.cpp` |
| P1-5 | CAMS3 WiFi 无界阻塞 | 5s 超时 + 重新初始化重试 (与接收端同款) | `cams3/espnow/src/main.cpp` |
| P1-6 | CAMS3 版本串停在 v0.0.7 | 打印 `v0.0.9 (proto 2.0, PKT_DATA_MAX 1450)` | 同上 |
| P1-7 | 相册动作键无去抖 | `f`/`t`/`x`/删除/多选/帮助 全部上升沿; 导航 `a/d/w/s` 上升沿+180ms 连发 | `UIManager.cpp` |
| P1-8 | 取景/预览/成品三种构图 | `#define VIEW_SCALE 1.125f` → 整幅 160×120 进 240×135 (左右各 30px); 想回退改这一行 | `UIManager.cpp` |
| P1-9 | 悬空 `extern int exposureEV;` | 删除 (真值源只有 `UIManager::exposureEV`) | `include/Global.h` |
| P1-10 | PSRAM 口径矛盾 | 等待画面加启动信息行 `v0.8.7 · PSRAM xxxxKB` → 明天一眼确认机型 | `main.cpp`/`UIManager.cpp` |
| P2-1 | 渲染管线 6 份拷贝 | 片头 `beginFilter()` + 片尾 `finishFilter()` 合一; 分派点合一 `UIManager::renderFiltered()` (取景/拍照共用) | `UIManager.cpp` |
| P2-7 | 现场无串口可观测性 | 掉帧事件提示 `DROP n` (仅掉帧后 5s 出现, 非常驻) + 启动信息行 | `UIManager.cpp` |

**没有做的** (刻意留到你有判断后再动, 都在 review 报告 §3):
- 逐像素 `readPixel/drawPixel` → 行缓冲/`pushImage` 的性能改造 (需要实机 A/B 量数据, 且涉及字节序陷阱)
- `applyPortrait()` 与滤镜合并成一遍 (会改像素结果, 需要视觉 A/B)
- 协议常量抽共享头文件 (两个独立 PlatformIO 工程, 先加了两端启动打印 `proto 2.0` 做现场对齐)
- 相册热路径 String/vector 重构 (改动面大, 明天验证稳定后再做)

---

## 2. 明天刷机

```bash
# 设备需进下载模式 (Cardputer: 侧面电源开关 ON/OFF 后按住 G0 复位, 或直接插拔)
ls /dev/cu.usbmodem*                       # 先确认枚举到
cd <repo>
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 --baud 921600 \
  write_flash --flash_mode dio --flash_size 8MB 0x0 release/cardputer/firmware.factory.bin
# 或直接用 M5Burner 导入 release/m5burner.json 选 0.8.7
```
> 注: 上一版刷完后 `/dev/cu.usbmodem*` 消失过 (屏幕仍有画面)。若这次也不枚举: 先做一次断电重启再插 USB;
> 排查方向见 review 报告 §5-② (怀疑 `enterEspNowListen()` 最长阻塞 5s 触发 TWDT 复位循环)。

---

## 3. 真机验收清单 (屏幕判定, 无需串口)

**准备**: SD 卡里放一张 320×240 的图 (可选: 一张 320×240 的 BMP 拷进 `/gbcam/`)。
启动后在等待画面底部应看到 `v0.8.7 · PSRAM xxxxKB`。

| # | 验证项 | 操作 | 期望 (改好了) | 旧版症状 |
|---|--------|------|---------------|----------|
| 1 | 启动信息 | 上电看等待画面底部 | `v0.8.7 · PSRAM xxxKB` | 无此行 |
| 2 | **按住键只翻一次** | 相机模式按住 `v` 或 `0` 各 10 秒 | HUD 人像/FRM 指示**只翻转一次**, 松手才落盘 | 按住时每秒翻转十几次并写 flash |
| 3 | EV 连发 | 按住 `+` 3 秒 | EV1→EV2→EV3 逐级 (约 120ms 一步) | 一步到底/乱跳 |
| 4 | **取景整幅** | 看取景画面 | 图像完整, **左右各 30px 黑边**, 上下不再被裁 | 填满宽度、上下各裁 22px |
| 5 | 取景=成品 | `,`/`/` 开相框后拍一张 → 相册 `o` 切 BMP | 照片构图与取景一致 (成品多出上下内容的情况消失) | 取景裁 25%, 成品是全幅 |
| 6 | **相册静止不再闪** | 进相册 → Enter 看大图, 静置 30 秒 | 画面稳定不重绘; 按 `d` 翻页立刻响应 | 每 30ms 重读 SD, 卡顿/发热 |
| 7 | 相册动作键 | 浏览页按住 `f`(收藏) / `v`(收藏视图) / `t`(缩略图) | 只执行一次 | 按住反复搬文件 |
| 8 | 大 BMP 不崩 | 把 320×240 BMP 拷进 `/gbcam/`, 浏览+查看它 | 正常缩放显示 | 花屏或重启 (LoadProhibited) |
| 9 | **重连不花屏** | ESP-NOW 下跑 10 分钟, 中途给 CAMS3 断电 3 次 | 恢复后画面正常, 不出现半张旧帧; 掉帧时左下角短暂出现 `DROP n` | 偶发花屏/最后一帧卡住 |
| 10 | HTTP 模式积压 | 刷 `cams3/wifi` 后跑 5 分钟, 期间反复进出相册 | 不出现"上半旧帧下半新帧" | 积压时撕裂 |
| 11 | 内存告警 | (若机型无 PSRAM) 看启动行 PSRAM 是否为 0KB | 若为 0KB, 说明 `platformio.ini` 的 `psram=enable` 与实机不符 → 告诉我, 我出 adv 专用 env | 无提示 |
| 12 | 拍照落盘 | 拍 3 张 (normal + 开框 + 滤镜) | 相册三件套齐全 (JPG/BMP/THUM), 文件名连号 | — |

**若出现异常**: 记下「哪个键/什么操作 → 屏幕现象 → 是否可复现」, 有 `DROP n` 字样一并记下。
需要串口日志时再单独处理 (v0.8.7 已把关键事件做成屏幕可见的 `DROP` 提示)。

---

## 4. 几何对比图

`docs/v087-viewfinder-geometry.png` — 同一张 320×240 源图在三种状态下的构图:
取景(v0.8.6 填满裁切) / 取景(v0.8.7 整幅) / 保存的照片(全幅)。
