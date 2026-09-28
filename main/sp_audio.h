// main/sp_audio.h —— 设备音频：合成提示音 + 离线 ADPCM 语音播报。
//
// 单一 worker 任务独占 bsp_audio（写阻塞只发生在该任务）。
// 每次播报带 generation 号：新请求或 stop() 会打断正在播放的旧请求。
// 音量 4 档（0=静音）。sleep/wake 配合轻睡眠。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sp_voice_script.h"

#define SP_AUDIO_VOICE_MAX 32

typedef enum {
    SP_EAR_CLICK = 0,   // 移动焦点
    SP_EAR_SELECT,      // 确认
    SP_EAR_FEED,        // 喂食
    SP_EAR_PET,         // 抚摸
    SP_EAR_CHECKIN,     // 签到得星币
    SP_EAR_GROW,        // 成长
    SP_EAR_SUCCESS,     // 通用成功
    SP_EAR_FAIL,        // 失败/冷却
    SP_EAR_OPEN,        // 开签
    SP_EAR_COUNT,
} sp_earcon_t;

bool sp_audio_start(uint8_t level);
void sp_audio_set_level(uint8_t level);
uint8_t sp_audio_level(void);

// 排队提示音（队列满则丢弃，绝不阻塞 UI）。
void sp_audio_earcon(sp_earcon_t ear);
// 顺序播报片段；内部复制 id 列表。count 超过 SP_AUDIO_VOICE_MAX 截断。
void sp_audio_voice(const spv_id_t *ids, size_t count);
// 立即打断并清空队列。
void sp_audio_stop(void);

// 轻睡眠配套：打断当前播放并等 worker 静止后挂 codec；wake 恢复格式。
void sp_audio_sleep(void);
void sp_audio_wake(void);
