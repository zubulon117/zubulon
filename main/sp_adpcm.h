// main/sp_adpcm.h —— IMA-ADPCM  nibble 流解码（纯逻辑，主机可测）。
//
// 素材文件格式（小端）：
//   uint32 sample_count  PCM 采样数（16kHz/16bit/单声道）
//   uint32_t data_bytes  nibble 数据字节数
//   uint8_t  data[]      nibble 打包：每字节低 4 bit 为较早采样
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t predictor;
    int32_t step_index;
} sp_adpcm_state_t;

// 解码一个 nibble（0..15），返回新预测值并更新状态。
int16_t sp_adpcm_decode_nibble(uint8_t nibble, sp_adpcm_state_t *state);

// 从初始状态解码整段 nibble 数据到 PCM。每个 nibble 一个采样；
// 最多输出 cap 个采样，返回实际写入数。
size_t sp_adpcm_decode(const uint8_t *data, size_t data_bytes,
                       int16_t *pcm, size_t cap);

// .adpcm 文件头尺寸。
#define SP_ADPCM_HEADER_SIZE 8u
// 从文件镜像解码：校验 8 字节头与容量后解码；返回采样数，0=失败。
size_t sp_adpcm_decode_file(const uint8_t *file, size_t file_len,
                            int16_t *pcm, size_t cap);
uint32_t sp_adpcm_file_samples(const uint8_t *file, size_t file_len);
