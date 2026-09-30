# GBCAMS — 代码 Review 报告 (v0.8.6 时期)

> 当时的项目名是 "Cardputer GB Cam"；2026-09-30 起统一为 **GBCAMS**。

**审查对象**: 本仓库 (Cardputer 接收端 v0.8.6 + CAMS3 发送端 v0.0.8/v0.0.1)
**审查时间**: 2026-09-29
**方法**: 逐行通读 (3,830 行自有代码) + 交叉对照项目历史教训 (经验沉淀 / skill) + 静态检查 (pio check / cppcheck，见文末)
**结论**: 架构方向是对的（双缓冲、Content-Length 帧边界、探测状态机、拍照冻结）。风险集中在 ①跨核共享状态 ②HTTP 模式单缓冲 ③按键/NVS 落盘 ④SD/内存边界 ⑤渲染管线复制粘贴。**4 个 P0 里有 3 个是"平时不显形、偶发花屏/卡帧/崩"的类型，建议合并成一版 v0.8.7 一次修完再刷机。**

---

## 0. 亮点（保留，别在优化时破坏）

| # | 设计 | 位置 |
|---|------|------|
| 1 | 双缓冲 + 索引交换解决 ESP-NOW 回调↔渲染跨核竞态，只在上一帧被消费后才标 ready（防丢帧） | `main.cpp:83-86, 161-167` |
| 2 | HTTP 解析只信 `Content-Length`，不用 SOI/EOI 搜索 → 不会假 EOI 截断 | `MjpegHttpClient.cpp:84-116` |
| 3 | 探测状态机纯软切换、永不 reboot；`s_auto_detect` 类自激循环已按历史教训禁用 | `main.cpp:539-615` |
| 4 | 拍照冻结 `capFreeze` 同时约束 ESP-NOW 回调与 HTTP 解析器，捕获帧与渲染帧一致 | `Global.h:12`, `main.cpp:128, 834` |
| 5 | SD/WiFi 初始化顺序（SD 放 WiFi 射频校准之后）+ 启动失败落 RETRY 而非死循环 | `main.cpp:178-184, 503-507` |
| 6 | NVS 计数与 SD 实际最大编号取大值，避免换卡/清 NVS 后编号回退 | `UIManager.cpp:852-889` |

---

## 1. P0 — 必须修（正确性 / 崩溃 / 闪存磨损）

### P0-1 重组状态被两条核同时写（跨核竞态 + 非 volatile）

**证据**
- 回调（Core 0 / WiFi 任务）写：`s_frame_active`、`s_recv_len`、`s_recv_pkts`、`s_total_pkts`、`s_frame_start_ms`、`s_last_frame_ms` — `main.cpp:127-172`
- 主循环（Core 1）在 `MODE_ESPNOW` 的帧超时分支同样**写**这几个变量：`main.cpp:571-577`；并在 `579-580` 读 `s_last_frame_ms`
- 这些变量**没有 `volatile`、没有临界区**（只有 `s_ready_idx/s_ready_len/s_fill_idx` 是 volatile）

**后果**
1. `s_last_frame_ms` 可被编译器缓存到寄存器 → 10 秒无帧判定可能永不触发（或反逻辑触发）；
2. 更严重：主循环清零 `s_recv_pkts/s_total_pkts` 的时刻若正好在回调"最后一包到达→判定收齐"之间，该帧**永远收不齐**（表现：偶发一帧不动 / 掉帧），反之则可能带着半帧数据被标记 ready。

**修法（推荐 A：单侧所有权）**
```cpp
// main.cpp — 主循环只发"请求"，重组状态只由回调改
static volatile bool s_frame_abort_req = false;   // 超时时置 true
static volatile uint32_t s_frame_watchdog_ms = 0; // 回调维护

void on_espnow_recv(...) {
  if (s_frame_abort_req) {              // 回调侧处理中止
    s_frame_abort_req = false; s_frame_active = false;
    s_recv_len = 0; s_recv_pkts = 0; s_total_pkts = 0;
  }
  ...
}
// 主循环 MODE_ESPNOW 分支里：删掉 571-577 的直写，改为
if (s_frame_active && now - s_frame_start_ms > FRAME_TIMEOUT_MS) s_frame_abort_req = true;
```
（方案 B：把这一组状态收进一个 struct，全部 `volatile` + `portENTER_CRITICAL(&mux)` 保护 `s_recv_pkts/s_recv_len` 的读改写。）

