# GBCAMS 构建与刷机

[English](BUILDING.md) · **中文**

三个彼此独立的 PlatformIO 工程：`cardputer/`（接收端）、`cams3/espnow/` 与 `cams3/wifi/`（发送端）。
不需要别的 —— PlatformIO 会自己拉 ESP32 工具链和 Arduino 框架。

## 前置

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/)（`pio`，Python 3.8+）
- 两根 USB-C 线、M5Cardputer + UnitCamS3-5MP
- 想刷 merged 全镜像的话再要 `esptool`（PlatformIO 自带，或 `pip install esptool`）

```bash
pio --version        # 本项目开发用的是 6.x
```

## 构建

```bash
cd cardputer        && pio run        # 接收端
cd cams3/espnow     && pio run        # 发送端 ESP-NOW   (推荐)
cd cams3/wifi       && pio run        # 发送端 AP-HTTP   (兼容官方 App)
```

一次出完整发行包（三端 + `VERSION` + merged 镜像）：

```bash
./release/build.sh      # 产出 release/<target>/firmware.factory.bin (merged, flash_mode=dio)
python3 tools/make_m5burner_covers.py   # 重出 320x200 封面
```

构建缓存各自在 `.pio/` 下。**二进制故意不入库** —— 它们作为 GitHub release 附件发布，
仓库里只留 `release/*/firmware.factory.bin.sha256` 校验值。

## 依赖是故意钉版的

```ini
# cardputer/platformio.ini
m5stack/M5Unified@0.2.21      ; 它声明的传递依赖 (M5GFX / IRremote) 也一并显式钉住
m5stack/M5GFX@0.2.28
m5stack/M5Cardputer@1.1.1
IRremote@4.7.1
# cams3/*/platformio.ini
https://github.com/espressif/esp32-camera.git#v2.0.0
```

用 `^0.2.15` 这种范围写法的后果是：别人克隆当天解析到最新版，编出来的固件**和发布件不是同一个**。
如果你克隆后编出来的镜像尺寸和 release notes 对不上，先查解析到的库版本，别急着怀疑代码。

**PlatformIO 双副本坑。** 当某个传递依赖也声明了你钉的那个库时，PlatformIO 可能把**两份**都装上 ——
`.pio/libdeps/` 里会同时出现 `M5GFX` 和 `M5GFX@0.2.31`。这是真实的代码差异（我们踩过：
`.text` 差了 35 万字节）。处理办法：

```bash
rm -rf cardputer/.pio/libdeps/m5cardputer/M5GFX*
cd cardputer && pio run -t clean && pio run
ls .pio/libdeps/m5cardputer/     # 应当只剩一个 M5GFX 目录，且版本等于钉的值
```

## 刷机

### PlatformIO（开发用）

```bash
cd cardputer      && pio run -t upload --upload-port /dev/cu.usbmodem101
cd cams3/espnow   && pio run -t upload --upload-port /dev/cu.usbmodem1101
```

### esptool + merged 镜像（和 M5Burner 等价）

```bash
esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_size 8MB \
  0x0 release/cardputer/firmware.factory.bin
```

### M5Burner

导入 `release/m5burner.json`（M5Burner → **Custom** → **Import Custom FW**），逐设备 Burn。
设备类别：接收端条目选 **Cardputer**，UnitCamS3 条目选 **TimerCamera**。

## 会造成黑屏的坑

| 坑 | 为什么致命 | 正确做法 |
|---|---|---|
| 刷了 `firmware.bin`（app-only） | M5Burner 固定写 `0x0`，而 app 镜像期望在 `0x10000`，于是覆盖 bootloader | 只发 `firmware.factory.bin` —— PlatformIO 12+ 的 factory 文件本身就是 merged（bootloader@0x0、分区表@0x8000、boot_app0@0xe000、app@0x10000） |
| `flash_mode` 不一致 | bootloader 编译时的 flash 模式与 merged 头部不一致 → 能启动但起不来 flash | `platformio.ini` 里 `board_build.flash_mode = dio`，合并时也写 `--flash_mode dio` |
| 分区表落在 `0x9000` | ESP-IDF v5 布局用 `0x9000`；本项目 bootloader（PlatformIO 钉的框架，v4 时代）找的是 `0x8000` | 合并用 `0x8000` |
| 用了 `--pad-to-size` / `--fill-flash-size` | 把镜像撑到整个 8 MB，毫无必要还容易误导 | 别用；正确的 merged 镜像约 1.0–1.6 MB |
| `Failed to connect to ESP32-S3: No serial data received` | 芯片在休眠，或上一版固件没释放 USB | 按住 **KEY1**（GPIO0）→ 点一下 **RST** → 继续按住 KEY1 → 执行刷机 |

发布前先验 merged 镜像：

```bash
python3 - <<'EOF'
d = open('release/cardputer/firmware.factory.bin','rb').read()
print('size', len(d))
print('bootloader 0x0     ', hex(d[0]),     '应为 0xe9')
print('flash mode 字节    ', hex(d[2]),     '应为 0x02 (DIO)')
print('partitions 0x8000  ', hex(d[0x8000]),'应为 0xaa')
print('app 0x10000        ', hex(d[0x10000]),'应为 0xe9')
EOF
```

## 怎么证明"编出来的是同一个固件"

同一份源码两次构建，整文件 `sha256` **永远不同** —— app descriptor 里带构建日期时间和 ELF 哈希。
要比就比机器码：

```bash
xtensa-esp32s3-elf-objcopy -O binary --only-section='.flash.text*' -j '.text*' \
  .pio/build/m5cardputer/firmware.elf a.bin   # 另一棵树同样跑一次，然后
shasum a.bin b.bin
```

段内容一致 + 尺寸一致 = 同一个固件，与时间戳无关。

### 路径串会让代码整体位移（别被 diff 骗了）

同一份源码，从 `.pio/libdeps/m5cardputer/M5GFX` 编译和从
`.pio/libdeps/m5cardputer/M5GFX@0.2.28` 编译（**同一个 M5GFX 版本**），出来的镜像 `.text`
能差约 35 万字节 —— 原因是库源码用 `__FILE__` 把自身路径嵌进了断言和日志字符串，
某条字符串变长，后面所有内容整体位移。尺寸一模一样，代码一行没改。
遇到这种"巨量差异"先比 `strings` 输出：真正的差异只有几条路径和构建时间。
要比就比 `.flash.text`（见上），不要逐字节比。

## 资源预算（接收端 M5Cardputer）

Cardputer **没有 PSRAM**，所有内存都要按 DRAM 算。当前发行镜像：接收端 merged 镜像
**1,596,160 B**，其中内置 `efontCN_12` 中文字库约 **151 KB**。想再塞大字体、额外帧缓冲或更大的
重组缓冲，就得先放弃点别的 —— 先量再假设。

## 不用串口的验收方式

本项目自己的验收都在屏幕上做（射频忙时 USB CDC 不可靠）：侧栏 `FPS`、丢帧计数、
`3/3` 状态帮助页（`H` → `TAB` `TAB`，里面有 `T`/`O`/`S` 帧计数）、相册、拍照文件名回显。
见 [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md)。
