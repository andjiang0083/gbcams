# Changelog — GBCAMS

> 项目代号 **GBCAMS**（2026-09-30 起统一；旧称 Cardputer Camera / Cardputer GB Cam / Cardputer GameBoy Cam 都是它）

> 顶层仓库管理项目级资产;固件变更详见各子仓库
> (`cardputer/` 接收端, `cams3/espnow/` + `cams3/wifi/` 发送端)。

## [v0.8.8] - 2026-09-30 (候选发行包 — 待真机验证)  🏁 里程碑

> **里程碑 (2026-09-30, 用户拍板)**: 取景 UI 定稿 (画面靠左 180×135 + 右侧 60px 侧栏)、
> help 体系成型 (中英双语 5 页: 取景 3 + 相册 2, 含状态诊断页)、丢帧"卡一下"消除 (软接收)。
> 画面 / 侧栏 / HUD / 帮助 四块布局自此稳定, 后续版本只做增量, 不再整页重排。


取景布局定稿 (侧栏) + 人像模式移除 + help 体系重做 (中英双语) + 丢帧软接收。
所有改动都能在屏幕上验收, 不依赖串口。

### Changed (UI 定稿 — 侧栏取景)
- **取景布局: 画面靠左 180×135 + 右侧 60px 侧栏 HUD**。原"黑边"改成 HUD 功能区:
  滤镜短名 / FRM+mini 预览 / EV / 模式 / FPS / DROP(事件) / 拍照反馈(事件) / `h help`。
  HUD 不再压在画面上; 侧栏每帧先清 → 修掉 v0.8.7 "黑边里残留上一帧滤镜名/FPS" 的残影
  (根因: 1.125× 只覆盖 x=0..179, x=180..239 保留上一帧内容, 而 HUD 恰好画在那条带里)
- 拍照反馈文字从画面中央移到侧栏 (画面 100% 纯净)
- `modeLabel` (ESP-NOW / AP) 现在真的显示在侧栏 — v0.8.7 只赋值、从没画出来

### Renamed (2026-09-30)
- **项目代号统一为 `GBCAMS`**（用户拍板, 为日后维护便利）: 旧称 Cardputer Camera / Cardputer GB Cam /
  Cardputer GameBoy Cam 全部统一。文档叙述、发行说明、320×200 封面、构建脚本输出、M5Burner 条目名
  (`GBCAMS Receiver (M5Cardputer)` / `GBCAMS Sender ESP-NOW (UnitCamS3-5MP)` / `GBCAMS Sender AP-HTTP (UnitCamS3-5MP)`)。
  与既有内部标识对齐: SD 目录 `/gbcam/`、NVS 命名空间 `gbcam`、协议宏 `GBCAM_PROTO_VER`。
  硬件名不变 (M5Cardputer / UnitCamS3-5MP)。
  同日续做:   **三端固件自报串**改 GBCAMS (`Serial.printf("GBCAMS %s (proto %s)")` / `[GBCAMS] UnitCamS3 … Sender`),
  镜像按同名版本重出 (v0.8.8 / v0.0.9 / v0.0.1, 只有字符串变化; 三端前 0x10000 与改名前的镜像逐字节一致 → M5Burner 安全)。

### Removed
- **人像模式 (`v` 键 / `applyPortrait()`) 整块删除** — 真机判定为失败产物; 顺带省 3-6ms/帧
- 相机模式 `v` 键释放 (相册模式 `v` 仍是"全部/收藏"视图)
- NVS blob 的 `portraitOn` 字段 (ver 2; 兼容读 v0.8.7 的 4 字节 blob 与更早的 3 键)

### Added (help 体系 — 中英双语)
- 公共版式: 橙色标题条 (标题+页码) / 中文标签列 (efontCN_12, 橙) + 键位列 (内置 6×8, 黄) +
  英文列 (dim, 右对齐) / dim 底栏
- **取景可按 `h` 进帮助** (v0.8.7 只有相册有), 3 页: 键位 / 滤镜·相框对照表 / 状态
- 相册帮助重做为同一套版式 (2 页: BROWSE / VIEWER); 关闭只用 `h`/`q`, 翻页 `Tab`/`e`
- 状态页: 帧率 / 丢帧 T·O / 软接 / 解码失败 / 内存 / PSRAM / 版本 (现场无串口的诊断入口)
- 中文走 M5GFX 内置 efontCN_12 (GB2312), 无需额外字体库; 固件 +151KB Flash

