// tests/test_sp_adpcm.c —— IMA-ADPCM 解码器主机测试。
// 向量由 tools/starpet/adpcm_codec.py vectors 生成（跨 C/Python 两实现互证）。
#define _USE_MATH_DEFINES
#include "sp_adpcm.h"
#include "adpcm_vectors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MinGW/MSVC 经 _USE_MATH_DEFINES 提供 M_PI；glibc 严格 -std=c11 不暴露。 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static void test_known_vector(void)
{
    sp_adpcm_state_t st = { 0, 0 };
    const uint8_t *seq = vec_nibble_data;
    int idx = 0;
    for (size_t i = 0; i < VEC_NIBBLE_BYTES; ++i) {
        int16_t a = sp_adpcm_decode_nibble(seq[i] & 0x0F, &st);
        int16_t b = sp_adpcm_decode_nibble(seq[i] >> 4, &st);
        CHECK(a == vec_expected[idx++]);
        CHECK(b == vec_expected[idx++]);
    }
    CHECK(idx == VEC_SAMPLES);
}

static void test_silence(void)
{
    // 全 0 nibble：预测器迅速归零且不再产生大幅值（无直流漂移）。
    uint8_t zeros[64];
    memset(zeros, 0, sizeof(zeros));
    int16_t pcm[128];
    size_t n = sp_adpcm_decode(zeros, sizeof(zeros), pcm, 128);
    CHECK(n == 128);
    for (int i = 8; i < 128; ++i) {
        CHECK(abs(pcm[i]) <= 2);
    }
}

static void test_sine_snr(void)
{
    // 1) C 解码与 Python 参考重建逐样本一致。
    int16_t pcm[SINE_SAMPLES];
    size_t n = sp_adpcm_decode(sine_adpcm, SINE_BYTES, pcm, SINE_SAMPLES);
    CHECK(n == (size_t)SINE_SAMPLES);
    for (int i = 0; i < SINE_SAMPLES; ++i) {
        CHECK(pcm[i] == sine_ref[i]);
    }

    // 2) 与理想正弦比较 SNR，仅统计信号能量充足的区间（避开首尾静音）。
    double noise = 0.0, signal = 0.0;
    int counted = 0;
    // 跳过淡入与 IMA 冷启动爬升段（前 48 采样），只度量稳态质量。
    for (int i = 48; i < SINE_SAMPLES - 8; ++i) {
        double ideal = 12000.0 * sin(2.0 * M_PI * 100.0 * i / 16000.0);
        if (fabs(ideal) < 1000.0) continue;
        double e = (double)sine_ref[i] - ideal;
        noise += e * e;
        signal += ideal * ideal;
        counted++;
    }
    CHECK(counted > 100);
    double snr_db = 10.0 * log10(signal / noise);
    printf("  sine adpcm SNR = %.1f dB (n=%d)\n", snr_db, counted);
    CHECK(snr_db >= 30.0);
}

static void test_file_format(void)
{
    // 手工拼一个合法 .adpcm 镜像。
    static uint8_t file[SP_ADPCM_HEADER_SIZE + SINE_BYTES];
    file[0] = SINE_SAMPLES & 0xFF;
    file[1] = (SINE_SAMPLES >> 8) & 0xFF;
    file[2] = (SINE_SAMPLES >> 16) & 0xFF;
    file[3] = (SINE_SAMPLES >> 24) & 0xFF;
    file[4] = SINE_BYTES & 0xFF;
    file[5] = (SINE_BYTES >> 8) & 0xFF;
    file[6] = (SINE_BYTES >> 16) & 0xFF;
    file[7] = (SINE_BYTES >> 24) & 0xFF;
    memcpy(file + SP_ADPCM_HEADER_SIZE, sine_adpcm, SINE_BYTES);

    CHECK(sp_adpcm_file_samples(file, sizeof(file)) == SINE_SAMPLES);
    int16_t pcm[SINE_SAMPLES];
    CHECK(sp_adpcm_decode_file(file, sizeof(file), pcm, SINE_SAMPLES)
          == (size_t)SINE_SAMPLES);
    CHECK(pcm[SINE_SAMPLES - 1] == sine_ref[SINE_SAMPLES - 1]);

    // 负例：长度不符、容量不足、空指针。
    CHECK(sp_adpcm_file_samples(file, sizeof(file) - 1) == 0);
    int16_t tiny[4];
    CHECK(sp_adpcm_decode_file(file, sizeof(file), tiny, 4) == 0);
    CHECK(sp_adpcm_file_samples(NULL, 0) == 0);
}

int main(void)
{
    test_known_vector();
    test_silence();
    test_sine_snr();
    test_file_format();
    if (s_failures == 0) {
        printf("test_sp_adpcm: ALL PASS\n");
        return 0;
    }
    printf("test_sp_adpcm: %d FAILURE(S)\n", s_failures);
    return 1;
}
