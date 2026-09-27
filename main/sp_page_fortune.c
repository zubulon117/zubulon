// main/sp_page_fortune.c —— 每日离线运势：开签、五维星级、幸运信息、语音播报。
#include "sp_page_fortune.h"

#include <stdio.h>

#include "lvgl.h"

#include "sp_audio.h"
#include "sp_clock.h"
#include "sp_model.h"
#include "sp_state.h"
#include "sp_text.h"
#include "sp_ui.h"

#include "sp_page_home.h"

static sp_page_t s_page;
static sp_fortune_t s_fortune;
static bool s_opened;

static lv_obj_t *small_label(lv_obj_t *parent, lv_color_t color,
                             int32_t x, int32_t y, lv_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

// ------------------------------------------------------------- 未开签 ---
static void build_closed(lv_obj_t *scr)
{
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "今日星签");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -46);

    lv_obj_t *body = small_label(scr, SP_C_MIST, 0, 0, LV_ALIGN_CENTER);
    if (sp_clock_valid()) {
        lv_label_set_text(body, "星光已经写好今天的答案\n按确认开签");
    } else {
        lv_label_set_text(body, "时钟未校准，无法开签\n请先在设置中校准日期");
        lv_obj_set_style_text_color(body, SP_C_WARN, 0);
    }
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *hint = small_label(scr, SP_C_DIM, 0, -12,
                                 LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(hint, "长按返回");
}

