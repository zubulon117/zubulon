// main/sp_scr_splash.c —— 启动闪屏：星标动画 + 启动路由（引导/校时/主页）。
#include "sp_scr_splash.h"

#include "esp_log.h"
#include "lvgl.h"

#include "sp_clock.h"
#include "sp_page_home.h"
#include "sp_page_wizard.h"
#include "sp_state.h"
#include "sp_ui.h"

#define TAG "sp_splash"
#define SPLASH_HOLD_MS 1500

typedef struct {
    uint32_t elapsed_ms;
    bool leaving;
    lv_obj_t *stars;
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

static void splash_tick(sp_page_t *page, uint32_t ms)
{
    (void)page;
    s_ctx.elapsed_ms += ms;
    if (!s_ctx.leaving && s_ctx.elapsed_ms >= SPLASH_HOLD_MS) {
        s_ctx.leaving = true;
        route_next();
        return;
    }
    // 星标呼吸（每 300ms 在 1/5 星之间轻微变化，保持低成本动画）。
    if (s_ctx.stars) {
        uint8_t phase = (uint8_t)((s_ctx.elapsed_ms / 300) % 4);
        lv_obj_set_style_opa(s_ctx.stars,
                             phase == 0 ? LV_OPA_70 : LV_OPA_COVER, 0);
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
    sp_ui_style_screen(page->scr);

    // 启动期字形自检：缺字只记录日志，不阻断启动。
    uint32_t missing = sp_ui_glyph_selfcheck();
    if (missing) {
        ESP_LOGW(TAG, "font glyph selfcheck missing: %lu",
                 (unsigned long)missing);
    }

    s_ctx.stars = sp_ui_star_row_create(page->scr, 5);
    lv_obj_align(s_ctx.stars, LV_ALIGN_CENTER, 0, -52);

    lv_obj_t *title = lv_label_create(page->scr);
    lv_label_set_text(title, "命定星宠");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

    lv_obj_t *sub = lv_label_create(page->scr);
    lv_label_set_text(sub, "星光正在苏醒…");
    lv_obj_set_style_text_font(sub, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(sub, SP_C_MIST, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 22);
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
