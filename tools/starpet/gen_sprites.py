#!/usr/bin/env python3
"""命定星宠像素精灵生成器。

每个 tools/starpet/pets/<sign>.txt 是 24x24 覆盖图：
  空格  = 保留基础星宠身体（身体由本脚本统一定义，保证 12 只体型一致）
  其它  = 覆盖字符（调色板索引或眼睛/闪光标记）

标记：
  . 透明  1 描边  2 身体  3 肚皮  4 暗部
  o 睁眼瞳  h 眼神高光   7 腮红
  8 金  9 亮金  a 奶白  b 水蓝  c 叶绿  d 朱红  e 星紫  f 纯白
  x 闪光（仅第 3 帧出现）

每个底色生成 3 帧：
  F0 睁眼无闪光 / F1 眨眼（o->1, h->2）/ F2 睁眼+闪光（x->f）

输出：
  main/sp_sprites.c/.h   I4 nibble（低 nibble 在前，行 stride=12）
  tools/starpet/pets/preview.png（12 只 × 3 帧预览，需 Pillow）
用法：python gen_sprites.py [--no-preview]
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PET_DIR = os.path.join(HERE, "pets")
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

# ARGB8888。0 全透明。
PALETTE = [
    0x00000000,  # 0 透明
    0xFF2A2B45,  # 1 夜蓝描边
    0xFFF2E3C8,  # 2 奶油身体
    0xFFFFF7E8,  # 3 肚皮高光
    0xFFD8C4A4,  # 4 暗部/脚
    0xFF1B1C30,  # 5 瞳
    0xFFFFFFFF,  # 6 眼神高光
    0xFFF49AB0,  # 7 腮红
    0xFFF6C453,  # 8 星金
    0xFFFBDD8E,  # 9 亮金
    0xFFEFE6D2,  # a 角/奶白
    0xFF6FB7E8,  # b 水蓝
    0xFF7BC77B,  # c 叶绿
    0xFFE86A6A,  # d 朱红
    0xFFA98BE0,  # e 星紫
    0xFFFFFFFF,  # f 纯白
]

# ---- 基础身体（24x24，o/h 为眼睛标记），覆盖图空格表示沿用此模板 ----------
def _build_body():
    g = [["."] * SIZE for _ in range(SIZE)]

    def fill(r, cols, ch):
        for c in cols:
            g[r][c] = ch

    fill(6, range(6, 18), "1")
    fill(7, [5, 18], "1"); fill(7, range(6, 18), "2")
    for r in (8, 9, 10, 12, 14):
        fill(r, [4, 19], "1"); fill(r, range(5, 19), "2")
    # 眼睛（o/h 标记，第 11 行）。
    fill(11, [4, 19], "1"); fill(11, range(5, 19), "2")
    g[11][7] = "o"; g[11][8] = "h"; g[11][15] = "h"; g[11][16] = "o"
    # 腮红。
    fill(13, [4, 19], "1"); fill(13, range(5, 19), "2")
    g[13][6] = "7"; g[13][17] = "7"
    # 肚皮。
    for r in (15, 16, 17):
        fill(r, [4, 19], "1"); fill(r, [5, 6, 17, 18], "2")
        fill(r, range(7, 17), "3")
    fill(18, [4, 19], "1"); fill(18, [5, 6, 7, 16, 17, 18], "2")
    fill(18, range(8, 16), "3")
    fill(19, [5, 18], "1"); fill(19, range(6, 18), "2")
    # 脚。
    fill(20, [7, 8, 9, 10, 13, 14, 15, 16], "1")
    fill(21, [8, 9, 14, 15], "1")
    return ["".join(row) for row in g]


BODY = _build_body()
assert all(len(r) == SIZE for r in BODY), [len(r) for r in BODY]

VALID_CHARS = set(".1234oh789abcdefx ")
MARKER_OPEN = ord("0") + 5   # o -> 5
MARKER_SHINE = ord("0") + 6  # h -> 6


def load_overlay(path):
    rows = []
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if line.startswith("name:"):
            continue
        rows.append(line)
    if len(rows) != SIZE:
        raise SystemExit(f"{path} 应有 {SIZE} 行，实际 {len(rows)}")
    for i, row in enumerate(rows):
        if len(row) != SIZE:
            raise SystemExit(f"{path} 第 {i} 行长度 {len(row)} != {SIZE}：{row!r}")
        bad = set(row) - VALID_CHARS
        if bad:
            raise SystemExit(f"{path} 第 {i} 行非法字符 {bad!r}")
    return rows


def compose(sign, overlay, frame):
    """返回 24x24 索引矩阵（list[list[int]]）。"""
    grid = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            ch = overlay[y][x]
            base = BODY[y][x]
            src = base if ch == " " else ch
            if src == " " or src == ".":
                idx = 0
            elif src == "o":
                idx = 1 if frame == 1 else 5
            elif src == "h":
                idx = 2 if frame == 1 else 6
            elif src == "x":
                idx = 0xF if frame == 2 else 0
            else:
                idx = int(src, 16)
            grid[y][x] = idx
    return grid


def pack_i4(grid):
    out = bytearray(FRAME_BYTES)
    k = 0
    for y in range(SIZE):
        for x in range(0, SIZE, 2):
            lo = grid[y][x] & 0xF
            hi = grid[y][x + 1] & 0xF
            out[k] = lo | (hi << 4)
            k += 1
    return bytes(out)


def emit_c(all_frames):
    lines = [
        "// 自动生成：tools/starpet/gen_sprites.py，请勿手改。",
        '#include "sp_sprites.h"',
        "",
        "const uint32_t sp_sprite_palette_argb[16] = {",
    ]
    for i in range(0, 16, 4):
        lines.append("    " + ", ".join(f"0x{PALETTE[j]:08X}"
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
    lines.append("""
const uint8_t *sp_sprite_frame(uint8_t sign, uint8_t frame)
{
    if (sign >= SP_SIGN_COUNT || frame >= SP_SPRITE_FRAMES) {
        return SPRITES[0][0];
    }
    return SPRITES[sign][frame];
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

// I4 调色板（ARGB8888，0 号全透明），UI 启动时灌入 canvas。
extern const uint32_t sp_sprite_palette_argb[16];

// 取某星座某帧的 I4 像素（低 nibble 在前）；越界回退白羊第 0 帧。
const uint8_t *sp_sprite_frame(uint8_t sign, uint8_t frame);

#ifdef __cplusplus
}}
#endif
""")


def render_preview(grids):
    from PIL import Image
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
                    argb = PALETTE[v]
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
    grids = []
    packed = []
    for sign in SIGNS:
        overlay = load_overlay(os.path.join(PET_DIR, sign + ".txt"))
        sign_frames = []
        sign_packed = []
        for f in range(FRAMES):
            g = compose(sign, overlay, f)
            sign_frames.append(g)
            sign_packed.append(pack_i4(g))
        grids.append(sign_frames)
        packed.append(sign_packed)
    emit_c(packed)
    total = 12 * FRAMES * FRAME_BYTES
    if total > BUDGET:
        raise SystemExit(f"精灵超预算 {total} > {BUDGET}")
    if not no_preview:
        render_preview(grids)
    print(f"sprites OK：12 星座 x {FRAMES} 帧，{total} 字节，预览 -> {PREVIEW}")


if __name__ == "__main__":
    main()
