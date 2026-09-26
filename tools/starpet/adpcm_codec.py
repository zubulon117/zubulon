#!/usr/bin/env python3
"""IMA-ADPCM 编解码与 .adpcm 文件格式（与 main/sp_adpcm.c 同规范）。

文件布局（小端）：
  uint32 sample_count, uint32 data_bytes, nibble bytes（低 nibble 在前）

用法：
  python adpcm_codec.py encode input.pcm output.adpcm
  python adpcm_codec.py decode input.adpcm output.pcm
  python adpcm_codec.py vectors tests/vectors/adpcm_vectors.h
"""
import argparse
import struct
import sys

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


class AdpcmState:
    def __init__(self, predictor=0, step_index=0):
        self.predictor = predictor
        self.step_index = step_index


def decode_nibble(nibble, st):
    step = STEP_TABLE[st.step_index]
    diff = step >> 3
    if nibble & 0x4:
        diff += step
    if nibble & 0x2:
        diff += step >> 1
    if nibble & 0x1:
        diff += step >> 2
    if nibble & 0x8:
        diff = -diff
    st.predictor = max(-32768, min(32767, st.predictor + diff))
    st.step_index = max(0, min(88, st.step_index + INDEX_TABLE[nibble & 0x0F]))
    return st.predictor


def encode_sample(sample, st):
    """IMA 标准编码器：顺序减除量化，返回 nibble；再用解码前滚状态保证两端一致。"""
    step = STEP_TABLE[st.step_index]
    delta = sample - st.predictor
    sign = 0x8 if delta < 0 else 0
    delta = abs(delta)

    nib = sign
    remain = step
    if delta >= remain:          # 量级位 0x4，权重 step
        nib |= 0x4
        delta -= remain
    remain >>= 1
    if delta >= remain:          # 0x2，权重 step/2
        nib |= 0x2
        delta -= remain
    remain >>= 1
    if delta >= remain:          # 0x1，权重 step/4
        nib |= 0x1

    decoded = decode_nibble(nib, st)
    return nib, decoded


def encode_pcm(pcm):
    st = AdpcmState()
    out = bytearray()
    nibbles = []
    for s in pcm:
        nib, _ = encode_sample(int(s), st)
        nibbles.append(nib)
    if len(nibbles) % 2:
        nibbles.append(0)
    for i in range(0, len(nibbles), 2):
        out.append(nibbles[i] | (nibbles[i + 1] << 4))
    return bytes(out)


def decode_bytes(data, sample_count=None):
    st = AdpcmState()
    pcm = []
    for b in data:
        pcm.append(decode_nibble(b & 0x0F, st))
        pcm.append(decode_nibble(b >> 4, st))
    if sample_count is not None:
        pcm = pcm[:sample_count]
    return pcm


def write_file(path, pcm):
    data = encode_pcm(pcm)
    with open(path, "wb") as f:
        f.write(struct.pack("<II", len(pcm), len(data)))
        f.write(data)


def read_file(path):
    with open(path, "rb") as f:
        blob = f.read()
    samples, nbytes = struct.unpack("<II", blob[:8])
    pcm = decode_bytes(blob[8:8 + nbytes], samples)
    return pcm, blob[8:8 + nbytes], samples


def emit_vectors(header_path):
    """生成 C 测试头：固定 nibble 序列期望 PCM + 正弦片段 ADPCM。"""
    # 1) 固定 nibble 序列（覆盖正负、index 升降）。
    nibbles = [0x0, 0x7, 0xA, 0x3, 0xF, 0x1, 0x8, 0xC,
               0x5, 0x2, 0x9, 0x6, 0x0, 0xD, 0x4, 0xB]
    data = bytearray()
    for i in range(0, len(nibbles), 2):
        data.append(nibbles[i] | (nibbles[i + 1] << 4))
    expected = decode_bytes(bytes(data))[:len(nibbles)]

    # 2) 正弦：320 个采样、振幅 12000（100Hz @16k，两周期）；头部 32 采样
    #    线性淡入、尾部 8 采样静音——与真实 TTS 片段的淡入淡出包络一致。
    import math
    raw = [12000.0 * math.sin(2 * math.pi * 100 * i / 16000)
           for i in range(320)]
    fade = 32
    for i in range(fade):
        raw[i] *= (i + 1) / fade
    sine = [int(round(v)) for v in raw]
    sine += [0] * 8
    adpcm = encode_pcm(sine)
    rebuilt = decode_bytes(adpcm, len(sine))

    lines = []
    lines.append("// 自动生成：tools/starpet/adpcm_codec.py vectors，请勿手改。")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append(f"#define VEC_NIBBLE_BYTES {len(data)}")
    lines.append(f"#define VEC_SAMPLES {len(expected)}")
    lines.append("static const uint8_t vec_nibble_data[VEC_NIBBLE_BYTES] = {")
    for i in range(0, len(data), 12):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",")
    lines.append("};")
    lines.append("static const int16_t vec_expected[VEC_SAMPLES] = {")
    for i in range(0, len(expected), 10):
        lines.append("  " + ",".join(str(v) for v in expected[i:i + 10]) + ",")
    lines.append("};")
    lines.append(f"#define SINE_SAMPLES {len(sine)}")
    lines.append(f"#define SINE_BYTES {len(adpcm)}")
    lines.append("static const uint8_t sine_adpcm[SINE_BYTES] = {")
    for i in range(0, len(adpcm), 12):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in adpcm[i:i + 12]) + ",")
    lines.append("};")
    lines.append("static const int16_t sine_ref[SINE_SAMPLES] = {")
    for i in range(0, len(rebuilt), 10):
        lines.append("  " + ",".join(str(v) for v in rebuilt[i:i + 10]) + ",")
    lines.append("};")
    with open(header_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {header_path}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("encode")
    e.add_argument("pcm_in")
    e.add_argument("adpcm_out")
    d = sub.add_parser("decode")
    d.add_argument("adpcm_in")
    d.add_argument("pcm_out")
    v = sub.add_parser("vectors")
    v.add_argument("header_out")
    args = ap.parse_args()

    if args.cmd == "vectors":
        emit_vectors(args.header_out)
        return
    if args.cmd == "encode":
        raw = open(args.pcm_in, "rb").read()
        pcm = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
        write_file(args.adpcm_out, pcm)
    else:
        pcm, _, _ = read_file(args.adpcm_in)
        with open(args.pcm_out, "wb") as f:
            f.write(struct.pack("<%dh" % len(pcm), *pcm))


if __name__ == "__main__":
    main()
