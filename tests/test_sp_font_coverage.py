#!/usr/bin/env python3
"""中文字体门禁：

1. 生成器自校验（lv_font_conv dump 覆盖 + 缺字负例）；
2. 两档字体 C 文件存在且声明符号正确；
3. 尺寸预算（两份源文件合计 <= 900 KB）。
仅用标准库；需要 node + lv_font_conv（路径可用 LV_FONT_CONV 覆盖）。
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "tools", "starpet", "gen_fonts.py")
FONT_DIR = os.path.join(ROOT, "main", "assets", "fonts")
BUDGET_SRC = 900_000


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    r = subprocess.run([sys.executable, GEN, "--check"], cwd=ROOT)
    if r.returncode != 0:
        fail("gen_fonts.py --check 未通过（先运行 gen_fonts.py 生成）")

    total = 0
    for size in (16, 20):
        c_path = os.path.join(FONT_DIR, f"sp_font_{size}.c")
        h_path = os.path.join(FONT_DIR, f"sp_font_{size}.h")
        for p in (c_path, h_path):
            if not os.path.exists(p):
                fail(f"缺生成文件 {p}")
        c_src = open(c_path, encoding="utf-8").read()
        h_src = open(h_path, encoding="utf-8").read()
        if f"const lv_font_t sp_font_{size} =" not in c_src:
            fail(f"{c_path} 缺字体符号 sp_font_{size}")
        if f"LV_FONT_DECLARE(sp_font_{size})" not in h_src:
            fail(f"{h_path} 缺声明 sp_font_{size}")
        total += os.path.getsize(c_path)

    if total > BUDGET_SRC:
        fail(f"字体源文件 {total} 字节超预算 {BUDGET_SRC}")

    print(f"test_sp_font_coverage: PASS（源文件 {total/1024:.0f} KB）")


if __name__ == "__main__":
    main()
