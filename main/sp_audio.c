#include "sp_audio.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sp_adpcm.h"
#include "sp_store.h"
#include "sp_voice_manifest.h"

static const char *TAG = "sp_audio";

#define SAMPLE_RATE   16000u
#define CHUNK_SAMPLES 256u
#define QUEUE_DEPTH   6u

// 0 静音；1..3 映射到 codec 百分比。
static const uint8_t LEVEL_PCT[4] = { 0, 35, 55, 75 };

typedef enum { REQ_EARCON, REQ_VOICE } req_kind_t;

typedef struct {
    req_kind_t kind;
    uint32_t gen;
    union {
        uint8_t ear;
        struct {
            uint8_t count;
            spv_id_t ids[SP_AUDIO_VOICE_MAX];
        } voice;
    };
} audio_req_t;

static QueueHandle_t s_queue;
static volatile uint8_t s_level = 1;
static volatile uint32_t s_gen;
static volatile bool s_playing;
static bool s_codec_suspended;

static uint32_t s_noise = 0xC001D00Du;

static int16_t wave_sample(uint32_t phase, uint8_t wave, int16_t amp)
{
    uint16_t p = (uint16_t)(phase >> 16);
    if (wave == 0) {
        return p < 32768u ? amp : (int16_t)-amp;
    }
    if (wave == 1) {
        int32_t tri = p < 32768 ? (int32_t)p * 2 - 32768 :
                      98303 - (int32_t)p * 2;
        return (int16_t)(tri * amp / 32768);
    }
    s_noise ^= s_noise << 13;
    s_noise ^= s_noise >> 17;
    s_noise ^= s_noise << 5;
    return (int16_t)(((int32_t)(s_noise & 0xFFFFu) - 32768) * amp / 32768);
}

static void tone(uint16_t hz, uint16_t ms, uint8_t wave, uint8_t level)
{
    if (hz == 0 || ms == 0 || level == 0) {
        return;
    }
    uint32_t total = SAMPLE_RATE * ms / 1000u;
    uint32_t step = (uint32_t)(((uint64_t)hz << 32) / SAMPLE_RATE);
    uint32_t phase = 0;
    int16_t buf[CHUNK_SAMPLES];
    uint32_t done = 0;
    while (done < total) {
        uint32_t n = total - done < CHUNK_SAMPLES ? total - done : CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t pos = done + i;
            uint32_t edge = total / 8u + 1u;
            uint32_t env = pos < edge ? pos * 100u / edge :
                           total - pos < edge ? (total - pos) * 100u / edge : 100u;
            int16_t amp = (int16_t)((600 + level * 700) * env / 100u);
            buf[i] = wave_sample(phase, wave, amp);
            phase += step;
        }
        bsp_audio_write(buf, n * sizeof(int16_t));
        done += n;
    }
}