**验收**：ESP-NOW 模式下播放 10 分钟，中途反复断电 CAMS3 制造丢包，屏幕不应出现"最后一帧卡住不更新"；再连续切 5 次模式不应丢帧率。

---

### P0-2 HTTP 模式单缓冲：一次 `update()` 可解析多帧 → 撕裂帧

**证据**
- `MjpegHttpClient::_mpFeed()` 在帧尾发 `onFrameReady(_dataLen)` 后**立刻**把状态机推回 `ST_MULTIPART_HEADERS` 继续解析（`MjpegHttpClient.cpp:106-110`）
- `update()` 是 `while (_client.available())` 全排空循环（`118-166`）
- 回调把 `s_ready_idx = 0`（`main.cpp:303-308`），而**渲染发生在 loop 后半段**（`main.cpp:618`）

**后果**：只要一次 `update()` 里 socket 缓冲中有 ≥2 帧（相机 25fps、渲染 ~15fps，抖动后极易积压），第二帧就会覆盖 `s_frame_buf[0]`，渲染器读到"前半旧帧 + 后半新帧"的合成图 → `drawJpg` 失败或花屏后被静默跳过。**这就是"偶发花屏"最可能的根因**（ESP-NOW 侧因双缓冲没有这个洞）。

**修法**：emit 后立即退出本轮，让渲染先消费，剩余字节留在 `WiFiClient` 缓冲里下次继续（语义安全，不丢字节）：
```cpp
// MjpegHttpClient.cpp _mpFeed(), ST_FRAME_DATA 分支
if (onFrameReady && _dataLen > 100) {
  size_t n = _dataLen;
  _dataLen = 0;
  _state = ST_MULTIPART_HEADERS;
  onFrameReady(n);
  _pending_frame = true;   // 新增成员
  return;
}
// update() 的 while 循环条件改为： while (_client.available() && !_pending_frame)
// 循环末尾清 _pending_frame = false;
```
（更彻底：HTTP 模式也改用 `s_frame_buf[2]` 双缓冲，与 ESP-NOW 路径统一。）

**验收**：用 CAMS3 的 AP-HTTP 固件（`cams3/wifi`）跑 5 分钟，同时用秒表在 Cardputer 上做慢操作（进相册/出相册）制造积压，不应出现"半张旧帧"。

---

### P0-3 按键 → NVS 每次写入 3 个 key（闪存磨损 + 无去抖）

**证据**
- `saveFramePrefs()` 一次写 3 个 key（`main.cpp:423-429`），调用点在 `465 / 469 / 473 / 478`（`0`、`,`、`/`、`v`）
- 键判定是 `isChange()` + `isKeyPressed()`（`main.cpp:440-442`；相册里 `UIManager.cpp:1497-1499`）——你们自己在 v0.8.4i 已记录 **`isChange()` 松键也会触发**

**后果**
1. 按住 `v`/`0` 会以 loop 频率（≈30Hz，相册态 33Hz）反复翻转 + **每次翻转写 3 个 key** → 每秒几十次 NVS 写入，NVS 分区有磨损上限（≈10 万次/扇区）；
2. 三键非原子：掉电可能留下 `frameOn=1` 但 `frameIdx` 是旧值。

**修法**
```cpp
// (a) 边沿检测（沿用 skill 里 getKeyEdge 的位掩码写法），动作键只在上升沿触发
// (b) 脏标志 + 延迟落盘，一次 blob 原子写
static bool s_prefs_dirty = false;
static uint32_t s_prefs_dirty_ms = 0;
struct PrefBlob { uint8_t ver; bool frameOn; uint8_t frameIdx; bool portraitOn; } __attribute__((packed));
static void flushPrefsIfDirty() {           // loop() 里调用：脏且 30s 未再变 → 写一次
  if (!s_prefs_dirty || millis() - s_prefs_dirty_ms < 30000) return;
  Preferences p; p.begin("gbcam", false);
  PrefBlob b{1, UIManager::frameOn, (uint8_t)UIManager::frameIdx, UIManager::portraitOn};
  p.putBytes("prefs", &b, sizeof(b)); p.end();
  s_prefs_dirty = false;
}
// 按键处只改内存 + s_prefs_dirty = true; s_prefs_dirty_ms = millis();
// 另在进入相册、退出相册、捕获完成时也 flush 一次（保底）
```
（读侧保持向后兼容：`getBytes("prefs")` 失败则回落到旧的 3 个 key。）

