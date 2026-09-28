#!/usr/bin/env python3
"""命定星宠离线语音素材生成器。

读取 voice_inventory.json，逐条 TTS（edge-tts 在线 zh-CN-XiaoxiaoNeural；
失败回退本机 SAPI 中文慧慧），解码为 16kHz/16bit/单声道 PCM，
做静音修剪 + 8ms 淡入淡出 + 峰值归一，再 IMA-ADPCM 编码，输出：
  main/assets/voice/<file>.adpcm
  main/sp_voice_manifest.h / sp_voice_manifest.c

--check 只做三方一致性与产物校验，不发起 TTS（供静态门禁调用）。
用法（推荐在 C:\\esp\\asset-tools\\.venv 中运行）：
  python gen_voice.py            # 生成全部
  python gen_voice.py --check    # 校验
  python gen_voice.py --only 000_intro,001_overall
"""
import argparse
import asyncio
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import adpcm_codec  # noqa: E402

INVENTORY = os.path.join(HERE, "voice_inventory.json")
VOICE_DIR = os.path.join(REPO, "main", "assets", "voice")
MANIFEST_H = os.path.join(REPO, "main", "sp_voice_manifest.h")
MANIFEST_C = os.path.join(REPO, "main", "sp_voice_manifest.c")
ENUM_HEADER = os.path.join(REPO, "main", "sp_voice_script.h")

TTS_VOICE = "zh-CN-XiaoxiaoNeural"
SAMPLE_RATE = 16000
FADE_SAMPLES = 128           # 8ms
SILENCE_THRESHOLD = 280      # 振幅阈值
TARGET_PEAK = int(32767 * 0.9)
BUDGET_BYTES = 1_200_000     # spec：语音素材 ≤ 1.2MB


# ----------------------------------------------------------- 清单解析 ----
def load_inventory():
    with open(INVENTORY, encoding="utf-8") as f:
        items = json.load(f)
    return items


def enum_order():
    """从 sp_voice_script.h 按声明顺序提取枚举名（去掉 SPV_COUNT）。"""
    text = open(ENUM_HEADER, encoding="utf-8").read()
    body = text[text.index("typedef enum {"):text.index("} spv_id_t;")]
    names = []
    for raw in re.findall(r"SPV_[A-Z0-9_]+", body):
        if raw not in names:
            names.append(raw)
    names = [n for n in names if n != "SPV_COUNT"]
    return names


def validate_items(items):
    enums = enum_order()
    names = [i["enum"] for i in items]
    if names != enums:
        missing = [e for e in enums if e not in names]
        extra = [n for n in names if n not in enums]
        order_bad = names == enums
        raise SystemExit(
            f"清单与枚举不一致：missing={missing} extra={extra} order={order_bad}")
    files = [i["file"] for i in items]
    if len(set(files)) != len(files):
        raise SystemExit("清单 file 字段存在重复")
    return enums


# ----------------------------------------------------------- TTS+PCM ----
def decode_pcm(path):
    import miniaudio  # 仅在真正生成时依赖
    dec = miniaudio.decode_file(
        path, nchannels=1, sample_rate=SAMPLE_RATE,
        output_format=miniaudio.SampleFormat.SIGNED16)
    # dec.samples 是 array('h')，每个元素即一个 int16 采样。
    return list(dec.samples)


async def edge_tts(text, out_mp3):
    import edge_tts
    communicate = edge_tts.Communicate(text, TTS_VOICE)
    await communicate.save(out_mp3)


