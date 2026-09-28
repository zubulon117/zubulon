// main/sp_page_wizard.c —— 孵化引导 + 手动日期时间校时（三键表单）。
#include "sp_page_wizard.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "sp_audio.h"
#include "sp_clock.h"
#include "sp_model.h"
#include "sp_sprites.h"
#include "sp_state.h"
#include "sp_text.h"
#include "sp_ui.h"

enum {
    STEP_INTRO = 0,
    STEP_DATE,
    STEP_TIME,
    STEP_BIRTH,
    STEP_REVEAL,
};

typedef struct {
    bool hatch_mode;          // true=孵化引导；false=仅校时
    sp_page_t *page;
    uint8_t step;
    bool clock_was_valid;
    bool name_shown;

    sp_ui_form_t form;
    sp_ui_field_t fields[4];
    sp_date_t set_date;
    uint16_t set_minute;
    sp_date_t birthday;
    uint8_t reveal_sign;

    lv_obj_t *hint;
    lv_obj_t *title;
    lv_timer_t *anim;
    lv_obj_t *pet_cv;
    uint32_t anim_start_ms;
} wiz_ctx_t;

static sp_page_t s_hatch_page;
static sp_page_t s_time_page;
static wiz_ctx_t s_ctx;
static sp_page_t *s_next_after;   // 完成/取消后的去向

void sp_wizard_set_next(sp_page_t *next)
{
    s_next_after = next;
}

// ------------------------------------------------------------- 回调前置 ---
static void build_step(void);
static void form_done(void *arg);
static void form_back(void *arg);
static void anim_cb(lv_timer_t *t);

// --------------------------------------------------------------- enter ---
static void enter_common(sp_page_t *page, bool hatch)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.hatch_mode = hatch;
    s_ctx.page = page;
    s_ctx.clock_was_valid = sp_clock_valid();
    s_ctx.step = hatch ? STEP_INTRO : STEP_DATE;
    build_step();
}

static void hatch_enter(sp_page_t *page)
{
    enter_common(page, true);
}

static void time_enter(sp_page_t *page)
{
    enter_common(page, false);
}

static void exit_page(sp_page_t *page)
{
    (void)page;
    if (s_ctx.anim) {
        lv_timer_delete(s_ctx.anim);
        s_ctx.anim = NULL;
    }
}

// ----------------------------------------------------------- 表单构建 ---
static void make_date_fields(sp_date_t init, int16_t min_year)
{
    s_ctx.fields[0] = (sp_ui_field_t) { "年", init.year,
                                        min_year, SP_YEAR_MAX, 4 };
    s_ctx.fields[1] = (sp_ui_field_t) { "月", init.month, 1, 12, 2 };
    s_ctx.fields[2] = (sp_ui_field_t) { "日", init.day, 1, 31, 2 };
    sp_ui_form_init(&s_ctx.form, s_ctx.page->scr, s_ctx.fields, 3,
                    form_done, form_back, NULL);
}

static void make_time_fields(uint16_t init_min)
{
    s_ctx.fields[0] = (sp_ui_field_t) { "时", init_min / 60, 0, 23, 2 };
    s_ctx.fields[1] = (sp_ui_field_t) { "分", init_min % 60, 0, 59, 2 };
    sp_ui_form_init(&s_ctx.form, s_ctx.page->scr, s_ctx.fields, 2,
                    form_done, form_back, NULL);
}

