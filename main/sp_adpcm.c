// main/sp_adpcm.c —— IMA-ADPCM 标准解码器（与 tools/starpet/adpcm_codec.py 同规范）。
#include "sp_adpcm.h"

static const int32_t s_step_table[89] = {
       7,     8,     9,    10,    11,    12,    13,    14,
      16,    17,    19,    21,    23,    25,    28,    31,
      34,    37,    41,    45,    50,    55,    60,    66,
      73,    80,    88,    97,   107,   118,   130,   143,
     157,   173,   190,   209,   230,   253,   279,   307,
     337,   371,   408,   449,   494,   544,   598,   658,
     724,   796,   876,   963,  1060,  1166,  1282,  1411,
    1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
   15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
   32767
};

static const int32_t s_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

int16_t sp_adpcm_decode_nibble(uint8_t nibble, sp_adpcm_state_t *state)
{
    int32_t step = s_step_table[state->step_index];

    // 累积差分：基础 step/8；量级位 0x4=step、0x2=step/2、0x1=step/4；0x8 为符号。
    int32_t diff = step >> 3;
    if (nibble & 0x4) diff += step;
    if (nibble & 0x2) diff += step >> 1;
    if (nibble & 0x1) diff += step >> 2;
    if (nibble & 0x8) diff = -diff;

    int32_t pred = state->predictor + diff;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    state->predictor = pred;

    int32_t idx = state->step_index + s_index_table[nibble & 0x0F];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    state->step_index = idx;

    return (int16_t)pred;
}

size_t sp_adpcm_decode(const uint8_t *data, size_t data_bytes,
                       int16_t *pcm, size_t cap)
{
    sp_adpcm_state_t state = { 0, 0 };
    size_t n = 0;
    for (size_t i = 0; i < data_bytes && n < cap; ++i) {
        uint8_t byte = data[i];
        if (n < cap) pcm[n++] = sp_adpcm_decode_nibble(byte & 0x0F, &state);
        if (n < cap) pcm[n++] = sp_adpcm_decode_nibble(byte >> 4, &state);
    }
    return n;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t sp_adpcm_file_samples(const uint8_t *file, size_t file_len)
{
    if (file == NULL || file_len < SP_ADPCM_HEADER_SIZE) return 0;
    uint32_t samples = rd_u32(file);
    uint32_t bytes = rd_u32(file + 4);
    if ((uint64_t)SP_ADPCM_HEADER_SIZE + bytes != file_len) return 0;
    uint32_t packed = bytes * 2;
    if (samples > packed) return 0;
    return samples;
}

size_t sp_adpcm_decode_file(const uint8_t *file, size_t file_len,
                            int16_t *pcm, size_t cap)
{
    uint32_t samples = sp_adpcm_file_samples(file, file_len);
    if (samples == 0 || samples > cap) return 0;
    size_t got = sp_adpcm_decode(file + SP_ADPCM_HEADER_SIZE,
                                 file_len - SP_ADPCM_HEADER_SIZE, pcm, cap);
    return got < samples ? got : samples;
}
