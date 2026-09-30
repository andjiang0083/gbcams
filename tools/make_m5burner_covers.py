#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════
# GBCAMS — M5Burner 封面生成 (320×200, 官方固件源规格)
#
# 风格与其它项目保持一致: 深色底 + 顶部标题带 + 主体 + 底部特性行。
# 标题/侧栏文字用【设备本机字体 Font0 (5×7 GLCD)】渲染 —— 与真机同款,
# 字形从 M5GFX 的 glcdfont.h 直接解析, 保证视觉一致(不是近似字体)。
#
# 用法: python3 tools/make_m5burner_covers.py
# 产物: release/<target>/cover-320x200.png
# ═══════════════════════════════════════════════════════════════
import os, re, sys, glob
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CJK_FONT = "/System/Library/Fonts/Hiragino Sans GB.ttc"
W, H = 320, 200


def load_font0() -> dict:
    """从 M5GFX glcdfont.h 解析设备内置 5×7 字模 (每字符 5 字节列位图, bit i = 行 i)。"""
    cands = []
    for pat in (".pio/libdeps/*/M5GFX/src/lgfx/Fonts/glcdfont.h",
                "**/.pio/libdeps/*/M5GFX/src/lgfx/Fonts/glcdfont.h",
                os.path.expanduser("~/.platformio/packages/**/M5GFX/src/lgfx/Fonts/glcdfont.h")):
        cands += glob.glob(os.path.join(ROOT, pat), recursive=True)
    if not cands:
        sys.exit("找不到 glcdfont.h (先 pio run 一次让 libdeps 就位)")
    h = open(cands[0], encoding="utf-8", errors="ignore").read()
    i = h.rfind("= {")
    seg = re.sub(r"//[^\n]*", "", h[h.find("{", i): h.rfind("}")])
    data = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", seg)]
    if len(data) != 256 * 5:
        sys.exit(f"字模长度异常: {len(data)} (期望 1280)")
    return {c: data[c * 5:(c + 1) * 5] for c in range(256)}


FONT0 = load_font0()


def f0_text(d, x, y, s, scale=1, color=(255, 255, 255)):
    """用设备字模画文字; 墨水行 y+1..y+6 (与真机一致), 字距 6*scale。"""
    for ch in s:
        g = FONT0.get(ord(ch) & 0xFF) or FONT0[ord("?")]
        for col in range(5):
            for row in range(8):
                if (g[col] >> row) & 1:
                    d.rectangle([x + col * scale, y + row * scale,
                                 x + (col + 1) * scale - 1, y + (row + 1) * scale - 1], fill=color)
        x += 6 * scale
    return x


def f0_width(s, scale=1):
    return 6 * scale * len(s)


def cjk(size=12):
    return ImageFont.truetype(CJK_FONT, size)


def cjk_text(d, x, y, s, size=12, color=(255, 255, 255), anchor_left=True):
    f = cjk(size)
    b = f.getbbox(s)
    d.text((x - b[0], y - b[1]), s, font=f, fill=color)
    return x + (b[2] - b[0])


def cjk_width(s, size=12):
    f = cjk(size); b = f.getbbox(s); return b[2] - b[0]


def skeleton(accent, title, version, subtitle=None):
    """标题带(28px) + 主体区 + 底行区; 标题左 / 副标题同轴 / 版本右; 返回 (img, draw)。"""
    img = Image.new("RGB", (W, H), (10, 10, 13))
    d = ImageDraw.Draw(img)
    for y in range(H):                                   # 极淡点阵底纹
        for x in range(W):
            if (x * 7 + y * 13) % 97 == 0:
                img.putpixel((x, y), (18, 18, 24))
    d.rectangle([0, 0, W - 1, 31], fill=(16, 16, 20))    # 标题带
    d.line([(0, 31), (W - 1, 31)], fill=accent)
    tx = f0_text(d, 12, 5, title, 3, (240, 240, 245))    # 项目名 (scale3 → 墨水 8..25)
    vw = f0_width(version, 2)
    vx = W - 12 - vw
    if subtitle:                                         # 副标题与标题同轴 (CJK 10px, 墨水 12..21)
        sw = cjk_width(subtitle, 10)
        assert tx + 10 + sw + 12 <= vx, f"副标题会撞版本号: 副标题终 {tx+10+sw} vs 版本起 {vx} (缩短副标题)"
        cjk_text(d, tx + 10, 12, subtitle, 10, accent)
    f0_text(d, vx, 10, version, 2, accent)
    d.rectangle([3, 3, W - 4, H - 4], outline=(38, 38, 46))
    return img, d


