#!/usr/bin/env python3
"""精灵素材门禁（v2 管线：每星座独立形象 + 独立 16 色调色板）：

1. 重新生成（--no-preview）并核对退出码；
2. 图源完整：10 个 PNG（有效、非空）+ 水瓶/双鱼内置 ASCII 稿（24x24 合法）；
3. 每星座：0 号透明、其余不透明；三帧互异；墨点占比合理；
4. 12 星座 F0 两两不同；
5. 生成的 C 数据量与预算。
需要 Pillow（与 gen_sprites.py 相同依赖）。
"""
import importlib.util
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "tools", "starpet", "gen_sprites.py")
SRC_DIR = os.path.join(ROOT, "tools", "starpet", "pets", "src")
BUDGET = 256_000
PNG_SIGNS = ["aries", "taurus", "gemini", "cancer", "leo", "virgo",
             "libra", "scorpio", "sagittarius", "capricorn"]
ASCII_SIGNS = {"aquarius": "AQUARIUS_ART", "pisces": "PISCES_ART"}


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def load_gen():
    spec = importlib.util.spec_from_file_location("gen_sprites", GEN)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def ink_count(grid):
    return sum(1 for row in grid for v in row if v)


def main():
    r = subprocess.run([sys.executable, GEN, "--no-preview"], cwd=ROOT)
    if r.returncode != 0:
        fail("gen_sprites.py 失败")

    m = load_gen()

    # ---- 图源完整性 -----------------------------------------------------
    for sign in PNG_SIGNS:
        path = os.path.join(SRC_DIR, sign + ".png")
        if not os.path.exists(path):
            fail(f"缺图源 {path}")
        with Image.open(path) as im:
            im.load()
            if im.width < 24 or im.height < 24:
                fail(f"{sign}.png 尺寸过小（{im.width}x{im.height}）")
    for sign, art_name in ASCII_SIGNS.items():
        art = getattr(m, art_name, None)
        if not art or len(art) != m.SIZE:
            fail(f"{sign} 内置稿 {art_name} 缺失或行数 != 24")
        if any(len(row) != m.SIZE for row in art):
            fail(f"{sign} 内置稿存在非 24 字符行")

    # ---- 每只精灵的数据属性 ---------------------------------------------
    f0_grids = {}
    for s, sign in enumerate(m.SIGNS):
        if sign in ASCII_SIGNS:
            pal, g0 = m.load_ascii_sprite(
                getattr(m, ASCII_SIGNS[sign]),
                m.AQ_PAL if sign == "aquarius" else m.PI_PAL)
        else:
            pal, g0 = m.load_png_sprite(sign)
        if len(pal) != 16:
            fail(f"{sign} 调色板 != 16 项")
        if pal[0] != 0:
            fail(f"{sign} 0 号色不是全透明")
        if any((c >> 24) & 0xFF != 0xFF for c in pal[1:]):
            fail(f"{sign} 存在非不透明调色板项")
        ink = ink_count(g0)
        if not (46 < ink < 490):
            fail(f"{sign} 墨点占比异常：{ink}")
        frames = m.make_frames(sign, g0)
        if frames[0] == frames[1] or frames[0] == frames[2] or \
                frames[1] == frames[2]:
            fail(f"{sign} 三帧未全部互异")
        f0_grids[sign] = g0

    # ---- 12 星座 F0 两两不同 --------------------------------------------
    signs = m.SIGNS
    for i in range(12):
        for j in range(i + 1, 12):
            if f0_grids[signs[i]] == f0_grids[signs[j]]:
                fail(f"{signs[i]} 与 {signs[j]} F0 完全相同")

    total = 12 * m.FRAMES * m.FRAME_BYTES + 12 * 16 * 4
    if total > BUDGET:
        fail(f"精灵数据 {total} 超预算 {BUDGET}")
    c_size = os.path.getsize(os.path.join(ROOT, "main", "sp_sprites.c"))
    print(f"test_sp_sprites: PASS（12 星座，数据 {total} B，C 源 {c_size/1024:.0f} KB）")


if __name__ == "__main__":
    main()
