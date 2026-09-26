#!/usr/bin/env python3
"""语音素材四方一致性门禁：
枚举(sp_voice_script.h) ↔ inventory JSON ↔ manifest C 符号 ↔ .adpcm 文件。
仅用标准库，供 tools/validate.sh 与 Windows 等价门禁调用。
"""
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENUM = os.path.join(ROOT, "main", "sp_voice_script.h")
INV = os.path.join(ROOT, "tools", "starpet", "voice_inventory.json")
MANIFEST_C = os.path.join(ROOT, "main", "sp_voice_manifest.c")
VOICE_DIR = os.path.join(ROOT, "main", "assets", "voice")
BUDGET = 1_200_000


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    text = open(ENUM, encoding="utf-8").read()
    body = text[text.index("typedef enum {"):text.index("} spv_id_t;")]
    enums = []
    for name in re.findall(r"SPV_[A-Z0-9_]+", body):
        if name != "SPV_COUNT" and name not in enums:
            enums.append(name)

    items = json.load(open(INV, encoding="utf-8"))
    if [i["enum"] for i in items] != enums:
        fail("inventory 顺序/内容与枚举不一致")

    manifest = open(MANIFEST_C, encoding="utf-8").read()
    total = 0
    for i, it in enumerate(items):
        stem = it["file"]
        for suffix in ("start", "end"):
            # IDF EMBED_FILES 的链接符号只取文件基名。
            sym = f"_binary_{stem}_adpcm_{suffix}"
            if sym not in manifest:
                fail(f"manifest 缺符号 {sym}")

        path = os.path.join(VOICE_DIR, stem + ".adpcm")
        if not os.path.exists(path):
            fail(f"缺素材文件 {path}")
        blob = open(path, "rb").read()
        if len(blob) < 8:
            fail(f"素材过短 {path}")
        samples, nbytes = struct.unpack("<II", blob[:8])
        if 8 + nbytes != len(blob):
            fail(f"素材长度字段不符 {path}")
        if samples < 400 or samples > 80000:  # 25ms..5s 合理片段
            fail(f"片段时长异常 {path}: {samples} 采样")
        if nbytes != (samples + 1) // 2:
            fail(f"nibble 打包长度不符 {path}")
        total += len(blob)

    if total > BUDGET:
        fail(f"素材总量 {total} 超预算 {BUDGET}")

    print(f"test_voice_inventory: PASS（{len(items)} 条，{total/1024:.0f} KB）")


if __name__ == "__main__":
    main()