def tags_centered(d, text, y, size=10, color=(215, 215, 225)):
    """底行居中 (自动缩字号避免贴边)。"""
    while cjk_width(text, size) > W - 32 and size > 8:
        size -= 1
    cjk_text(d, (W - cjk_width(text, size)) // 2, y, text, size, color)


def dmg_quantize(img, palette):
    """按设备 DMG 4 阶调色板 + Floyd-Steinberg 抖动量化 (与固件同款色值)。"""
    px = img.convert("RGB").load()
    w, h = img.size
    buf = [list(px[x, y]) for y in range(h) for x in range(w)]
    out = Image.new("RGB", (w, h))
    op = out.load()
    for y in range(h):
        for x in range(w):
            i = y * w + x
            r, g, b = buf[i]
            best = min(palette, key=lambda p: (p[0] - r) ** 2 + (p[1] - g) ** 2 + (p[2] - b) ** 2)
            op[x, y] = best
            er, eg, eb = r - best[0], g - best[1], b - best[2]
            for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    buf[j] = [min(255, max(0, v + int(e * f))) for v, e in zip(buf[j], (er, eg, eb))]
    return out


def scene_160x120():
    """合成一帧"相机画面"(夕阳山景) — 封面示意用, 随后按 DMG 4 阶量化。"""
    im = Image.new("RGB", (160, 120))
    d = ImageDraw.Draw(im)
    for y in range(120):                       # 天空渐变
        t = y / 119
        d.line([(0, y), (159, y)], fill=(int(60 + 150 * t), int(90 + 110 * t), int(150 + 60 * t)))
    d.ellipse([96, 26, 126, 56], fill=(255, 236, 190))          # 太阳
    for cx, cy, r in ((70, 78, 34), (30, 84, 26), (120, 82, 30)):   # 远山
        d.polygon([(cx - r, 96), (cx, cy), (cx + r, 96)], fill=(70, 66, 90))
    d.rectangle([0, 96, 159, 119], fill=(44, 40, 56))            # 地面
    d.polygon([(60, 96), (70, 74), (80, 96)], fill=(30, 28, 40))  # 近处独树
    d.rectangle([68, 88, 72, 96], fill=(24, 22, 32))
    d.ellipse([104, 86, 112, 96], fill=(20, 18, 26))              # 人物
    d.ellipse([106, 80, 110, 85], fill=(20, 18, 26))
    return im


DMG_PALETTE = [(15, 56, 15), (48, 98, 48), (139, 172, 15), (155, 188, 15)]  # #0F380F/#306230/#8BAC0F/#9BBC0F


def device_screen(scale=1):
    """复刻真机取景画面: 左侧画面区 (180×135, DMG 滤镜) + 右侧 60px 侧栏 HUD(设备 Font0)。"""
    scr = Image.new("RGB", (240, 135), (0, 0, 0))
    img = dmg_quantize(scene_160x120(), DMG_PALETTE).resize((180, 135), Image.NEAREST)
    scr.paste(img, (0, 0))
    d = ImageDraw.Draw(scr)
    WHITE, ORANGE, YELLOW, RED, DGREEN, CYAN, DGREY = (255,255,255), (255,165,0), (255,255,0), (255,0,0), (0,128,0), (0,255,255), (85,85,85)
    x = 184
    f0_text(d, x, 2, "DMG", 1, DGREEN)              # ① 滤镜短名
    f0_text(d, x, 16, "FRM3", 1, ORANGE)            # ② 相框编号
    d.rectangle([x, 28, x + 21, 28 + 16], outline=WHITE)      #    CRT 相框 mini (双线)
    d.rectangle([x + 3, 31, x + 18, 41], outline=WHITE)
    f0_text(d, x, 46, "EV+1", 1, ORANGE)            # ③ 曝光
    f0_text(d, x, 58, "ESP-NOW", 1, CYAN)           # ④ 模式
    f0_text(d, x, 70, "24.8FPS", 1, YELLOW)         # ⑤ 帧率 (与封面 25fps 文案一致)
    f0_text(d, x, 106, "CAPT0021", 1, WHITE)        # ⑦ 拍照反馈
    f0_text(d, x, 124, "H HELP", 1, (120, 120, 130))  # ⑧ 帮助提示 (封面提亮)
    return scr if scale == 1 else scr.resize((240 * scale, 135 * scale), Image.NEAREST)


def cover_cardputer(path):
    ACC = (255, 150, 40)
    img, d = skeleton(ACC, "GBCAMS", "v0.8.8", "无线取景器")
    scr = device_screen()
    img.paste(scr, (40, 38)); d.rectangle([39, 37, 280, 173], outline=(70, 70, 80))
    tags_centered(d, "ESP-NOW 25fps · 9 滤镜 · 4 相框 · 中文帮助 · SD 拍照相册", 178)
    img.save(path); return path


def _module(d, ACC, lens_glass=(38, 52, 74), highlight=(120, 160, 200)):
    """UnitCamS3 模块示意图: 黑 PCB + 丝印 + 排针 + 芯片 + 镜头 + USB-C + 电波。"""
    bx, by, bw, bh = 26, 46, 138, 112
    d.rounded_rectangle([bx, by, bx + bw, by + bh], 4, fill=(18, 19, 23), outline=(62, 66, 76))
    d.rounded_rectangle([bx + 6, by + 6, bx + 70, by + 70], 3, fill=(26, 28, 34), outline=(70, 74, 84))
    cx, cy = bx + 6 + 32, by + 6 + 32
    d.ellipse([cx - 24, cy - 24, cx + 24, cy + 24], fill=(11, 12, 15), outline=(92, 98, 110), width=2)
    d.ellipse([cx - 16, cy - 16, cx + 16, cy + 16], fill=lens_glass, outline=(120, 130, 150))
    d.ellipse([cx - 6, cy - 7, cx + 4, cy + 3], fill=highlight)
    r = 0
    for i, (px, py, s) in enumerate(((bx + 12, by + 84, (30, 30)), (bx + 20, by + 92, (20, 16)),
                                     (bx + 46, by + 84, (26, 24)), (bx + 76, by + 90, (14, 12)))):
        d.rectangle([px, py, px + s[0], py + s[1]], fill=(34, 36, 42), outline=(64, 68, 78))
    for i in range(7):                       # 排针 (顶部一排金色焊盘)
        d.rectangle([bx + 82 + i * 7, by + 8, bx + 86 + i * 7, by + 12], fill=(196, 164, 74))
    d.rectangle([bx + 30, by + 104, bx + 108, by + 111], fill=(14, 15, 18), outline=(72, 76, 86))
    d.ellipse([bx + bw - 26, by + 14, bx + bw - 20, by + 20], fill=(255, 255, 255))   # 状态灯
    f0_text(d, bx + 6, by + 74, "UNITCAM S3-5MP", 1, (150, 155, 165))
    for rr in (26, 40, 54):                  # 电波
        d.arc([cx + 24 - rr, cy - rr, cx + 24 + rr, cy + rr], -55, 55, fill=ACC, width=2)


def cover_cams3(path):
    ACC = (0, 220, 255)
    img, d = skeleton(ACC, "GBCAMS", "v0.0.9", "ESP-NOW 发送端")
    _module(d, ACC)
    d.line([(186, 44), (186, 172)], fill=(45, 48, 56))
    f0_text(d, 198, 50, "12KB x 9 PKT", 1, ACC)
    f0_text(d, 198, 64, "ONE FRAME", 1, (200, 205, 215))
    f0_text(d, 198, 78, "25 FPS", 1, (200, 205, 215))
    cjk_text(d, 198, 104, "无需路由器", 11, (170, 175, 185))
    cjk_text(d, 198, 126, "自动发现配对", 11, (170, 175, 185))
    cjk_text(d, 198, 148, "失联自动重连", 11, (170, 175, 185))
    tags_centered(d, "配合 GBCAMS 接收端 (M5Cardputer) 使用 · ESP-NOW v2.0 大包", 178)
    img.save(path); return path


def cover_cams3_wifi(path):
    ACC = (255, 205, 60)
    img, d = skeleton(ACC, "GBCAMS", "v0.0.1", "AP-HTTP 发送端")
    _module(d, ACC, lens_glass=(52, 46, 26), highlight=(206, 186, 118))
    d.line([(186, 44), (186, 172)], fill=(45, 48, 56))
    f0_text(d, 198, 50, "HTTP MJPEG", 1, ACC)
    f0_text(d, 198, 64, "12 FPS", 1, (200, 205, 215))
    cjk_text(d, 198, 104, "兼容官方 App", 11, (170, 175, 185))
    cjk_text(d, 198, 126, "同 SSID 同接口", 11, (170, 175, 185))
    cjk_text(d, 198, 148, "免装新发送端", 11, (170, 175, 185))
    tags_centered(d, "GBCAMS 接收端检测到官方 AP 路径时自动使用 (右上黄标)", 178)
    img.save(path); return path


if __name__ == "__main__":
    for rel, fn in (("release/cardputer/cover-320x200.png", cover_cardputer),
                    ("release/cams3/cover-320x200.png", cover_cams3),
                    ("release/cams3-wifi/cover-320x200.png", cover_cams3_wifi)):
        p = os.path.join(ROOT, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        fn(p)
        print("✓", rel, Image.open(p).size)