### Fixed (丢帧"卡一下")
- **软接收**: 帧超时后若只缺 1 块 → 用现有数据出帧 (缺块保留上一帧内容), 不再整帧丢弃。
  15fps 下丢一包 = 画面静止 60ms, 这正是真机反馈的"突然卡一下"; 只缺 2 块以上才丢帧
- 丢帧计数拆为 T (超时) / O (超限) / S (软接), 状态页可看

### 真机反馈修复 (同版重刷, 2026-09-30)
- **中英混排垂直对齐**: 真机反馈"英文靠上、汉字靠下" → 从 M5GFX 字库数据量出墨水盒
  (Font0 大写字母 y+1..y+6 中心 +3.5; efontCN_12 汉字 char_y=-2/h=12 → y+0..y+11 中心 +5.5),
  汉字绘制偏移由 +2 改为 **-2** (原值凭手感, 实机偏 4px); 行区随之下移 (y=20 起), 标题条/底栏重排;
  推导过程见 `docs/design-v0.8.8.md` §3b 与 `~/esp32/ESP32-经验沉淀.md` §134
- **键位全大写** ("全部界面"): 帮助页键位列 / 底栏 / 取景侧栏 `H HELP` / 相册提示栏
  (`A/D W/S nav  ENTER open  H help  Q quit`, `ENTER mark  BS del marked  Q exit`, `A/D nav  Q back  H help`)
- 版本沿用 v0.8.8 (未验证的候选版不另开号); 镜像重刷 + 回读校验

### 待验证 (真机)
见 `docs/design-v0.8.8.md` 验收清单 (侧栏无残影 / HUD 不压画面 / 中文帮助页可读 /
`h` 开关正常 / 帧率仍 16-17 且不再"卡一下")。

## [v0.8.7] - 2026-09-30 (候选发行包 — 待真机验证)

代码 review 后的修复版: 4 个 P0 + 10 个 P1。构建产物 `release/cardputer/firmware.factory.bin`
(1,380,208 B, DIO merged)。全部改动都能在屏幕上验收, 不依赖串口。

### Fixed (P0 — 正确性/崩溃/闪存磨损)
- **跨核竞态丢帧**: ESP-NOW 重组状态 (`s_recv_*`/`s_frame_active`) 改为回调独占, 主循环超时只置
  `s_frame_abort_req` 由回调清理 (旧版两核同时写 + 非 volatile → 偶发"最后一帧卡住"/半帧)
- **HTTP 模式偶发花屏**: `_mpFeed()` 交出一帧后设 `_pendingFrame` 立即收手, 剩下的字节留在
  socket 下一轮再解析 (旧版同一次 `update()` 里解析到第二帧就覆写同一个渲染缓冲)
- **按住键反复触发 + NVS 磨损**: 键盘全改位掩码边沿检测 (动作键仅上升沿, 导航键 +180ms 连发);
  NVS 由"每次按键写 3 个 key"改为单 blob 原子写 + 停手 3s 落盘
- **SD 上 >160px 宽 BMP 栈越界读**: `drawBmpTo()` 把 `srcW` 夹到 160 (行缓冲宽度) 并加 0 缩放守卫

### Fixed (P1)
- 帧超过 `MAX_FRAME_SIZE` → 整帧丢弃 (旧版仍计数 → 长度虚高的半帧被当"收齐"交给 drawJpg 静默失败)
- 包序号位图去重 (同一 idx 重复到达不再提前满足"收齐"并丢块)
- 相册查看页加画面指纹缓存 (旧版静止看图也 ~30 次/秒重读 SD + 重解码)
- `createSprite` 返回值检查 + 失败明确报错页 (旧版失败即空指针)
- CAMS3 发送端 WiFi STA 加 5s 超时重试; 版本串修正 (此前一直打印 v0.0.7)
- 取景几何统一: `VIEW_SCALE 1.125f` → 160×120 整幅进 240×135 (旧版 1.5× 填满宽度但上下裁 25%,
  取景与保存的照片构图不一致); 保存的照片始终是全幅, 不丢像素
- `Global.h` 删除悬空 `extern int exposureEV;` (双真值源)
- 相册动作键 (`f` 收藏/`t` 补缩略图/删除/多选) 全部改为上升沿, 不再"按住即反复搬文件"

