// 自动生成：tools/starpet/gen_voice.py，请勿手改。
#pragma once
#include <stdint.h>
#include "sp_voice_script.h"

typedef struct { const uint8_t *data; uint32_t bytes; } sp_clip_t;
const sp_clip_t *sp_voice_clip(spv_id_t id);
