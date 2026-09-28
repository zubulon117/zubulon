#!/usr/bin/env python3
"""命定星宠像素精灵生成器 v2（每星座独立形象 + 独立 16 色调色板）。

图源：
  tools/starpet/pets/src/<sign>.png  AI 像素画（10 只）
  水瓶座 / 双鱼座为本脚本内置手绘 24x24 像素稿（AQUARIUS_ART / PISCES_ART）

处理（PNG 路径）：
  1. 亮度掩码裁出主体（排除底部 14% 水印区），方形补齐
  2. 缩到 24x24（LANCZOS）
  3. 从边缘洪泛填充暗色背景 -> 透明（眼睛等内部暗区不连通边缘，不受影响）
  4. 不透明像素中位切法量化为 15 色；索引 0 固定全透明
  5. 调色板按亮度升序排列，便于眨眼帧取"最深色"作闭眼线

帧：
  F0 常态 / F1 眨眼（EYES 配置的行区间内，深色像素 -> 最深调色板色）
  F2 常态 + 四角星光（最亮调色板色）

输出：
  main/sp_sprites.c/.h   每星座 16 色 ARGB 调色板 + I4 帧（低 nibble 在前）
  tools/starpet/pets/preview.png（12 只 x 3 帧预览）
用法：python gen_sprites.py [--no-preview]
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PET_DIR = os.path.join(HERE, "pets")
SRC_DIR = os.path.join(PET_DIR, "src")
OUT_C = os.path.join(REPO, "main", "sp_sprites.c")
OUT_H = os.path.join(REPO, "main", "sp_sprites.h")
PREVIEW = os.path.join(PET_DIR, "preview.png")

SIZE = 24
STRIDE = SIZE // 2
FRAME_BYTES = STRIDE * SIZE  # 288
FRAMES = 3
SIGNS = ["aries", "taurus", "gemini", "cancer", "leo", "virgo",
         "libra", "scorpio", "sagittarius", "capricorn", "aquarius", "pisces"]
SIGN_CN = ["白羊", "金牛", "双子", "巨蟹", "狮子", "处女",
           "天秤", "天蝎", "射手", "摩羯", "水瓶", "双鱼"]
BUDGET = 256_000

BG_LUMA = 48        # 低于此亮度视为背景候选
WATERMARK_ROWS = 0.14  # 底部水印区比例，裁剪前忽略

# 按星座覆盖背景阈值：暗色主体（天蝎/摩羯）需要更低阈值避免被当作背景。
BG_LUMA_OVERRIDE = {"scorpio": 26, "capricorn": 26}
# 主体中位亮度低于该值时整体提亮（深色屏幕上暗色主体看不清）。
BRIGHTEN_BELOW = 70
BRIGHTEN_GAIN = 1.75
# 按星座覆盖提亮增益（摩羯深灰岩石主体需要更强提亮）。
BRIGHTEN_GAIN_OVERRIDE = {"capricorn": 2.1}

# 眨眼帧：sign -> (行0, 行1, [(列起, 列止), ...])，区间内深色像素变最深色。
# 位置按 preview.png 人工标定。
EYES = {}

# 星光帧点缀位置（x, y），避开主体中心。
SPARKLES = [(1, 4), (22, 3), (2, 20), (21, 21)]

# ------------------------------------------------------- 手绘：水瓶座 ---
# 水壶倾泻星光 + 水滴小精灵。字符 -> (R,G,B)
AQ_PAL = {
    "1": (26, 27, 48),      # 描边
    "2": (111, 183, 232),   # 水蓝身体
    "3": (168, 216, 245),   # 浅蓝高光
    "4": (246, 196, 83),    # 星金（壶）
    "5": (251, 221, 142),   # 亮金
    "6": (27, 28, 48),      # 瞳
    "7": (244, 154, 176),   # 腮红
    "8": (255, 255, 255),   # 高光白
    "9": (86, 130, 200),    # 暗蓝阴影
}
AQUARIUS_ART = [
    "........................",
    "............4444........",
    "...8.......444444....8..",
    "..........44444444......",
    ".........4444444444.....",
    ".........4444444444.....",
    "..........44444444..5...",
    "...........333333.......",
    "..........33333333......",
    ".........113333311......",
    "........1122222211......",
    ".......112222222211.....",
    "......11222222222211....",
    "......12682222228611....",
    "......12682222228611....",
    ".....1122222222222211...",
    ".....1722222222222271...",
    ".....1122222222222211...",
    "......11299999992211....",
    "......11111111111111....",
    ".......1.9.9..9.9.1.....",
    "...........22...........",
    "..5....8..2222....5.....",
    "........................",
]

# ------------------------------------------------------- 手绘：双鱼座 ---
# 蓝鱼（上，朝右）与粉鱼（下，朝左）环绕，气泡点缀。
PI_PAL = {
    "1": (26, 27, 48),      # 描边
    "2": (111, 183, 232),   # 蓝鱼身
    "3": (168, 216, 245),   # 蓝鱼腹
    "4": (244, 154, 176),   # 粉鱼身
    "5": (252, 200, 214),   # 粉鱼腹
    "6": (27, 28, 48),      # 瞳
    "7": (255, 255, 255),   # 高光白
    "8": (246, 196, 83),    # 星金气泡
}
PISCES_ART = [
    "........................",
    "....7...................",
    "...1111111..........8...",
    "..11222222211...........",
    ".112222222222111........",
    "112222682222222211......",
    "11222268222222222211....",
    ".1122222222233332211....",
    "..111222222233333211....",
    "....111222211333111.....",
    "......1111..1111........",
    ".........7..............",
    "........................",
    "......8.........1111....",
    "..............11122211..",
    "............111444422211",
    "..........11144444442211",
    "........1114444444864211",
    "........1144455544864211",
    ".........11144555544211.",
    "...........11144441111..",
    ".............111111.....",
    "....7.................7.",
    "........................",
]


def luma(rgb):
    return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) // 1000


def crop_body(img, bg_luma=BG_LUMA):
    """亮度掩码裁主体（忽略底部水印），方形补齐后返回裁剪图。"""
    w, h = img.size
    px = img.load()
    cut_h = int(h * (1.0 - WATERMARK_ROWS))
    xs, ys = [], []
    for y in range(cut_h):
        for x in range(w):
            if luma(px[x, y][:3]) > bg_luma:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise SystemExit("未找到主体")
    m = 8
    x0, x1 = max(0, min(xs) - m), min(w - 1, max(xs) + m)
    y0, y1 = max(0, min(ys) - m), min(cut_h - 1, max(ys) + m)
    side = max(x1 - x0 + 1, y1 - y0 + 1)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    half = side // 2
    x0 = max(0, min(cx - half, w - side))
    y0 = max(0, min(cy - half, cut_h - side))
    side = min(side, w - x0, cut_h - y0)
    return img.crop((x0, y0, x0 + side, y0 + side))


def bg_alpha(img, bg_luma=BG_LUMA):
    """边缘洪泛：与边缘连通的暗像素 -> 透明。返回 24x24 的 bool 不透明掩码。"""
    w, h = img.size
    px = img.load()
    dark = [[luma(px[x, y][:3]) < bg_luma for x in range(w)]
            for y in range(h)]
    seen = [[False] * w for _ in range(h)]
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            if dark[y][x] and not seen[y][x]:
                stack.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if dark[y][x] and not seen[y][x]:
                stack.append((x, y))
    while stack:
        x, y = stack.pop()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y][x] or not dark[y][x]:
            continue
        seen[y][x] = True
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return [[not seen[y][x] for x in range(w)] for y in range(h)]


def quantize_sprite(img, opaque):
    """不透明像素量化 15 色，返回 (palette[16] ARGB, grid[24][24] 索引)。"""
    w, h = img.size
    px = img.load()
    pixels = [px[x, y][:3] for y in range(h) for x in range(w)
              if opaque[y][x]]
    if not pixels:
        raise SystemExit("主体为空")
    flat = Image.new("RGB", (len(pixels), 1))
    flat.putdata(pixels)
    q = flat.quantize(colors=15, method=Image.MEDIANCUT)
    pal_rgb = q.getpalette()[:45]
    colors = [(pal_rgb[i * 3], pal_rgb[i * 3 + 1], pal_rgb[i * 3 + 2])
              for i in range(15)]
    colors.sort(key=luma)  # 亮度升序：1 最深，15 最亮
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette([c for rgb in colors for c in rgb] + [0] * (768 - 45))

    grid = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            if not opaque[y][x]:
                continue
            rgb = px[x, y][:3]
            # 最近色匹配（15 色，暴力即可）。
            best, bi = 1 << 30, 0
            for i, c in enumerate(colors):
                d = sum((a - b) * (a - b) for a, b in zip(rgb, c))
                if d < best:
                    best, bi = d, i
            grid[y][x] = bi + 1
    palette = [0x00000000] + [0xFF000000 | (r << 16) | (g << 8) | b
                              for r, g, b in colors]
    return palette, grid


def brighten_if_dark(img, opaque, sign=""):
    """主体中位亮度过低时整体提亮（深底色屏幕上保证可辨识）。"""
    px = img.load()
    w, h = img.size
    lumas = sorted(luma(px[x, y][:3]) for y in range(h) for x in range(w)
                   if opaque[y][x])
    if not lumas:
        return img
    median = lumas[len(lumas) // 2]
    if median >= BRIGHTEN_BELOW:
        return img
    gain = BRIGHTEN_GAIN_OVERRIDE.get(sign, BRIGHTEN_GAIN)
    out = Image.new("RGB", (w, h))
    op = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y][:3]
            op[x, y] = (min(255, int(r * gain)),
                        min(255, int(g * gain)),
                        min(255, int(b * gain)))
    return out


def load_png_sprite(sign):
    bg = BG_LUMA_OVERRIDE.get(sign, BG_LUMA)
    img = Image.open(os.path.join(SRC_DIR, sign + ".png")).convert("RGB")
    img = crop_body(img, bg)
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    opaque = bg_alpha(img, bg)
    img = brighten_if_dark(img, opaque, sign)
    return quantize_sprite(img, opaque)


def load_ascii_sprite(art, pal_map):
    """手绘稿 -> (palette[16] ARGB, grid)。颜色按亮度升序分配索引。"""
    colors = sorted(set(pal_map.values()), key=luma)
    idx_of = {c: i + 1 for i, c in enumerate(colors)}
    palette = [0x00000000] + [0xFF000000 | (r << 16) | (g << 8) | b
                              for r, g, b in colors]
    palette += [0xFF000000] * (16 - len(palette))
    grid = [[0] * SIZE for _ in range(SIZE)]
    for y, row in enumerate(art):
        assert len(row) == SIZE, f"手绘稿第 {y} 行长度 {len(row)}"
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            grid[y][x] = idx_of[pal_map[ch]]
    return palette, grid


def make_frames(sign, grid):
    """F0 常态 / F1 眨眼（无眼睛配置则整体下移 1px 作呼吸帧）/ F2 星光。"""
    f0 = [row[:] for row in grid]
    f1 = [row[:] for row in grid]
    if sign in EYES:
        r0, r1, cols = EYES[sign]
        for r in range(r0, r1 + 1):
            for c0, c1 in cols:
                for c in range(c0, c1 + 1):
                    if 0 < f1[r][c] <= 4:   # 深色区（瞳/描边）-> 最深色=闭眼线
                        f1[r][c] = 1
    else:
        # 呼吸帧：整体下移 1 行，首行清空。
        f1 = [[0] * SIZE] + [row[:] for row in grid[:-1]]
    f2 = [row[:] for row in grid]
    for x, y in SPARKLES:
        if f2[y][x] == 0:
            f2[y][x] = 15   # 最亮色
    return [f0, f1, f2]


def pack_i4(grid):
    out = bytearray(FRAME_BYTES)
    k = 0
    for y in range(SIZE):
        for x in range(0, SIZE, 2):
            out[k] = (grid[y][x] & 0xF) | ((grid[y][x + 1] & 0xF) << 4)
            k += 1
    return bytes(out)


def emit_c(palettes, all_frames):
    lines = [
        "// 自动生成：tools/starpet/gen_sprites.py，请勿手改。",
        '#include "sp_sprites.h"',
        '#include <stddef.h>',
        "",
        "// 每星座独立 16 色 ARGB8888 调色板（0 号全透明，按亮度升序）。",
    ]
    for s, sign in enumerate(SIGNS):
        lines.append(f"static const uint32_t pal_{sign}[16] = {{")
        for i in range(0, 16, 4):
            lines.append("    " + ", ".join(f"0x{palettes[s][j]:08X}"
                                            for j in range(i, i + 4)) + ",")
        lines.append("};")
    lines.append("")
    for s, sign in enumerate(SIGNS):
        for f in range(FRAMES):
            data = all_frames[s][f]
            lines.append(f"static const uint8_t {sign}_f{f}[SP_SPRITE_FRAME_BYTES] = {{")
            for y in range(SIZE):
                chunk = data[y * STRIDE:(y + 1) * STRIDE]
                lines.append("    " + ",".join(f"0x{b:02X}" for b in chunk) + ",")
            lines.append("};")
        lines.append("")
    lines.append("static const uint8_t *const SPRITES[SP_SIGN_COUNT][SP_SPRITE_FRAMES] = {")
    for sign in SIGNS:
        lines.append("    {" + ",".join(f"{sign}_f{f}" for f in range(FRAMES)) + "},")
    lines.append("};")
    lines.append("static const uint32_t *const PALETTES[SP_SIGN_COUNT] = {")
    lines.append("    " + ", ".join(f"pal_{s}" for s in SIGNS))
    lines.append("};")
    lines.append("""