### Added
- 渲染分派点合一 `UIManager::renderFiltered()` (取景与拍照重渲染共用一张表, 旧版两处同步)
- 取景片尾掉帧事件提示 `DROP n` (仅掉帧后 5s 内出现, 非常驻 UI)
- 等待画面启动信息行 `v0.8.7 · PSRAM 0KB` (现场无串口时确认机型/内存)
- 两端启动打印协议标识 `proto 2.0`

### 待验证 (真机)
见 `docs/fix-report-v0.8.7.md` 的验收清单 (按住键只翻一次 / 相册静止不再闪 / 重连不花屏 /
整幅取景构图 / >160px BMP 不崩)。

## [v0.8.6] - 2026-09-30 (补录 — 人像模式)

- `v` 开关人像模式 (二次元三件套: Sobel 描边 + posterize + 对比拉伸), 取景与拍照同路径
- HUD 右上人形图标; 相机模式 `v` = 人像, 相册模式 `v` = 收藏视图 (互不干扰)

## [v0.8.5] - 2026-08-25

### Added (cardputer/ 接收端 — 像素相框)
- **4 款像素相框**: DMG Classic (黑框白点阵+顶条) / GBC Purple (紫框白内边) / CRT Retro (双线粗角) / Polaroid (白框+底条)
- **交互**: `0` 开关 (记忆款式) · `,` 左切 · `/` 右切 — 与 1-9 滤镜正交组合 (36 种)
- **图片完整性优先**: 边框只长在照片外围 — 拍照重渲染缩进版 (内容 152×112 居中, FRAME_INSET=4), 取景器画面零占用
- **HUD 预览**: FRM1-4 文字 + mini 边框特征示意 (20×14), 左上角实时显示
- **NVS 记忆**: 开关 + 款式重启保留
- 快门瞬间显示带框照片预览 (重渲染副产品)

### Changed
- 6 个渲染函数加 `inset` 参数 (drawJpg 目标矩形 + 循环边界), 取景路径零变化
- normal 模式开框双存: JPG 原图 + 带框 BMP (相册 `o` 可对比)

## [v0.8.4] - 2026-08-25

### Added (cardputer/ 接收端 — 相册大升级)
- **批量删除**: `x` 多选 → Enter 勾选(红点+计数 `[n]`) → BS 确认删勾选, 三件套联动删 (JPG+BMP+THUM)
- **双向翻页**: `e` 前翻 / Tab 后翻
- **格式徽标**: caption 显示 `0007 J32K` / `B57K` / `J+B`, ★ 收藏角标
- **收藏夹**: `v` 全部↔收藏视图, 查看时 `f` 收藏 (三件套移入 /gbcam/FAV/, 编号扫描兼容防重号)
- **双格式对比**: 滤镜拍照同时存 JPG 原图 + BMP 效果, 查看时 `o` 切换 [EFFECT]/[ORIG]
- **元数据**: `i` 显示 编号/格式/JPG 尺寸 (SOF 解析)/大小
- **幻灯片**: `p` 3s 自动翻页循环 (▶ 角标)
- **缩略图自愈**: `t` 批量补当前页缺省 THUM (从主图文件解码生成)
- **h 帮助页**: 两页 (BROWSE/VIEWER) 大行距键位表, Tab/e 翻页, h/q/Enter 关闭
- **hint 精简**: 底部仅核心键, 高级功能移入帮助页

### Changed
- 条目模型: GEntry{num,hasJpg,hasBmp,sz,inFav}, 同编号 JPG+BMP 合并为一张照片
- 删除/收藏后重扫描刷新列表 (替代脆弱的索引修正)
- 键位全小写 (键盘库 isKeyPressed 大小写不敏感, 大写键永不触发 — v0.8.4f)

### Fixed
- **帮助页一闪而过**: isChange() 松键也触发 → "任意键关闭" 改为明确键 (v0.8.4i)

## [v0.8.3] - 2026-08-25

### Changed (双端联动)
- **ESP-NOW v2.0 大包**: `PKT_DATA_MAX` 240→1450 (上限 1470 留余量)
  - cams3/espnow v0.0.8: 分包 52→9 包/帧 (12KB), pacing 200→280µs
    (> 1450B@54Mbps 空中时长 ≈222µs, 防队列积压; 库零改动, Peer::send
    自动查 getMaxDataLen v2.0=1470)
  - cardputer v0.8.3: 接收宏同步, 重组公式自动跟随
  - 量化收益: 丢包 1% 时帧成功率 59.3%→91.4% (帧损失暴露面降 ~4-5×)
