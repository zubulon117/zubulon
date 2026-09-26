// main/sp_page_home.c —— 主页：星宠动画、状态概览、主菜单入口。
#include "sp_page_home.h"

#include <stdio.h>

#include "lvgl.h"

#include "sp_audio.h"
#include "sp_model.h"
#include "sp_state.h"
#include "sp_text.h"
#include "sp_ui.h"

#include "sp_page_care.h"
#include "sp_page_fortune.h"
#include "sp_page_settings.h"

// ---------------------------------------------------------------- 主页 ---
typedef struct {
    sp_ui_topbar_t bar;
    lv_obj_t *pet_cv;
    lv_obj_t *mood_lab;
    lv_obj_t *bond_lab;
    lv_obj_t *coins_lab;
    lv_obj_t *hint_lab;
    lv_timer_t *anim;
    uint8_t frame_phase;
} home_ctx_t;

static sp_page_t s_home_page;
static home_ctx_t s_home;

static const uint8_t FRAME_SEQ[] = { 0, 0, 0, 1, 0, 0, 2, 0 };

static void refresh_home(void)
{
    sp_pet_t *pet = sp_state_pet();
    if (!pet) {
        return;
    }
    sp_date_t today;
    sp_date_t *tp = sp_state_today(&today) ? &today : NULL;
    sp_ui_topbar_refresh(&s_home.bar, pet->sign, pet->stage, tp,
                         sp_state_battery());
    lv_label_set_text_fmt(s_home.mood_lab, "心情 %d", pet->mood);
    lv_label_set_text_fmt(s_home.bond_lab, "亲密 %d", pet->bond);
    lv_label_set_text_fmt(s_home.coins_lab, "星币 %d", pet->coins);
    lv_label_set_text(s_home.hint_lab,
                      pet->fortune_seen ? "确认打开菜单" : "今日星签未开");
}

static void home_anim_cb(lv_timer_t *t)
{
    (void)t;
    sp_pet_t *pet = sp_state_pet();
    if (!pet || !s_home.pet_cv) {
        return;
    }
    s_home.frame_phase++;
    uint8_t f = FRAME_SEQ[s_home.frame_phase %
                          (sizeof(FRAME_SEQ) / sizeof(FRAME_SEQ[0]))];
    sp_ui_pet_canvas_show(s_home.pet_cv, pet->sign, f);
}

static void home_enter(sp_page_t *page)
{
    sp_ui_style_screen(page->scr);

    sp_pet_t *pet = sp_state_pet();
    sp_date_t today;
    sp_date_t *tp = sp_state_today(&today) ? &today : NULL;
    sp_state_catchup();
    sp_ui_topbar_create(&s_home.bar, page->scr,
                        pet ? pet->sign : 0, pet ? pet->stage : 0,
                        tp, sp_state_battery());

    s_home.pet_cv = sp_ui_pet_canvas_create(page->scr, 3);
    lv_obj_align(s_home.pet_cv, LV_ALIGN_CENTER, 0, -34);
    if (pet) {
        sp_ui_pet_canvas_show(s_home.pet_cv, pet->sign, 0);
    }

    lv_obj_t *row = lv_obj_create(page->scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 228, 24);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 84);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_home.mood_lab = lv_label_create(row);
    s_home.bond_lab = lv_label_create(row);
    s_home.coins_lab = lv_label_create(row);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_text_font(lv_obj_get_child(row, i),
                                   SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(row, i),
                                    SP_C_MIST, 0);
    }

    s_home.hint_lab = lv_label_create(page->scr);
    lv_obj_set_style_text_font(s_home.hint_lab, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_home.hint_lab, SP_C_GOLD, 0);
    lv_obj_align(s_home.hint_lab, LV_ALIGN_BOTTOM_MID, 0, -12);

    refresh_home();
    s_home.frame_phase = 0;
    s_home.anim = lv_timer_create(home_anim_cb, 600, NULL);
}

static void home_exit(sp_page_t *page)
{
    (void)page;
    if (s_home.anim) {
        lv_timer_delete(s_home.anim);
        s_home.anim = NULL;
    }
}

static void home_key(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    if (input->ev == SP_EV_CLICK && input->btn == SP_BTN_OK) {
        sp_audio_earcon(SP_EAR_CLICK);
        sp_app_goto(sp_page_menu());
    }
}

sp_page_t *sp_page_home(void)
{
    s_home_page = (sp_page_t) {
        .name = "home",
        .enter = home_enter,
        .exit = home_exit,
        .key = home_key,
    };
    return &s_home_page;
}

// ---------------------------------------------------------------- 菜单 ---
static sp_page_t s_menu_page;
static sp_ui_menu_t s_menu;

static const char *MENU_ITEMS[] = {
    "今日运势",
    "互动照料",
    "设置",
};

static void menu_pick(uint8_t index, void *arg)
{
    (void)arg;
    sp_audio_earcon(SP_EAR_SELECT);
    if (index == 0) {
        sp_app_goto(sp_page_fortune());
    } else if (index == 1) {
        sp_app_goto(sp_page_care());
    } else {
        sp_app_goto(sp_page_settings());
    }
}

static void menu_back(uint8_t index, void *arg)
{
    (void)index;
    (void)arg;
    sp_audio_earcon(SP_EAR_CLICK);
    sp_app_goto(sp_page_home());
}

static void menu_enter(sp_page_t *page)
{
    sp_ui_style_screen(page->scr);
    lv_obj_t *title = lv_label_create(page->scr);
    lv_label_set_text(title, "星宠菜单");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    lv_obj_t *holder = lv_obj_create(page->scr);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, 200, 180);
    lv_obj_align(holder, LV_ALIGN_CENTER, 0, 20);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);

    sp_ui_menu_init(&s_menu, holder, MENU_ITEMS, 3, menu_pick, menu_back,
                    NULL);

    lv_obj_t *hint = lv_label_create(page->scr);
    lv_label_set_text(hint, "上下选择 长按返回");
    lv_obj_set_style_text_font(hint, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, SP_C_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void menu_key(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    sp_ui_menu_key(&s_menu, input);
}

sp_page_t *sp_page_menu(void)
{
    s_menu_page = (sp_page_t) {
        .name = "menu",
        .enter = menu_enter,
        .key = menu_key,
    };
    return &s_menu_page;
}