**验收**：按住 `v` 10 秒 → 屏幕指示应只翻转 1 次（边沿检测），`prefs` blob 只在松手后写一次；可用一个临时 `Serial.printf` 统计写入次数确认后删除。

---

### P0-4 `drawBmpTo()` 栈越界读（SD 上任何 >160px 宽的 BMP）

**证据**
- `uint8_t rowBuf[160 * 3 + 4];` — `UIManager.cpp:1292`
- `size_t readSize = min((size_t)srcW * 3, sizeof(rowBuf));` — `1299`（**srcW 没有 clamp 到 160**）
- 但 `outW` 仍按原始 `srcW` 计算（`1285-1290`），循环里 `srcCol = c / scale` 可到 `srcW-1`，随后读 `rowBuf[srcCol*3+2]`（`1307-1309`）

**后果**：把一张宽 >160px 的 24-bit BMP 拷进 `/gbcam/`（从电脑拷图、别的项目产物、缩略图以外的文件），相册浏览/查看就会读栈外的数据 → 花屏、或 `LoadProhibited` 崩溃重启。`min()` 反而把问题掩盖成"不报错的脏读"。

**修法**
```cpp
if (srcW > 160) srcW = 160;                  // 行缓冲上限；同时 clamp srcX/srcY
if (srcX + srcW > bw) { srcW = bw - srcX; if (srcW <= 0) { f.close(); return; } }
...
size_t readSize = (size_t)srcW * 3;          // 不再需要 min()，两者已一致
```
（或改成按 `bw` 动态分配行缓冲并复用 gallery 的 `_fb()`。）

**验收**：拷一张 320×240 的 BMP 进 `/gbcam`，改前会崩/花，改后能正常缩放显示。

---

## 2. P1 — 应该修（稳定性 / 性能 / 功耗）

| # | 问题 | 证据 | 修法要点 |
|---|------|------|---------|
| P1-1 | **帧超过 32KB 被当成"收齐"**：`offset+payload > MAX_FRAME_SIZE` 时跳过 memcpy，但 `s_recv_pkts++`、`s_recv_len` 照旧推进 → 帧被标 ready 且长度虚高，`drawJpg` 静默失败无任何日志 | `main.cpp:150-158` | 置 `s_frame_bad`，收齐判定排除它，并打一条告警；相机若换 VGA/低 quality 会立刻暴露 |
| P1-2 | **无重复包去重**：`s_recv_pkts` 是计数不是位图，同一 `idx` 重复到达会让"收齐"提前成立并丢块 | `main.cpp:150-161` | `uint32_t seen_mask`（9 包/帧够用）+ 只在首次置位时 `s_recv_pkts++` |
| P1-3 | **相册查看页每帧重解码**：`galleryUpdate()` 被 loop 以 30ms 周期驱动，`_renderViewer()` 每次都重新从 SD 读整个文件（BMP 57KB）并解码 → ~30 次/秒 SD 读 + 解码，纯浪费（浏览页有 `pageChanged` 缓存，查看页没有） | `UIManager.cpp:1479-1493, 1318-1401`；`main.cpp:521-525` | 加"已渲染指纹"（idx + show_eff + info + slide + 状态），未变化就只 `pushSprite` 缓存画面 — **本报告里收益最大的单点优化** |
| P1-4 | **canvas 分配未检查返回值**：两块 sprite（160×120 + 240×135 = 128KB）与 ESP-NOW 双缓冲 64KB 同处内部 RAM，无 PSRAM 设备上分配失败即空指针解引用 | `UIManager.cpp:589-590` | `if (!canvas.createSprite(...))` 失败则显示错误页并降级（例如跳过 sprite，直接画屏） |
| P1-5 | **CAMS3 端 WiFi 无界阻塞**：`while (!WiFi.STA.started()) delay(10);` 没有超时——Cardputer 侧已修，发送端没修；射频校准失败时 CAMS3 永久卡在 setup（LED 亮、无流、无日志） | `cams3/espnow/src/main.cpp:298` | 与 Cardputer 同款 5s 超时 + 失败重试/持续重试循环 |
| P1-6 | **CAMS3 版本串过期**：实际 v0.0.8，`setup()` 仍打印 `v0.0.7` | `cams3/espnow/src/main.cpp:225` | 平台已有 `VERSION` 指纹机制，改成注入的 `APP_VERSION` |
| P1-7 | **相册动作键无去抖**：`f`（移三件套 + 全量重扫描）、`t`（批量解码写卡）、`v`、`x`、`p`、`i`、BS 全部只在 `isChange()` 门内用 `isKeyPressed()` 判；按住即重复执行 | `UIManager.cpp:1503-1645` | 同一套位掩码边沿检测；导航键（a/d/w/s）允许连发，动作键仅上升沿 |
| P1-8 | **"所见非所得"**：取景 `pushRotateZoom(1.5f)` 把 160×120 放到 240×180，而面板只有 135 高 → 上下各裁 ~22px（约 25% 画面被裁）；拍照走 inset 分支时 `drawJpg(...,0)` 是"自动适配"到 152×112 的 letterbox；存下的 BMP 又是完整 160×120 → **取景 / 预览 / 成品三种构图** | 渲染函数尾 `1.5f`（如 `UIManager.cpp:327-328, 666`）；`306-307 / 340-341 / 817-829` | 统一几何：取景改 `1.125f`（=180×135，整幅放下）或显式还裁剪意图（取景框标注裁切区）；inset 路径的缩放因子也显式写死，别用 0（自动适配） |
| P1-9 | **悬空全局声明**：`Global.h:8` 的 `extern int exposureEV;` 全项目无定义，与 `UIManager::exposureEV` 重名但不是一个东西 | `Global.h:8` vs `UIManager.cpp:13` | 删除该行；指数型的 EV 只在 `UIManager::exposureEV` 一处（单一真值源） |
| P1-10 | **PSRAM 配置与实测机型不一致**：`platformio.ini` 是 `psram=enable` + `qio_opi`（有 PSRAM 的机型），而现场设备的固件指纹是 `cardputer-adv`（Cardputer ADV = ESP32-S3FN8 **无 PSRAM**） | `cardputer/platformio.ini:11-12` + 现场 flash 证据 | 明确目标机型：拆成两个 env（adv: `psram=disable` / classic: `enable`），启动画面打印 `ESP.getPsramSize()` 便于现场一眼确认 |

