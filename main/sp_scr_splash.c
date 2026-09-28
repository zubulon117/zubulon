// main/sp_scr_splash.c —— 启动闪屏：英文 LOGO 游戏式入场动画 + 启动路由。
//
// 入场时间线（总时长约 2.8s，任意键可跳过）：
//   0.00-0.50s  五连星从上方坠落淡入（带回弹）
//   0.35s 起    "FATED" 逐字弹入（85ms 间隔，下落+缩放回弹，ease-out-back）
//   0.85s 起    "STAR PET" 逐字弹入
//   1.70-2.00s  金色分割线从中线展开
//   2.00s 起    副标题淡入，随后星光呼吸
#include "sp_scr_splash.h"

#include "esp_log.h"
#include "lvgl.h"

#include "sp_clock.h"
#include "sp_page_home.h"
#include "sp_page_wizard.h"
#include "sp_state.h"
#include "sp_ui.h"

#define TAG "sp_splash"
#define SPLASH_HOLD_MS 2800

#define FONT_LOGO (&lv_font_montserrat_28)
#define FONT_SUB  (&lv_font_montserrat_14)

// 字母动画。
typedef struct {
    lv_obj_t *obj;
    uint16_t delay;   // 该字母弹入起始 ms
} letter_t;

typedef struct {
    uint32_t elapsed_ms;
    bool leaving;
    lv_obj_t *stars;
    lv_obj_t *line;       // 金色分割线
    lv_obj_t *sub;        // 副标题
    letter_t letters[16]; // FATED(5) + STAR PET(8)
    uint8_t letter_count;
} splash_ctx_t;

static splash_ctx_t s_ctx;

static void route_next(void)
{
    // 完成后：无档 → 孵化引导；有档但时钟掉电 → 先校时；否则主页。
    sp_wizard_set_next(sp_page_home());
    if (!sp_state_has_pet()) {
        sp_app_goto(sp_page_hatch());
    } else if (!sp_clock_valid()) {
        sp_app_goto(sp_page_datetime());
    } else {
        sp_app_goto(sp_page_home());
    }
}

// ease-out cubic：开始快、收尾稳，用于淡入/展开等不过冲的动画。
static int32_t ease_out_cubic(uint32_t t, uint32_t dur,
                              int32_t from, int32_t to)
{
    if (t >= dur) {
        return to;
    }
    int32_t span = to - from;
    float p = (float)t / (float)dur;
    p = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    return from + (int32_t)(span * p);
}

// ease-out back：越过终点约 10% 再回弹，街机 LOGO "砸进位置" 的手感。
// 返回进度（0 -> ~1.10 -> 1）。
static float ease_out_back(uint32_t t, uint32_t dur)
{
    if (t >= dur) {
        return 1.0f;
    }
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float x = (float)t / (float)dur - 1.0f;
    return 1.0f + c3 * x * x * x + c1 * x * x;
}

// 创建一行逐字弹入的单词（text 中的空格渲染为等宽间隔，不参与弹入）。
static void make_word(lv_obj_t *parent, const char *text, int32_t y,
                      uint16_t delay, uint16_t step)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 1, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, y);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (const char *p = text; *p; p++) {
        lv_obj_t *l = lv_label_create(row);
        char buf[2] = { *p == ' ' ? ' ' : *p, 0 };
        lv_label_set_text(l, buf);
        lv_obj_set_style_text_font(l, FONT_LOGO, 0);
        lv_obj_set_style_text_color(l, SP_C_GOLD, 0);
        if (*p == ' ') {
            continue;  // 空格间隔，不动画。
        }
        // 注意：不可用 transform_scale 做弹入——缩放会让 LVGL 把每个字母
        // 渲染成临时图层，13 个字母同时缩放会耗尽 24KB LVGL 内存池。
        // 回弹只用 Y 位移 + 透明度，零额外显存。
        if (s_ctx.letter_count <
            (uint8_t)(sizeof(s_ctx.letters) / sizeof(s_ctx.letters[0]))) {
            uint8_t i = s_ctx.letter_count++;
            s_ctx.letters[i].obj = l;
            s_ctx.letters[i].delay = delay;
        }
        delay += step;
    }
}