static lv_obj_t *add_title(void)
{
    lv_obj_t *l = lv_label_create(s_ctx.page->scr);
    lv_obj_set_style_text_font(l, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(l, SP_C_GOLD, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 36);
    return l;
}

static lv_obj_t *add_hint(void)
{
    lv_obj_t *l = lv_label_create(s_ctx.page->scr);
    lv_obj_set_style_text_font(l, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, SP_C_DIM, 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -14);
    return l;
}

static void build_step(void)
{
    lv_obj_t *scr = s_ctx.page->scr;
    sp_ui_style_screen(scr);
    lv_obj_clean(scr);
    s_ctx.title = NULL;
    s_ctx.hint = NULL;
    s_ctx.pet_cv = NULL;
    s_ctx.name_shown = false;
    if (s_ctx.anim) {
        lv_timer_delete(s_ctx.anim);
        s_ctx.anim = NULL;
    }

    s_ctx.title = add_title();
    s_ctx.hint = add_hint();

    switch (s_ctx.step) {
    case STEP_INTRO: {
        lv_label_set_text(s_ctx.title, "命定星宠");
        lv_obj_t *body = lv_label_create(scr);
        lv_label_set_text(body,
            "输入生日，孵化专属于你的\n星座星宠。每日离线占卜，\n"
            "陪伴你的每个昼夜。");
        lv_obj_set_style_text_font(body, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(body, SP_C_MIST, 0);
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, -8);
        lv_label_set_text(s_ctx.hint, "按确认开始");
        break;
    }
    case STEP_DATE: {
        sp_date_t init;
        if (sp_clock_today(&init)) {
            s_ctx.set_date = init;
        } else {
            s_ctx.set_date = (sp_date_t) { 2026, 1, 1 };
        }
        lv_label_set_text(s_ctx.title,
                          s_ctx.hatch_mode ? "今天的日期" : "校准日期");
        // "今天"不允许设到 RTC 有效门槛（2024）以前。
        make_date_fields(s_ctx.set_date, SP_CLOCK_MIN_YEAR);
        lv_label_set_text(s_ctx.hint, "上下调数字 确认下一项 长按返回");
        break;
    }
    case STEP_TIME: {
        uint16_t init_min = 720;
        sp_clock_day_minutes(&init_min);
        s_ctx.set_minute = init_min;
        lv_label_set_text(s_ctx.title,
                          s_ctx.hatch_mode ? "现在的时间" : "校准时间");
        make_time_fields(init_min);
        lv_label_set_text(s_ctx.hint, "上下调数字 确认下一项 长按返回");
        break;
    }
    case STEP_BIRTH: {
        s_ctx.birthday = (sp_date_t) { 2000, 1, 1 };
        lv_label_set_text(s_ctx.title, "你的生日");
        make_date_fields(s_ctx.birthday, SP_YEAR_MIN);
        lv_label_set_text(s_ctx.hint, "上下调数字 确认下一项 长按返回");
        break;
    }
    case STEP_REVEAL: {
        s_ctx.reveal_sign = sp_zodiac_sign(s_ctx.birthday.month,
                                           s_ctx.birthday.day);
        lv_label_set_text(s_ctx.title, "星宠孵化");
        lv_obj_t *cv = sp_ui_pet_canvas_create(scr, 3);
        if (cv) {
            lv_obj_align(cv, LV_ALIGN_CENTER, 0, -28);
            s_ctx.pet_cv = cv;
            s_ctx.anim_start_ms = sp_app_millis();
            sp_ui_pet_canvas_show(cv, SP_SIGN_ARIES, 0);
        }
        lv_label_set_text(s_ctx.hint, "按确认结缘");
        s_ctx.anim = lv_timer_create(anim_cb, 120, NULL);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------- 揭幕动画 ---
static void anim_cb(lv_timer_t *t)
{
    (void)t;
    if (s_ctx.step != STEP_REVEAL || !s_ctx.pet_cv) {
        return;
    }
    uint32_t elapsed = sp_app_millis() - s_ctx.anim_start_ms;
    uint8_t sign;
    uint8_t frame;
    if (elapsed < 1800) {
        // 前 1.8s 轮换 12 星座预览。
        sign = (uint8_t)((elapsed / 150) % SP_SIGN_COUNT);
        frame = (uint8_t)((elapsed / 300) % 2);
    } else {
        sign = s_ctx.reveal_sign;
        frame = (uint8_t)((elapsed / 250) % 3);
    }
    sp_ui_pet_canvas_show(s_ctx.pet_cv, sign, frame);
}

static void show_reveal_name(void)
{
    const sp_sign_text_t *st = sp_sign_text(s_ctx.reveal_sign);
    lv_label_set_text_fmt(s_ctx.title, "%s星宠", st ? st->name : "");

    lv_obj_t *info = lv_label_create(s_ctx.page->scr);
    lv_label_set_text_fmt(info, "生肖%s",
                          sp_zodiac_name(
                              sp_chinese_zodiac(s_ctx.birthday.year)));
    lv_obj_set_style_text_font(info, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(info, SP_C_MIST, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 62);
}

// ------------------------------------------------------------- 表单回调 ---
static void commit_form(void)
{
    if (s_ctx.step == STEP_DATE) {
        s_ctx.set_date.year = (int16_t)s_ctx.fields[0].value;
        s_ctx.set_date.month = (uint8_t)s_ctx.fields[1].value;
        s_ctx.set_date.day = (uint8_t)s_ctx.fields[2].value;
        uint8_t dmax = sp_days_in_month(s_ctx.set_date.year,
                                        s_ctx.set_date.month);
        if (s_ctx.set_date.day > dmax) {
            s_ctx.set_date.day = dmax;
        }
    } else if (s_ctx.step == STEP_TIME) {
        s_ctx.set_minute = (uint16_t)(s_ctx.fields[0].value * 60 +
                                      s_ctx.fields[1].value);
    } else if (s_ctx.step == STEP_BIRTH) {
        s_ctx.birthday.year = (int16_t)s_ctx.fields[0].value;
        s_ctx.birthday.month = (uint8_t)s_ctx.fields[1].value;
        s_ctx.birthday.day = (uint8_t)s_ctx.fields[2].value;
        uint8_t dmax = sp_days_in_month(s_ctx.birthday.year,
                                        s_ctx.birthday.month);
        if (s_ctx.birthday.day > dmax) {
            s_ctx.birthday.day = dmax;
        }
    }
}

static void form_done(void *arg)
{
    (void)arg;
    sp_audio_earcon(SP_EAR_SELECT);
    commit_form();

    if (s_ctx.step == STEP_DATE) {
        s_ctx.step = STEP_TIME;
        build_step();
    } else if (s_ctx.step == STEP_TIME) {
        sp_clock_set_datetime(s_ctx.set_date, s_ctx.set_minute);
        if (s_ctx.hatch_mode) {
            s_ctx.step = STEP_BIRTH;
            build_step();
        } else if (s_next_after) {
            sp_app_goto(s_next_after);
        }
    } else if (s_ctx.step == STEP_BIRTH) {
        s_ctx.step = STEP_REVEAL;
        build_step();
    }
}

static void form_back(void *arg)
{
    (void)arg;
    sp_audio_earcon(SP_EAR_CLICK);
    if (s_ctx.step == STEP_DATE) {
        if (s_ctx.hatch_mode) {
            s_ctx.step = STEP_INTRO;
            build_step();
        } else if (s_next_after) {
            sp_app_goto(s_next_after);   // 放弃校时
        }
    } else if (s_ctx.step == STEP_TIME) {
        s_ctx.step = STEP_DATE;
        build_step();
    } else if (s_ctx.step == STEP_BIRTH) {
        s_ctx.step = s_ctx.clock_was_valid ? STEP_INTRO : STEP_TIME;
        build_step();
    }
}

static void finish_reveal(void)
{
    sp_date_t today;
    if (!sp_clock_today(&today)) {
        today = s_ctx.set_date;
    }
    sp_state_create_pet(s_ctx.birthday, today);
    sp_audio_earcon(SP_EAR_GROW);
    if (s_next_after) {
        sp_app_goto(s_next_after);
    }
}

static void key_page(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    if (input->ev == SP_EV_PRESS && input->btn == SP_BTN_OK) {
        sp_audio_earcon(SP_EAR_CLICK);
    }

    switch (s_ctx.step) {
    case STEP_INTRO:
        if (input->ev == SP_EV_CLICK && input->btn == SP_BTN_OK) {
            s_ctx.step = s_ctx.clock_was_valid ? STEP_BIRTH : STEP_DATE;
            build_step();
        }
        break;
    case STEP_REVEAL: {
        if (input->ev != SP_EV_CLICK || input->btn != SP_BTN_OK) {
            break;
        }
        uint32_t elapsed = sp_app_millis() - s_ctx.anim_start_ms;
        if (elapsed < 1800) {
            break;   // 预览未播完
        }
        if (!s_ctx.name_shown) {
            s_ctx.name_shown = true;
            show_reveal_name();
            sp_audio_earcon(SP_EAR_SUCCESS);
        } else {
            finish_reveal();
        }
        break;
    }
    case STEP_DATE:
    case STEP_TIME:
    case STEP_BIRTH:
        sp_ui_form_key(&s_ctx.form, input);
        break;
    default:
        break;
    }
}

static void tick_page(sp_page_t *page, uint32_t ms)
{
    (void)page;
    (void)ms;
}

sp_page_t *sp_page_hatch(void)
{
    s_hatch_page = (sp_page_t) {
        .name = "hatch",
        .enter = hatch_enter,
        .exit = exit_page,
        .key = key_page,
        .tick = tick_page,
    };
    return &s_hatch_page;
}

sp_page_t *sp_page_datetime(void)
{
    s_time_page = (sp_page_t) {
        .name = "datetime",
        .enter = time_enter,
        .exit = exit_page,
        .key = key_page,
        .tick = tick_page,
    };
    return &s_time_page;
}