- **实测形态**: 发送端 25fps 满速; 接收端 ~16-17fps 但**每帧完整** —
  帧完整性 > 帧率, 用户主观"比以前流畅一些" (残帧消失)

### Meta
- 发行包: cams3 v0.0.8 / cardputer v0.8.3 (VERSION 指纹)

---

## [v0.8.2] - 2026-08-25

### Added (cardputer/ 接收端)
- **DMG 真 4 阶滤镜** (`9` 键, filterMode 8): 经典 Game Boy 四阶绿
  `#0F380F→#306230→#8BAC0F→#9BBC0F` (RGB888→565 精确换算), 4×4 Bayer 抖动,
  标签 DMG 深绿。至此 9 滤镜: normal/GB/CLASS/GBC-1/GBC-2/GBA/BR-1/BR-2/DMG

### Meta
- cams3/espnow 补打 **v0.0.7 定版 tag** (8eed798): 暗光增强调参 + 背压实验全回滚,
  release 产物从 v0.0.6 升至 v0.0.7 (发行包此前漏打 tag, 实际固件已是 v0.0.7 行为)
- GBC-2 (Floyd-Steinberg) 无 PSRAM 实机帧率摸底: **13fps** (最重滤镜, 拍照构图够用, 验收通过)

---

## [v0.8.1] - 2026-08-25

### Fixed
- **拍照竞态 (P1, 真 bug)**: 快门瞬间捕获缓冲可能正被新帧覆盖 → 存出损坏 JPG/BMP。
  修复: `capFreeze` 全局冻结标志 — `captureFrame` 期间 (快门音 + SD 写入) ESP-NOW
  回调与 HTTP 解析器均拒收新帧; 解冻后重组状态随 frame_id 变化自愈。零 RAM 成本
  (无 PSRAM 设备 RAM 余额是硬约束, 不做副本科隆)。
- **快门阻塞缩短**: BMP 保存 (57.6KB) + 缩略图 (7.7KB) 从逐像素 `f.write×3` 改为
  行缓冲单次 write (160×3+4 / 64×3+4), 阻塞时间明显下降。

### Docs
- README 键位表修正: 1-8 滤镜 (原错误写 1-5), 新增 +/- EV 行, 删除不存在的
  `m` 切换键 (v0.8.0 双模自动识别后已无此键)

---

## [v0.8.0] - 2026-08-01

### Added
- **双模自动识别** (Cardputer): 开机探测循环 — ESP-NOW 听 1s → WiFi scan 3s
  找 `UnitCamS3-WiFi` → 匹配即进对应模式, 永不 reboot
- **ESP-NOW 单播 54Mbps**: beacon(0x55) 握手, 实测 **22-25fps**
  (广播模式 8-9fps 的 3 倍)
- **AP-HTTP 模式**: Content-Length 状态机 MJPEG 解析 (支持 chunked + 非 chunked),
  自动调参 QVGA + quality=10, 实测 12fps (cams3/wifi 固件)
- **无 PSRAM 适配**: 32KB×2 双缓冲 (修复 SD 注册 OOM, ESP_ERR_NO_MEM 0x101)
- **版本号编译时注入**: `APP_VERSION_RAW` 字符串化, 横幅显示 git tag
- **相册缩略图提速**: `drawJpg` 直接缩放绘制, 替代中间 canvas + 逐像素拷贝
- **发布流水线**: `release/build.sh` 三固件一键构建 + VERSION 指纹自检;
  `release/m5burner.json` 三条目 (cardputer v0.8.0 / cams3 v0.0.6 / cams3-wifi v0.0.1)

### Fixed
- 调参 quality=8 非法值损坏相机 (esp32-camera 合法范围 10~63) → 钳制为 10
- 官方固件 stream 空流 bug 实证: 响应头 + `"0"` 终止块 + 重复响应头, 无任何帧
  (官方 AsyncJpegStreamResponse 缺陷, 断电重启无法恢复 → 取景须用自研固件)

### Confirmed
- **用户实测确认取景流畅** (ESP-NOW 模式, 2026-08-01)

---

## [v0.1] - 2026-07-12

### Added (历史记录, 已废弃的 UART 项目雏形)
- 项目初始化, UART 通信, 波特率扫描, 监控/命令/拍照/转发模式
