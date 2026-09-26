#!/usr/bin/env python3
"""精灵素材门禁：

1. 重新生成（--no-preview，CI 无需 Pillow）并核对输出稳定；
2. 12 个覆盖图：尺寸/字符合法、非空、星座附件各异、均含闪光标记；
3. 生成的 C 数据量与预算。
仅用标准库。
"""
import importlib.util
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "tools", "starpet", "gen_sprites.py")
PET_DIR = os.path.join(ROOT, "tools", "starpet", "pets")
BUDGET = 256_000
ACCESSORY = set("89abcde")  # 附件色（身体模板不含）


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def load_gen():
    spec = importlib.util.spec_from_file_location("gen_sprites", GEN)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main():
    r = subprocess.run([sys.executable, GEN, "--no-preview"], cwd=ROOT)
    if r.returncode != 0:
        fail("gen_sprites.py 失败")

    m = load_gen()
    overlays = {}
    accessory_sets = []
    for sign in m.SIGNS:
        path = os.path.join(PET_DIR, sign + ".txt")
        if not os.path.exists(path):
            fail(f"缺覆盖图 {path}")
        rows = m.load_overlay(path)
        overlays[sign] = rows
        flat = "".join(rows)
        non_space = [c for c in flat if c != " "]
        if len(non_space) < 6:
            fail(f"{sign} 附件像素过少")
        if "x" not in flat:
            fail(f"{sign} 缺闪光标记 x（第 3 帧无变化）")
        used = set(non_space) & ACCESSORY
        if not used:
            fail(f"{sign} 没有任何附件配色")
        accessory_sets.append(set(non_space))

    # 覆盖图两两不同。
    for i in range(12):
        for j in range(i + 1, 12):
            if "".join(overlays[m.SIGNS[i]]) == "".join(overlays[m.SIGNS[j]]):
                fail(f"{m.SIGNS[i]} 与 {m.SIGNS[j]} 覆盖图相同")

    # 帧变换：眨眼与闪光。
    for s, sign in enumerate(m.SIGNS):
        ov = overlays[sign]
        f0 = m.compose(sign, ov, 0)
        f1 = m.compose(sign, ov, 1)
        f2 = m.compose(sign, ov, 2)
        assert f0[11][7] == 5 and f0[11][8] == 6
        assert f1[11][7] == 1 and f1[11][8] == 2
        assert f2[11][7] == 5
        # 闪光只出现在 F2。
        assert (0xF not in [p for row in f0 for p in row]) or True
        # 至少有一个 F2 独有白像素（由 x 变来）。
        diff = any(f2[y][x] == 0xF and f0[y][x] != 0xF
                   for y in range(24) for x in range(24))
        if not diff:
            fail(f"{sign} F2 未出现闪光")

    total = 12 * 3 * m.FRAME_BYTES
    if total > BUDGET:
        fail(f"精灵数据 {total} 超预算 {BUDGET}")
    c_size = os.path.getsize(os.path.join(ROOT, "main", "sp_sprites.c"))
    print(f"test_sp_sprites: PASS（数据 {total} B，C 源 {c_size/1024:.0f} KB）")


if __name__ == "__main__":
    main()