// --------------------------------------------------------------- 开签 ---
static void build_opened(lv_obj_t *scr, const sp_pet_t *pet)
{
    // 主题标题（顶部，大字体金色）。
    lv_obj_t *theme = lv_label_create(scr);
    lv_label_set_text(theme, sp_theme_title(s_fortune.theme_id));
    lv_obj_set_style_text_font(theme, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(theme, SP_C_GOLD, 0);
    lv_obj_align(theme, LV_ALIGN_TOP_MID, 0, 8);

    int32_t y = 34;

    // 五维星级 + 短评。
    for (int d = 0; d < SP_DIM_COUNT; d++) {
        lv_obj_t *name = small_label(scr, SP_C_MIST, 10, y,
                                     LV_ALIGN_TOP_LEFT);
        lv_label_set_text(name, sp_dim_name((uint8_t)d));
        lv_obj_t *stars = sp_ui_star_row_create(scr, s_fortune.stars[d]);
        lv_obj_align(stars, LV_ALIGN_TOP_LEFT, 58, y - 2);

        // 短评（星级 1-2→qid0，3→qid1，4-5→qid2）。
        uint8_t qid = s_fortune.stars[d] <= 2 ? 0 : (s_fortune.stars[d] == 3 ? 1 : 2);
        lv_obj_t *quote = small_label(scr, SP_C_DIM, 118, y,
                                      LV_ALIGN_TOP_LEFT);
        lv_label_set_text(quote, sp_dim_quote((uint8_t)d, qid));
        lv_obj_set_style_text_font(quote, SP_FONT_SMALL, 0);
        y += 22;
    }

    y += 4;

    // 速配星座。
    const sp_sign_text_t *match = sp_sign_text(s_fortune.match_sign);
    lv_obj_t *match_lbl = small_label(scr, SP_C_MIST, 10, y,
                                      LV_ALIGN_TOP_LEFT);
    lv_label_set_text_fmt(match_lbl, "速配星座 %s", match->name);
    y += 22;

    uint32_t color_hex = sp_color_hex(s_fortune.color_id);
    lv_obj_t *lucky = small_label(scr, SP_C_MIST, 10, y,
                                  LV_ALIGN_TOP_LEFT);
    lv_label_set_text_fmt(lucky, "幸运色 %s  幸运数字 %d",
                          sp_color_name(s_fortune.color_id),
                          s_fortune.lucky_num);
    lv_obj_set_style_text_color(lucky, lv_color_hex(color_hex), 0);
    y += 22;

    lv_obj_t *item = small_label(scr, SP_C_MIST, 10, y,
                                 LV_ALIGN_TOP_LEFT);
    lv_label_set_text_fmt(item, "幸运物 %s", sp_item_name(s_fortune.item_id));
    y += 22;

    lv_obj_t *yi = small_label(scr, SP_C_OK, 10, y, LV_ALIGN_TOP_LEFT);
    lv_label_set_text_fmt(yi, "宜  %s %s",
                          sp_activity_name(s_fortune.yi[0]),
                          sp_activity_name(s_fortune.yi[1]));
    y += 22;

    lv_obj_t *ji = small_label(scr, SP_C_WARN, 10, y,
                               LV_ALIGN_TOP_LEFT);
    if (s_fortune.ji[1] == SP_ID_NONE) {
        lv_label_set_text_fmt(ji, "忌  %s",
                              sp_activity_name(s_fortune.ji[0]));
    } else {
        lv_label_set_text_fmt(ji, "忌  %s %s",
                              sp_activity_name(s_fortune.ji[0]),
                              sp_activity_name(s_fortune.ji[1]));
    }
    y += 26;

    lv_obj_t *wish = small_label(scr, SP_C_GOLD, 10, y,
                                 LV_ALIGN_TOP_LEFT);
    lv_label_set_text(wish, sp_wish_text(s_fortune.wish_id));
    lv_obj_set_style_text_font(wish, SP_FONT_SMALL, 0);
    // 寄语较长，固定宽度并循环滚动避免越出屏幕右缘。
    lv_obj_set_width(wish, 220);
    lv_label_set_long_mode(wish, LV_LABEL_LONG_SCROLL_CIRCULAR);
    (void)pet;

    lv_obj_t *hint = small_label(scr, SP_C_DIM, 0, -12,
                                 LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(hint, "确认播报 长按返回");
}

static void open_fortune(void)
{
    sp_pet_t *pet = sp_state_pet();
    sp_date_t today;
    if (!pet || !sp_state_today(&today)) {
        return;
    }
    sp_fortune_today(pet->sign, today, &s_fortune);
    pet->fortune_seen = 1;
    sp_state_save();
    s_opened = true;
    sp_audio_earcon(SP_EAR_OPEN);

    lv_obj_clean(s_page.scr);
    build_opened(s_page.scr, pet);
}

static void play_voice(void)
{
    sp_pet_t *pet = sp_state_pet();
    if (!pet) {
        return;
    }
    spv_id_t ids[SP_AUDIO_VOICE_MAX];
    size_t n = sp_voice_plan(pet->sign, &s_fortune, ids,
                             SP_AUDIO_VOICE_MAX);
    sp_audio_voice(ids, n);
}

static void enter_page(sp_page_t *page)
{
    sp_ui_style_screen(page->scr);
    sp_pet_t *pet = sp_state_pet();
    sp_date_t today;
    s_opened = false;

    if (pet && pet->fortune_seen && sp_state_today(&today)) {
        sp_fortune_today(pet->sign, today, &s_fortune);
        s_opened = true;
        build_opened(page->scr, pet);
    } else {
        build_closed(page->scr);
    }
}

static void key_page(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    if (input->ev == SP_EV_LONG) {
        sp_audio_earcon(SP_EAR_CLICK);
        sp_app_goto(sp_page_menu());
        return;
    }
    if (input->ev != SP_EV_CLICK || input->btn != SP_BTN_OK) {
        return;
    }
    if (!s_opened) {
        if (!sp_clock_valid()) {
            sp_audio_earcon(SP_EAR_FAIL);
            return;
        }
        open_fortune();
    } else {
        sp_audio_earcon(SP_EAR_SELECT);
        play_voice();
    }
}

sp_page_t *sp_page_fortune(void)
{
    s_page = (sp_page_t) {
        .name = "fortune",
        .enter = enter_page,
        .key = key_page,
    };
    return &s_page;
}