static void rest_ms(uint16_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void play_earcon(uint8_t id, uint8_t level)
{
    // 星座无关的固定音效，根音取五声音阶里的几个常用音。
    switch ((sp_earcon_t)id) {
    case SP_EAR_CLICK:
        tone(659, 30, 1, level);
        break;
    case SP_EAR_SELECT:
        tone(659, 45, 1, level);
        tone(880, 60, 1, level);
        break;
    case SP_EAR_FEED:
        tone(523, 45, 2, level);
        rest_ms(20);
        tone(784, 80, 1, level);
        break;
    case SP_EAR_PET:
        tone(880, 50, 1, level);
        tone(988, 70, 1, level);
        break;
    case SP_EAR_CHECKIN:
        tone(988, 50, 0, level);
        rest_ms(25);
        tone(1319, 110, 0, level);
        break;
    case SP_EAR_GROW:
        tone(523, 70, 1, level);
        tone(659, 70, 1, level);
        tone(784, 70, 1, level);
        tone(1047, 140, 1, level);
        break;
    case SP_EAR_SUCCESS:
        tone(659, 60, 1, level);
        tone(988, 120, 1, level);
        break;
    case SP_EAR_FAIL:
        tone(392, 100, 0, level);
        tone(294, 160, 0, level);
        break;
    case SP_EAR_OPEN:
        tone(784, 50, 1, level);
        tone(988, 50, 1, level);
        tone(1175, 50, 1, level);
        tone(1568, 140, 1, level);
        break;
    default:
        break;
    }
}

// 流式解码并播放一个 ADPCM 素材文件镜像。每写一块检查 generation。
static void play_clip(const uint8_t *file, size_t file_len, uint32_t gen)
{
    if (file_len < SP_ADPCM_HEADER_SIZE) {
        return;
    }
    uint32_t samples;
    uint32_t data_bytes;
    memcpy(&samples, file, 4);
    memcpy(&data_bytes, file + 4, 4);
    if (data_bytes == 0 || 8u + data_bytes > file_len || samples == 0) {
        return;
    }
    sp_adpcm_state_t st = {0};
    const uint8_t *p = file + SP_ADPCM_HEADER_SIZE;
    static int16_t pcm[CHUNK_SAMPLES];
    uint32_t produced = 0;
    size_t used = 0;
    while (produced < samples && used < data_bytes) {
        uint32_t n = 0;
        while (n < CHUNK_SAMPLES && used < data_bytes && produced < samples) {
            uint8_t byte = p[used++];
            if (produced < samples) {
                pcm[n++] = sp_adpcm_decode_nibble(byte & 0xF, &st);
                produced++;
            }
            if (n < CHUNK_SAMPLES && produced < samples && used < data_bytes) {
                pcm[n++] = sp_adpcm_decode_nibble(byte >> 4, &st);
                produced++;
            }
        }
        bsp_audio_write(pcm, n * sizeof(int16_t));
        if (s_gen != gen) {
            return;  // 被新请求或 stop 打断
        }
    }
}

static void worker(void *arg)
{
    (void)arg;
    audio_req_t req;
    for (;;) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        uint8_t level = s_level;
        if (level == 0) {
            continue;  // 静音：直接丢弃请求
        }
        if (s_codec_suspended) {
            continue;
        }
        if (req.gen != s_gen) {
            continue;  // 排队期间已被取代
        }
        s_playing = true;
        if (req.kind == REQ_EARCON) {
            play_earcon(req.ear, level);
        } else {
            for (uint8_t i = 0; i < req.voice.count; i++) {
                if (req.gen != s_gen) {
                    break;
                }
                const sp_clip_t *clip = sp_voice_clip(req.voice.ids[i]);
                if (clip && clip->data && clip->bytes) {
                    play_clip(clip->data, clip->bytes, req.gen);
                }
            }
        }
        s_playing = false;
    }
}

bool sp_audio_start(uint8_t level)
{
    s_level = level < SP_VOL_LEVELS ? level : 1;
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "codec init failed (running silent)");
        // 即便无 codec 也建 worker，后续 wake/重试不崩。
    }
    bsp_audio_set_format(SAMPLE_RATE, 16, 1);
    bsp_audio_set_volume(LEVEL_PCT[s_level]);

    if (s_queue) {
        return true;
    }
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(audio_req_t));
    if (!s_queue) {
        return false;
    }
    if (xTaskCreate(worker, "sp_audio", 3072, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    return true;
}

void sp_audio_set_level(uint8_t level)
{
    if (level >= SP_VOL_LEVELS) {
        return;
    }
    s_level = level;
    bsp_audio_set_volume(LEVEL_PCT[level]);
}

uint8_t sp_audio_level(void)
{
    return s_level;
}

static void enqueue(audio_req_t *req)
{
    if (!s_queue) {
        return;
    }
    req->gen = ++s_gen;  // 任何新请求都打断上一个
    xQueueReset(s_queue);
    xQueueSend(s_queue, req, 0);
}

void sp_audio_earcon(sp_earcon_t ear)
{
    if (ear >= SP_EAR_COUNT || s_level == 0) {
        return;
    }
    audio_req_t req = {0};
    req.kind = REQ_EARCON;
    req.ear = (uint8_t)ear;
    enqueue(&req);
}

void sp_audio_voice(const spv_id_t *ids, size_t count)
{
    if (!ids || count == 0 || s_level == 0) {
        return;
    }
    audio_req_t req = {0};
    req.kind = REQ_VOICE;
    if (count > SP_AUDIO_VOICE_MAX) {
        count = SP_AUDIO_VOICE_MAX;
    }
    req.voice.count = (uint8_t)count;
    memcpy(req.voice.ids, ids, count * sizeof(spv_id_t));
    enqueue(&req);
}

void sp_audio_stop(void)
{
    s_gen++;
    if (s_queue) {
        xQueueReset(s_queue);
    }
}

void sp_audio_sleep(void)
{
    sp_audio_stop();
    // 等待 worker 退出阻塞写（一块 256 采样约 16ms，给 200ms 余量）。
    for (int i = 0; i < 20 && s_playing; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bsp_audio_sleep();
    s_codec_suspended = true;
}

void sp_audio_wake(void)
{
    bsp_audio_wake();
    bsp_audio_set_format(SAMPLE_RATE, 16, 1);
    bsp_audio_set_volume(LEVEL_PCT[s_level]);
    s_codec_suspended = false;
}
