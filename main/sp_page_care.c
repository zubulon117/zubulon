// main/sp_page_care.c —— 互动照料：喂食/抚摸/许愿签到，结果反馈与状态展示。
#include "sp_page_care.h"

#include <stdio.h>

#include "lvgl.h"

#include "sp_audio.h"
#include "sp_clock.h"
#include "sp_model.h"
#include "sp_state.h"
#include "sp_ui.h"

#include "sp_page_home.h"

static sp_page_t s_page;
static sp_ui_menu_t s_menu;
static lv_obj_t *s_stats;
static lv_obj_t *s_result;
static lv_timer_t *s_result_timer;

static const char *ACTION_ITEMS[] = {
    "喂食",
    "抚摸",
    "许愿签到",
};

static lv_obj_t *mk_small(lv_obj_t *parent, lv_color_t c, int32_t x,
                          int32_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);
    return l;
}

static void refresh_stats(void)
{
    sp_pet_t *pet = sp_state_pet();
    if (!pet) {
        return;
    }
    lv_label_set_text_fmt(s_stats,
                          "心情 %d    亲密 %d\n星币 %d    连签 %d 天",
                          pet->mood, pet->bond, pet->coins, pet->streak);
}

static void clear_result_timer(lv_timer_t *t)
{
    (void)t;
    if (s_result) {
        lv_label_set_text(s_result, "");
    }
    if (s_result_timer) {
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
    }
}

static void show_result(const char *text, bool good)
{
    lv_label_set_text(s_result, text);
    lv_obj_set_style_text_color(s_result, good ? SP_C_OK : SP_C_WARN, 0);
    if (s_result_timer) {
        lv_timer_delete(s_result_timer);
    }
    s_result_timer = lv_timer_create(clear_result_timer, 1600, NULL);
    lv_timer_set_repeat_count(s_result_timer, 1);
}

static void do_feed(void)
{
    sp_pet_t *pet = sp_state_pet();
    sp_act_result_t r = sp_pet_feed(pet);
    if (r == SP_ACT_OK) {
        sp_audio_earcon(SP_EAR_FEED);
        show_result("喂食成功，星宠很满足", true);
        sp_state_save();
    } else {
        sp_audio_earcon(SP_EAR_FAIL);
        show_result("今天已经喂饱啦", false);
    }
    refresh_stats();
}

static void do_pet(void)
{
    sp_pet_t *pet = sp_state_pet();
    // 时钟无效时传 -1：本次抚摸不参与冷却计时。
    int32_t now_min = sp_clock_valid() ? sp_clock_now_min() : -1;
    sp_act_result_t r = sp_pet_pet(pet, now_min);
    if (r == SP_ACT_OK) {
        sp_audio_earcon(SP_EAR_PET);
        show_result("星宠蹭了蹭你", true);
        sp_state_save();
    } else if (r == SP_ACT_COOLDOWN) {
        sp_audio_earcon(SP_EAR_FAIL);
        show_result("刚摸过啦，歇一会儿", false);
    } else {
        sp_audio_earcon(SP_EAR_FAIL);
        show_result("今天抚摸次数用完了", false);
    }
    refresh_stats();
}

static void do_checkin(void)
{
    sp_pet_t *pet = sp_state_pet();
    sp_date_t today;
    if (!sp_state_today(&today)) {
        sp_audio_earcon(SP_EAR_FAIL);
        show_result("请先校准日期", false);
        return;
    }
    sp_act_result_t r = sp_pet_checkin(pet, today);
    if (r == SP_ACT_OK) {
        sp_audio_earcon(SP_EAR_CHECKIN);
        uint16_t bonus = 10;
        if (pet->streak > 0) {
            uint16_t extra = pet->streak * SP_CHECKIN_BONUS_STEP;
            if (extra > SP_CHECKIN_BONUS_CAP * SP_CHECKIN_BONUS_STEP) {
                extra = SP_CHECKIN_BONUS_CAP * SP_CHECKIN_BONUS_STEP;
            }
            bonus += extra;
        }
        char msg[48];
        snprintf(msg, sizeof(msg), "许愿成功，获得 %d 星币", bonus);
        show_result(msg, true);
        sp_state_save();
    } else {
        sp_audio_earcon(SP_EAR_FAIL);
        show_result("今天已经许过愿了", false);
    }
    refresh_stats();
}

static void menu_pick(uint8_t index, void *arg)
{
    (void)arg;
    if (index == 0) {
        do_feed();
    } else if (index == 1) {
        do_pet();
    } else {
        do_checkin();
    }
}

static void menu_back(uint8_t index, void *arg)
{
    (void)index;
    (void)arg;
    sp_audio_earcon(SP_EAR_CLICK);
    sp_app_goto(sp_page_menu());
}

static void enter_page(sp_page_t *page)
{
    sp_ui_style_screen(page->scr);

    lv_obj_t *title = lv_label_create(page->scr);
    lv_label_set_text(title, "互动照料");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    s_stats = mk_small(page->scr, SP_C_MIST, 16, 64);
    refresh_stats();

    lv_obj_t *holder = lv_obj_create(page->scr);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, 160, 110);
    lv_obj_align(holder, LV_ALIGN_TOP_LEFT, 24, 126);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    sp_ui_menu_init(&s_menu, holder, ACTION_ITEMS, 3, menu_pick, menu_back,
                    NULL);

    s_result = lv_label_create(page->scr);
    lv_obj_set_style_text_font(s_result, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_result, SP_C_OK, 0);
    lv_obj_set_style_text_align(s_result, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_result, LV_ALIGN_TOP_MID, 0, 246);

    lv_obj_t *hint = lv_label_create(page->scr);
    lv_label_set_text(hint, "上下选择 确认执行 长按返回");
    lv_obj_set_style_text_font(hint, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, SP_C_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void exit_page(sp_page_t *page)
{
    (void)page;
    if (s_result_timer) {
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
    }
}

static void key_page(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    sp_ui_menu_key(&s_menu, input);
}

sp_page_t *sp_page_care(void)
{
    s_page = (sp_page_t) {
        .name = "care",
        .enter = enter_page,
        .exit = exit_page,
        .key = key_page,
    };
    return &s_page;
}
