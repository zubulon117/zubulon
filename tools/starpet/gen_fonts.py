#!/usr/bin/env python3
"""命定星宠中文字体子集生成器。

字符来源（单一清单原则）：
  1. main/sp_text.c 全部词库字符串；
  2. tools/starpet/voice_inventory.json 朗读文本；
  3. main/*.c 中含 CJK 的字符串字面量（页面固定文案）；
  4. tools/starpet/ui_chars.txt 显式补充字符（标点/符号）；
  5. ASCII 0x20-0x7E 与常用全角标点。

输出（LVGL9 fmt_txt，bpp4）：
  main/assets/fonts/sp_font_16.c/.h
  main/assets/fonts/sp_font_20.c/.h
用法：
  python gen_fonts.py            生成
  python gen_fonts.py --check    用 dump 校验覆盖（含缺字负例）
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FONT_SRC = os.path.join(REPO, "assets", "fonts", "NotoSansSC-Regular.woff")
OUT_DIR = os.path.join(REPO, "main", "assets", "fonts")
UI_CHARS = os.path.join(HERE, "ui_chars.txt")
CHARS_LIST = os.path.join(HERE, "font_chars.txt")

SIZES = (16, 20)
# 明确不收录的缺字负例（用于覆盖测试）。
NEGATIVE_TEXT = "龘饕餮"
NEGATIVE_CHARS = set(NEGATIVE_TEXT)


def collect_chars():
    chars = set()
    chars.update(chr(c) for c in range(0x20, 0x7F))
    # 常用全角标点。
    # 注：星号/箭头/几何块在 Noto Sans SC 简体子集里缺字，UI 用 LVGL 绘制。
    chars.update("，。！？、：；·…—“”‘’（）《》【】～￥")

    # 1) 词库 C 文件中的字符串。
    text_c = open(os.path.join(REPO, "main", "sp_text.c"), encoding="utf-8").read()
    for s in re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', text_c):
        chars.update(s)

    # 2) 朗读清单。
    inv = json.load(open(os.path.join(REPO, "tools", "starpet",
                                      "voice_inventory.json"), encoding="utf-8"))
    for it in inv:
        chars.update(it["text"])

    # 3) 应用 C 源里的 CJK 字符串字面量。
    for path in sorted(glob.glob(os.path.join(REPO, "main", "*.c"))):
        src = open(path, encoding="utf-8").read()
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
            if re.search(r"[\u4e00-\u9fff]", s):
                chars.update(s)

    # 4) 显式补充（忽略 # 注释行与空白）。
    if os.path.exists(UI_CHARS):
        for line in open(UI_CHARS, encoding="utf-8"):
            line = line.split("#", 1)[0]
            chars.update(line.strip())

    chars.discard("\n")
    chars.discard("\r")
    chars.discard("\t")
    return chars


def write_chars_list(chars):
    line = "".join(sorted(chars))
    with open(CHARS_LIST, "w", encoding="utf-8", newline="\n") as f:
        f.write(line + "\n")


def to_ranges(codepoints):
    pts = sorted(codepoints)
    ranges = []
    start = prev = pts[0]
    for cp in pts[1:]:
        if cp == prev + 1:
            prev = cp
            continue
        ranges.append((start, prev))
        start = prev = cp
    ranges.append((start, prev))
    return ranges


def find_lv_font_conv():
    env = os.environ.get("LV_FONT_CONV")
    if env:
        return env
    win = r"C:\Users\13600\.local\lvfont\node_modules\.bin\lv_font_conv.cmd"
    if os.name == "nt" and os.path.exists(win):
        return win
    return "lv_font_conv"  # 假定 PATH（Linux CI 用 npx 包装亦可）


def run_conv_raw(conv, size, ranges, out_path=None, dump=False):
    cmd = [conv, "--font", FONT_SRC, "--size", str(size),
           "--bpp", "4", "--format", "dump" if dump else "lvgl"]
    if not dump:
        # 生成的 .c 用 #include "lvgl.h"（组件 include 路径），
        # 而非默认的 lvgl/lvgl.h。
        # 保持默认压缩输出（bitmap_format=1）；固件侧必须同步启用
        # CONFIG_LV_USE_FONT_COMPRESSED（见 sdkconfig.defaults），
        # 否则字形无法解码、屏上渲染为空白。
        cmd += ["--lv-include", "lvgl.h"]
    for lo, hi in ranges:
        cmd += ["-r", f"0x{lo:X}-0x{hi:X}" if lo != hi else f"0x{lo:X}"]
    tmp = None
    if dump:
        # dump writer 把 -o 当输出目录，内含 font_info.json 与每字形 PNG。
        tmp = os.path.join(HERE, ".fontdump")
        os.makedirs(tmp, exist_ok=True)
        out_path = tmp
    cmd += ["-o", out_path]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if dump and r.returncode == 0:
        with open(os.path.join(tmp, "font_info.json"), encoding="utf-8") as f:
            r.stdout = f.read()
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    return r


_MISSING_RE = re.compile(r"range (0x[0-9A-Fa-f]+)(?:-(0x[0-9A-Fa-f]+))?")


def resolve_ranges(conv, ranges):
    """剔除字体里不存在的码点（lv_font_conv 遇到缺字即报错）。

    返回 (可用区间列表, 缺失码点列表)；多码点区间缺失时拆成单点重试。
    """
    missing = set()
    ranges = list(ranges)
    for _ in range(2000):
        r = run_conv_raw(conv, 16, ranges, dump=True)
        if r.returncode == 0:
            return ranges, sorted(missing)
        m = _MISSING_RE.search(r.stderr)
        if not m:
            raise SystemExit(f"lv_font_conv 失败: {r.stderr[:400]}")
        lo = int(m.group(1), 16)
        hi = int(m.group(2), 16) if m.group(2) else lo
        target = (lo, hi)
        if target not in ranges:
            raise SystemExit(f"无法定位缺失区间 {target}")
        ranges.remove(target)
        if lo == hi:
            missing.add(lo)
        else:
            # 拆成单点逐个试（缺失通常只是个别符号）。
            ranges.extend((cp, cp) for cp in range(lo, hi + 1))
    raise SystemExit("resolve_ranges 迭代超限")


def finalize_ranges(conv, chars):
    """resolve 处理“整段缺字”，这里再用 dump 消除“部分命中”区间的缺字。"""
    wanted = {ord(c) for c in chars}
    ranges, unsupported = resolve_ranges(conv, to_ranges(sorted(wanted)))
    unsupported = set(unsupported)
    for _ in range(50):
        r = run_conv_raw(conv, 16, ranges, dump=True)
        if r.returncode != 0:
            raise SystemExit(f"dump 校验失败: {r.stderr[:400]}")
        import json as _json
        got = {g["code"] for g in _json.loads(r.stdout).get("glyphs", [])
               if "code" in g}
        absent = wanted - got - unsupported
        if not absent:
            return ranges, unsupported
        # 从区间里摘掉缺失单点后重建区间。
        kept = set()
        for lo, hi in ranges:
            kept.update(range(lo, hi + 1))
        kept -= absent
        unsupported |= absent
        ranges = to_ranges(sorted(kept))
    raise SystemExit("finalize_ranges 迭代超限")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    chars = collect_chars()
    conv = find_lv_font_conv()

    if not os.path.exists(FONT_SRC):
        raise SystemExit(f"缺字体源 {FONT_SRC}")

    ranges, unsupported = finalize_ranges(conv, chars)
    if unsupported:
        shown = " ".join(f"U+{cp:04X}({chr(cp)})" for cp in sorted(unsupported)
                         if chr(cp).isprintable())
        print(f"警告：字体不支持 {len(unsupported)} 个码点，已剔除：{shown}")
    covered_chars = {c for c in chars if ord(c) not in unsupported}
    write_chars_list(covered_chars)

    if args.check:
        r = run_conv_raw(conv, 16, ranges, dump=True)
        import json as _json
        got = {g["code"] for g in _json.loads(r.stdout).get("glyphs", [])
               if "code" in g}
        missing = [c for c in covered_chars if ord(c) not in got]
        if missing:
            raise SystemExit("缺字：" + "".join(missing))
        leaked = [c for c in NEGATIVE_CHARS if ord(c) in got]
        if leaked:
            raise SystemExit("负例字符不应被收录：" + "".join(leaked))
        print(f"font coverage OK：{len(covered_chars)} 字（16/20px 同源子集）")
        return

    os.makedirs(OUT_DIR, exist_ok=True)
    for size in SIZES:
        c_path = os.path.join(OUT_DIR, f"sp_font_{size}.c")
        h_path = os.path.join(OUT_DIR, f"sp_font_{size}.h")
        r = run_conv_raw(conv, size, ranges, out_path=c_path)
        if r.returncode != 0:
            raise SystemExit(f"lv_font_conv 生成失败: {r.stderr[:400]}")
        with open(h_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(
                "// 自动生成：tools/starpet/gen_fonts.py，请勿手改。\n"
                "#pragma once\n#include \"lvgl.h\"\n"
                f"LV_FONT_DECLARE(sp_font_{size});\n")
        print(f"wrote {os.path.relpath(c_path, REPO)}")


if __name__ == "__main__":
    main()
