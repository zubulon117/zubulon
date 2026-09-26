// 自动生成：tools/starpet/gen_sprites.py，请勿手改。
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SP_SPRITE_SIZE 24
#define SP_SPRITE_STRIDE 12
#define SP_SPRITE_FRAME_BYTES 288
#define SP_SPRITE_FRAMES 3
#define SP_SIGN_COUNT 12

// I4 调色板（ARGB8888，0 号全透明），UI 启动时灌入 canvas。
extern const uint32_t sp_sprite_palette_argb[16];

// 取某星座某帧的 I4 像素（低 nibble 在前）；越界回退白羊第 0 帧。
const uint8_t *sp_sprite_frame(uint8_t sign, uint8_t frame);

#ifdef __cplusplus
}
#endif
