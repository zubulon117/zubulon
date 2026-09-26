/* 精灵 I4 数据主机测试：打包顺序、三帧差异、越界回退、调色板、星座区分。 */
#include "../main/sp_sprites.h"
#include <string.h>
#include <stdio.h>

/* 直接编入生成的数据实现。 */
#include "../main/sp_sprites.c"

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static int pixel(const uint8_t *f, int x, int y)
{
    int byte = f[y * SP_SPRITE_STRIDE + x / 2];
    return (x & 1) ? (byte >> 4) : (byte & 0xF);
}

int main(void)
{
    CHECK(SP_SPRITE_SIZE == 24, "size 24");
    CHECK(SP_SPRITE_STRIDE == 12, "stride 12");
    CHECK(SP_SPRITE_FRAME_BYTES == 288, "frame 288");
    CHECK(SP_SPRITE_FRAMES == 3 && SP_SIGN_COUNT == 12, "3x12");

    /* 调色板：0 号透明，其它有颜色。 */
    CHECK((sp_sprite_palette_argb[0] >> 24) == 0, "palette 0 transparent");
    for (int i = 1; i < 16; i++) {
        CHECK((sp_sprite_palette_argb[i] >> 24) == 0xFF, "opaque palette entry");
    }

    for (int s = 0; s < SP_SIGN_COUNT; s++) {
        const uint8_t *f0 = sp_sprite_frame(s, 0);
        const uint8_t *f1 = sp_sprite_frame(s, 1);
        const uint8_t *f2 = sp_sprite_frame(s, 2);
        CHECK(f0 && f1 && f2, "frames non-null");
        /* 角落全透明。 */
        CHECK(pixel(f0, 0, 0) == 0 && pixel(f0, 23, 0) == 0 &&
              pixel(f0, 0, 23) == 0 && pixel(f0, 23, 23) == 0,
              "transparent corners");
        /* 基础身体眼睛位置：F0 瞳(5)+高光(6)；F1 闭眼 描边(1)+身体(2)。 */
        CHECK(pixel(f0, 7, 11) == 5 && pixel(f0, 8, 11) == 6, "open eye");
        CHECK(pixel(f1, 7, 11) == 1 && pixel(f1, 8, 11) == 2, "blink frame");
        /* 三帧互不相同。 */
        CHECK(memcmp(f0, f1, SP_SPRITE_FRAME_BYTES) != 0, "f0 != f1");
        CHECK(memcmp(f0, f2, SP_SPRITE_FRAME_BYTES) != 0, "f0 != f2");
        CHECK(memcmp(f1, f2, SP_SPRITE_FRAME_BYTES) != 0, "f1 != f2");
        /* 非透明像素占比合理（8%..85%）。 */
        int ink = 0;
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 24; x++)
                if (pixel(f0, x, y)) ink++;
        CHECK(ink > 46 && ink < 490, "ink ratio");
    }

    /* 12 星座 F0 两两不同。 */
    for (int a = 0; a < 12; a++) {
        for (int b = a + 1; b < 12; b++) {
            CHECK(memcmp(sp_sprite_frame(a, 0), sp_sprite_frame(b, 0),
                         SP_SPRITE_FRAME_BYTES) != 0, "signs distinct");
        }
    }

    /* 越界回退白羊第 0 帧（非空、可访问整帧）。 */
    const uint8_t *bad = sp_sprite_frame(99, 99);
    CHECK(bad == sp_sprite_frame(0, 0), "out-of-range fallback");

    if (failures) {
        printf("test_sp_sprites: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("test_sp_sprites: ALL PASS\n");
    return 0;
}