---

## 3. P2 — 结构 / 可维护性（不影响当前行为）

1. **渲染管线复制粘贴**：`renderFrame` / `renderFrameGB|Classical|GBC|GBC2|GBA|BR1|BR2|DMG` / `renderInsetForCapture` 是同一套"drawJpg → 逐像素滤镜 → pushRotateZoom → overlay → pushSprite"的 6+ 份拷贝（`UIManager.cpp:299-707, 817-829`）。建议收成一个 `renderPipeline(buf, len, variant, inset)` + 三张表（滤镜枚举 / 调色板 / 参数），现在每加一个效果要在 3 处同步（渲染函数、inset 分派、`main.cpp` 键位映射）。
2. **`applyPortrait()` 是独立一遍全画布**（`117-171`）：在滤镜之后又一次 160×120 次 `readPixel` + Sobel 邻域读；可与滤镜合成一遍（算 luma 时就地存 3 行）。另外 `W/H` 硬编码 160×120，建议参数化。
3. **逐像素 `readPixel/drawPixel` 是主要瓶颈**：每帧 19,200 px × (读+写) 都走 LGFX 的边界检查。可考虑行缓冲 + `pushImage`，或直接操作 sprite 行指针（**注意字节序 / RGB-BGR 陷阱——你们 skill 里已记录过，必须实机 A/B 验证，不要凭推断报收益**）。
4. **String / std::vector 在相册热路径**：`enterGallery()`（`992-1046`）与 `captureFrame()` 扫描（`860-886`）用 `String name`、`atoi`、`push_back`，且每次收藏/删除都全量重扫描+重分配 → 长期运行的堆碎片风险（与 skill 里 String 碎片教训同源）。建议固定容量数组 + 一次扫描缓存。
5. **`drawBmpTo()` 逐行 seek+read**（`1298`）：顺序读更省；下采样时多个源行映射到同一目标行（`1302-1312`）→ 可先建列映射表、目标行去重。
6. **协议常量跨端复制**：`PKT_DATA_MAX/HEADER_LEN/MAX_FRAME_SIZE` 在两端各写一份，注释靠"记得同步"（`main.cpp:43-50` ↔ `cams3/espnow/src/main.cpp:60-61`）→ 抽一个共享 `protocol.h`，两端 include；启动时双方各打印一次版本+包长，便于现场对齐。
7. **日志策略**：发行固件保留 `Serial.printf`（因 USB CDC 常不可见，实际帮助有限）；建议加 `#if GBCAM_DEBUG` 分层，并把关键事件（丢帧、帧超限、SD 写失败、重连次数）累积成可在屏幕上查看的计数——现场没有串口时这是唯一的可观测面。
8. **缺主机侧算法回放**：滤镜/人像都是纯像素函数，可抽成 `(in, out, W, H)` 纯函数，在 PC 上对同一张 JPEG 跑对比图（与你们已有的 PC 重渲染流程同构）→ 改算法不必每次刷真机。