static void splash_tick(sp_page_t *page, uint32_t ms)
{
    (void)page;
    s_ctx.elapsed_ms += ms;
    uint32_t t = s_ctx.elapsed_ms;

    if (!s_ctx.leaving && t >= SPLASH_HOLD_MS) {
        s_ctx.leaving = true;
        route_next();
        return;
    }

    // 五连星坠落淡入（0-500ms，带回弹），之后轻微呼吸闪烁。
    if (s_ctx.stars) {
        if (t < 500) {
            float p = ease_out_back(t, 500);
            lv_obj_set_style_translate_y(s_ctx.stars,
                                         (int32_t)(-24.0f * (1.0f - p)), 0);
            int32_t opa = ease_out_cubic(t, 500, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_obj_set_style_opa(s_ctx.stars, (lv_opa_t)opa, 0);
        } else {
            uint8_t phase = (uint8_t)(((t - 500) / 350) % 4);
            lv_obj_set_style_opa(s_ctx.stars,
                                 phase == 0 ? LV_OPA_70 : LV_OPA_COVER, 0);
        }
    }

    // 字母逐字弹入（320ms）：20px 下方砸落，越过终点 2px 再回弹（Y 位移），
    // 透明度单独走 cubic，不参与过冲。
    for (uint8_t i = 0; i < s_ctx.letter_count; i++) {
        letter_t *lt = &s_ctx.letters[i];
        if (t < lt->delay) {
            lv_obj_set_style_opa(lt->obj, LV_OPA_TRANSP, 0);
            continue;
        }
        uint32_t lt_t = t - lt->delay;
        float p = ease_out_back(lt_t, 320);
        int32_t dy = (int32_t)(20.0f * (1.0f - p));
        int32_t opa = ease_out_cubic(lt_t, 320, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_obj_set_style_translate_y(lt->obj, dy, 0);
        lv_obj_set_style_opa(lt->obj, (lv_opa_t)opa, 0);
    }

    // 金色分割线 1.70-2.00s 从中线展开到 96px。
    if (s_ctx.line && t >= 1700) {
        int32_t w = ease_out_cubic(t - 1700, 300, 0, 96);
        lv_obj_set_width(s_ctx.line, w);
    }

    // 副标题 2.00-2.30s 淡入。
    if (s_ctx.sub && t >= 2000) {
        int32_t opa = ease_out_cubic(t - 2000, 300, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_obj_set_style_opa(s_ctx.sub, (lv_opa_t)opa, 0);
    }
}

static void splash_key(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    (void)input;
    if (!s_ctx.leaving) {
        s_ctx.leaving = true;
        route_next();
    }
}

static void splash_enter(sp_page_t *page)
{
    s_ctx.elapsed_ms = 0;
    s_ctx.leaving = false;
    s_ctx.letter_count = 0;
    sp_ui_style_screen(page->scr);

    // 启动期字形自检：缺字只记录日志，不阻断启动。
    uint32_t missing = sp_ui_glyph_selfcheck();
    if (missing) {
        ESP_LOGW(TAG, "font glyph selfcheck missing: %lu",
                 (unsigned long)missing);
    }

    // 五连星（初始在上方 -24px、透明，tick 中带回弹坠落入场）。
    s_ctx.stars = sp_ui_star_row_create(page->scr, 5);
    lv_obj_align(s_ctx.stars, LV_ALIGN_CENTER, 0, -72);
    lv_obj_set_style_translate_y(s_ctx.stars, -24, 0);
    lv_obj_set_style_opa(s_ctx.stars, LV_OPA_TRANSP, 0);

    // 英文 LOGO 两行，逐字弹入。
    make_word(page->scr, "FATED", -30, 350, 85);
    make_word(page->scr, "STAR PET", 6, 850, 85);

    // 金色分割线（初始宽度 0，居中展开）。
    s_ctx.line = lv_obj_create(page->scr);
    lv_obj_remove_style_all(s_ctx.line);
    lv_obj_set_size(s_ctx.line, 0, 2);
    lv_obj_align(s_ctx.line, LV_ALIGN_CENTER, 0, 38);
    lv_obj_set_style_bg_color(s_ctx.line, SP_C_GOLD, 0);
    lv_obj_set_style_bg_opa(s_ctx.line, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ctx.line, 1, 0);
    lv_obj_clear_flag(s_ctx.line, LV_OBJ_FLAG_SCROLLABLE);

    // 副标题。
    s_ctx.sub = lv_label_create(page->scr);
    lv_label_set_text(s_ctx.sub, "STARS ARE AWAKENING...");
    lv_obj_set_style_text_font(s_ctx.sub, FONT_SUB, 0);
    lv_obj_set_style_text_color(s_ctx.sub, SP_C_MIST, 0);
    lv_obj_align(s_ctx.sub, LV_ALIGN_CENTER, 0, 58);
    lv_obj_set_style_opa(s_ctx.sub, LV_OPA_TRANSP, 0);
}

static sp_page_t s_splash_page = {
    .name = "splash",
    .enter = splash_enter,
    .exit = NULL,
    .key = splash_key,
    .tick = splash_tick,
};

sp_page_t *sp_page_splash(void)
{
    return &s_splash_page;
}