const uint8_t *sp_sprite_frame(uint8_t sign, uint8_t frame)
{
    if (sign >= SP_SIGN_COUNT || frame >= SP_SPRITE_FRAMES) {
        return SPRITES[0][0];
    }
    return SPRITES[sign][frame];
}

const uint32_t *sp_sprite_palette(uint8_t sign)
{
    if (sign >= SP_SIGN_COUNT) {
        return PALETTES[0];
    }
    return PALETTES[sign];
}
""")
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""// 自动生成：tools/starpet/gen_sprites.py，请勿手改。
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define SP_SPRITE_SIZE {SIZE}
#define SP_SPRITE_STRIDE {STRIDE}
#define SP_SPRITE_FRAME_BYTES {FRAME_BYTES}
#define SP_SPRITE_FRAMES {FRAMES}
#define SP_SIGN_COUNT 12

// 每星座独立 I4 调色板（ARGB8888 x16，0 号全透明，按亮度升序）。
const uint32_t *sp_sprite_palette(uint8_t sign);

// 取某星座某帧的 I4 像素（低 nibble 在前）；越界回退白羊第 0 帧。
const uint8_t *sp_sprite_frame(uint8_t sign, uint8_t frame);

#ifdef __cplusplus
}}
#endif
""")


def render_preview(palettes, grids):
    scale = 4
    pad = 2
    label = 10
    pitch_y = SIZE * scale + label + pad
    w = 40 + (SIZE * scale + pad) * FRAMES
    h = pitch_y * 12 + pad
    img = Image.new("RGBA", (w, h), (11, 16, 41, 255))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(img)
    for s in range(12):
        y0 = pad + s * pitch_y
        draw.text((2, y0), f"{s+1:02d} {SIGN_CN[s]}", fill=(246, 196, 83, 255))
        for f in range(FRAMES):
            x0 = 40 + f * (SIZE * scale + pad)
            for y in range(SIZE):
                for x in range(SIZE):
                    v = grids[s][f][y][x]
                    argb = palettes[s][v]
                    a = (argb >> 24) & 0xFF
                    if a == 0:
                        continue
                    rgba = ((argb >> 16) & 0xFF, (argb >> 8) & 0xFF,
                            argb & 0xFF, a)
                    for dy in range(scale):
                        for dx in range(scale):
                            img.putpixel((x0 + x * scale + dx,
                                          y0 + label + y * scale + dy), rgba)
    img.save(PREVIEW)


def main():
    no_preview = "--no-preview" in sys.argv
    palettes, grids, packed = [], [], []
    for sign in SIGNS:
        if sign == "aquarius":
            pal, g0 = load_ascii_sprite(AQUARIUS_ART, AQ_PAL)
        elif sign == "pisces":
            pal, g0 = load_ascii_sprite(PISCES_ART, PI_PAL)
        else:
            pal, g0 = load_png_sprite(sign)
        frames = make_frames(sign, g0)
        palettes.append(pal)
        grids.append(frames)
        packed.append([pack_i4(g) for g in frames])
    emit_c(palettes, packed)
    total = 12 * FRAMES * FRAME_BYTES + 12 * 16 * 4
    if total > BUDGET:
        raise SystemExit(f"精灵超预算 {total} > {BUDGET}")
    if not no_preview:
        render_preview(palettes, grids)
    print(f"sprites OK：12 星座 x {FRAMES} 帧 + 独立调色板，{total} 字节 -> {PREVIEW}")


if __name__ == "__main__":
    main()