---

## 4. 建议的修复顺序（一次刷机验证）

| 顺序 | 内容 | 现场验证方式（无串口依赖） |
|------|------|--------------------------|
| 1 | P0-3（边沿检测 + 延迟落盘） | 按住 `v`/`0` 各 10 秒：屏幕只翻一次；相册 `f` 长按不再反复移动文件 |
| 2 | P0-2（emit 后退出 update） | 用 `cams3/wifi` AP 固件跑 5 分钟，进出相册制造积压 → 不再出现半张旧帧 |
| 3 | P0-1（重组状态单侧所有权） | ESP-NOW 跑 10 分钟 + 中途重启 CAMS3 三次 → 不出现"最后一帧卡住" |
| 4 | P0-4（BMP 行缓冲 clamp） | 拷一张 320×240 BMP 进 `/gbcam` → 改前崩/花，改后正常缩放 |
| 5 | P1-3（viewer 缓存）+ P1-8（统一构图） | 查看页不再每帧闪、续航对比；取景与成品构图一致 |
| 6 | 其余 P1/P2 | 合并成 v0.8.7 |

建议把 1–4 合成 **v0.8.7** 一次构建刷机（我这边可直接做：构建 → esptool 写 merged 镜像 → 屏幕法验收）。

---

## 5. 待你确认

1. **机型**：现场 flash 的固件指纹是 `cardputer-adv`（NVS 里还有 Meshtastic 2.7.26 的 LoRa/BLE 数据），与 `platformio.ini` 里"有 PSRAM"的配置矛盾。这台是 Cardputer ADV 吗？→ 决定 P1-10 要不要改 `psram=disable`。
2. **刷完后 USB 串口不枚举**：`/dev/cu.usbmodem*` 在刷写后消失（固件里明明开了 `ARDUINO_USB_CDC_ON_BOOT=1`），但屏幕有画面。若你要一条日志通道，我可以单独查（先怀疑要重新插拔/电源开关状态，其次怀疑 setup 里 `enterEspNowListen()` 最多阻塞 5 秒触发 TWDT 复位循环）。
3. **取景裁切 25%** 是刻意的"填满屏幕"还是历史遗留？若是刻意，建议在取景上加一条裁切提示线，让用户知道成片会多出上下内容。
4. **要不要我直接按 P0 出一条补丁 v0.8.7** 并构建 + 刷机？（现在这台设备上的 v0.8.6 是我今天构建刷入的，未做代码改动。）

---

## 附录 A. 静态检查

`pio check`（cppcheck）本次运行超时未返回结果（Check 工具链首次拉取），**本报告结论全部来自逐行人工通读与代码交叉比对**，未引用静态检查输出。需要的话我可以在后台单独跑一次并补一份 cppcheck 清单。

## 附录 B. 本次刷入的固件指纹

| 项 | 值 |
|----|-----|
| 源码 | `cardputer/` @ `main` = tag `v0.8.6`（人像模式），commit `5767895` |
| 构建 | `CAMERA_VERSION=v0.8.6`，`firmware.factory.bin` 1,379,072 B（merged：bootloader@0 / partitions@0x8000 / boot_app0@0xe000 / app@0x10000） |
| 刷写 | `erase_flash` → `write_flash --flash_mode dio 0x0`，esptool 写后 hash 校验通过；回读校验命中 `gbcam`×6、`v0.8.6`×1 |
| 设备 | ESP32-S3 (QFN56, rev v0.2, 8MB GD flash), MAC `<device-mac>` |