def sapi_tts(text, out_wav):
    """离线兜底：Windows SAPI 中文慧慧（若系统安装了该语音）。"""
    # 从已安装语音里挑一个中文（慧慧/晓晓/瑶瑶/任何含 Chinese 的），找不到用默认。
    ps = (
        "Add-Type -AssemblyName System.Speech;"
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
        "$v = $s.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name } |"
        " Where-Object { $_ -match 'Huihui|Xiaoxiao|Yaoyao|Chinese|China|Han' }"
        " | Select-Object -First 1;"
        "if ($v) { $s.SelectVoice($v) };"
        "$f = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo("
        "16000,16,1);"
        f"$s.SetOutputToWaveFile('{out_wav}', $f);"
        f"$s.Speak({json.dumps(text)});"
        "$s.Dispose();"
    )
    r = subprocess.run(
        ["powershell", "-NoProfile", "-NonInteractive", "-Command", ps],
        capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or not os.path.exists(out_wav):
        raise RuntimeError(f"SAPI 失败: {r.stderr.strip()}")


def synth(text):
    """返回 16kHz/16bit/单声道 PCM int 列表。"""
    with tempfile.TemporaryDirectory() as td:
        mp3 = os.path.join(td, "c.mp3")
        # edge-tts 偶发网络抖动，重试一次。
        for attempt in range(2):
            try:
                asyncio.run(edge_tts(text, mp3))
                if os.path.getsize(mp3) > 0:
                    return decode_pcm(mp3)
            except Exception as e:  # noqa: BLE001
                print(f"  edge-tts 尝试 {attempt + 1} 失败：{e}")
        wav = os.path.join(td, "c.wav")
        print("  回退 SAPI 慧慧（离线）")
        sapi_tts(text, wav)
        return decode_pcm(wav)


# ----------------------------------------------------------- 处理链 ----
def trim_and_shape(pcm):
    # 静音修剪。
    lo = 0
    while lo < len(pcm) and abs(pcm[lo]) < SILENCE_THRESHOLD:
        lo += 1
    hi = len(pcm)
    while hi > lo and abs(pcm[hi - 1]) < SILENCE_THRESHOLD:
        hi -= 1
    pcm = pcm[lo:hi] if hi > lo else [0]
    # 首尾补 20ms 轻缓冲，再做 8ms 淡变，避免咔哒声。
    pad = [0] * 320
    pcm = pad + pcm + pad
    n = len(pcm)
    for i in range(min(FADE_SAMPLES, n)):
        g = i / FADE_SAMPLES
        pcm[i] = int(pcm[i] * g)
        pcm[n - 1 - i] = int(pcm[n - 1 - i] * g)
    # 峰值归一。
    peak = max(abs(v) for v in pcm) or 1
    if peak < TARGET_PEAK:
        g = TARGET_PEAK / peak
        pcm = [max(-32768, min(32767, int(v * g))) for v in pcm]
    return pcm


# ----------------------------------------------------------- manifest ----
def symbol(file_stem, suffix):
    # IDF EMBED_FILES 生成的链接符号只取文件基名（无目录前缀）：
    # main/assets/voice/000_intro.adpcm → _binary_000_intro_adpcm_start。
    return f"_binary_{file_stem}_adpcm_{suffix}"


def write_manifest(items):
    lines_h = [
        "// 自动生成：tools/starpet/gen_voice.py，请勿手改。",
        "#pragma once",
        "#include <stdint.h>",
        '#include "sp_voice_script.h"',
        "",
        "typedef struct { const uint8_t *data; uint32_t bytes; } sp_clip_t;",
        "const sp_clip_t *sp_voice_clip(spv_id_t id);",
        "",
    ]
    with open(MANIFEST_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines_h))

    decls = []
    start_rows = []
    end_rows = []
    for i, it in enumerate(items):
        decls.append(
            f"extern const uint8_t {symbol(it['file'], 'start')}[];")
        decls.append(
            f"extern const uint8_t {symbol(it['file'], 'end')}[];")
        start_rows.append(f"    {symbol(it['file'], 'start')},")
        end_rows.append(f"    {symbol(it['file'], 'end')},")
    # 注意：两个外部链接符号的地址差不是 C 的整型常量表达式，不能做
    # 静态初值（RISC-V gcc 报 initializer element is not constant）。
    # 因此 start/end 分两张地址常量表，长度在查询时相减。
    lines_c = [
        "// 自动生成：tools/starpet/gen_voice.py，请勿手改。",
        '#include "sp_voice_manifest.h"',
        "",
        "\n".join(decls),
        "",
        f"static const uint8_t * const s_start[{len(items)}] = {{",
        "\n".join(start_rows),
        "};",
        "",
        f"static const uint8_t * const s_end[{len(items)}] = {{",
        "\n".join(end_rows),
        "};",
        "",
        "static sp_clip_t s_clip;",
        "",
        "const sp_clip_t *sp_voice_clip(spv_id_t id)",
        "{",
        "    if ((unsigned)id >= (unsigned)SPV_COUNT) return 0;",
        "    s_clip.data = s_start[id];",
        "    s_clip.bytes = (uint32_t)(s_end[id] - s_start[id]);",
        "    return &s_clip;",
        "}",
        "",
    ]
    with open(MANIFEST_C, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines_c))


def check_only():
    items = load_inventory()
    validate_items(items)
    problems = []
    total = 0
    for it in items:
        p = os.path.join(VOICE_DIR, it["file"] + ".adpcm")
        if not os.path.exists(p):
            problems.append(f"缺素材 {p}")
            continue
        blob = open(p, "rb").read()
        samples, nbytes = struct.unpack("<II", blob[:8])
        if 8 + nbytes != len(blob):
            problems.append(f"长度不符 {it['file']}")
        total += len(blob)
    if not os.path.exists(MANIFEST_H) or not os.path.exists(MANIFEST_C):
        problems.append("缺 manifest")
    else:
        ctext = open(MANIFEST_C, encoding="utf-8").read()
        for it in items:
            if symbol(it["file"], "start") not in ctext:
                problems.append(f"manifest 缺符号 {it['file']}")
    if total > BUDGET_BYTES:
        problems.append(f"素材总量 {total} > {BUDGET_BYTES}")
    if problems:
        for p in problems:
            print("FAIL:", p)
        raise SystemExit(1)
    print(f"voice check OK：{len(items)} 条，{total/1024:.0f} KB")


# ----------------------------------------------------------- main ----
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    if args.check:
        check_only()
        return

    items = load_inventory()
    validate_items(items)
    os.makedirs(VOICE_DIR, exist_ok=True)
    only = {s.strip() for s in args.only.split(",") if s.strip()}

    total = 0
    for i, it in enumerate(items):
        if only and it["file"] not in only and it["enum"] not in only:
            continue
        out = os.path.join(VOICE_DIR, it["file"] + ".adpcm")
        print(f"[{i + 1:2d}/{len(items)}] {it['enum']:24s} {it['text']}")
        pcm = synth(it["text"])
        pcm = trim_and_shape(pcm)
        data = adpcm_codec.encode_pcm(pcm)
        with open(out, "wb") as f:
            f.write(struct.pack("<II", len(pcm), len(data)))
            f.write(data)
        total += os.path.getsize(out)

    write_manifest(items)
    # 重新统计总量并做预算校验（--only 模式下未生成的文件跳过）。
    existing = [it for it in items
                if os.path.exists(os.path.join(VOICE_DIR, it["file"] + ".adpcm"))]
    total = sum(os.path.getsize(os.path.join(VOICE_DIR, it["file"] + ".adpcm"))
                for it in existing)
    print(f"完成：{len(existing)}/{len(items)} 条，"
          f"总量 {total/1024:.0f} KB（预算 {BUDGET_BYTES/1024:.0f} KB）")
    if len(existing) == len(items) and total > BUDGET_BYTES:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
