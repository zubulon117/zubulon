// main/sp_page_dex.c —— 星缘图鉴：按日星友来访 + 十二星座收集页。
//
// 布局（240x240）：
//   顶部标题「星缘图鉴」；副行显示今日星友来访或「星图大师」；
//   中部 4x3 精灵格：已相遇=本色、未相遇=暗蓝剪影，选中格金框；
//   底部详情卡：2x 精灵 + 名字 + 性格一句话 + 速配（未相遇为占位文案）。
// 进入页面即与今日星友相遇（置位并落盘）；访客由日期离线推导。
// 速配规则：(sign+4)%12 恰为同象限另一星座（火土风水各 3+3 映射）。
#include <string.h>

#include "sp_audio.h"
#include "sp_page_dex.h"
#include "sp_page_home.h"
#include "sp_state.h"
#include "sp_text.h"
#include "sp_ui.h"

static sp_page_t s_dex_page;

#define CELL_W   56
#define CELL_H   30
#define GRID_X0  8
#define GRID_Y0  52
#define BREATH_MS 600

typedef struct {
    uint16_t mask;
    uint8_t cursor;        // 0..11
    uint8_t visitor;       // 今日星友星座
    bool has_today;        // 日期有效（有今日星友）
    bool all_met;
    uint32_t breath_t;
    bool frame_alt;
    lv_obj_t *cell_cv[12];
    lv_obj_t *cell_frame[12];
    lv_obj_t *detail_cv;
    lv_obj_t *name_label;
    lv_obj_t *text_label;
    lv_obj_t *sub_label;
    lv_obj_t *banner;
} dex_ctx_t;

static dex_ctx_t s_c;

static void refresh_cells(void)
{
    uint8_t frame = s_c.frame_alt ? 1 : 0;
    for (uint8_t i = 0; i < 12; i++) {
        bool met = sp_dex_has(s_c.mask, i);
        sp_ui_pet_canvas_show_ex(s_c.cell_cv[i], i, frame, !met);
        bool sel = i == s_c.cursor;
        lv_obj_set_style_border_width(s_c.cell_frame[i], sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_c.cell_frame[i],
                                      sel ? SP_C_GOLD : SP_C_PANEL, 0);
    }
}

static void refresh_detail(void)
{
    uint8_t s = s_c.cursor;
    bool met = sp_dex_has(s_c.mask, s);
    sp_ui_pet_canvas_show_ex(s_c.detail_cv, s, s_c.frame_alt ? 1 : 0, !met);
    if (met) {
        lv_obj_set_style_text_color(s_c.name_label, SP_C_GOLD, 0);
        lv_label_set_text(s_c.name_label, sp_sign_text(s)->name);
        lv_obj_set_style_text_color(s_c.text_label, SP_C_MIST, 0);
        lv_label_set_text(s_c.text_label, sp_sign_personality(s));
        lv_obj_set_style_text_color(s_c.sub_label, SP_C_BLUSH, 0);
        lv_label_set_text_fmt(s_c.sub_label, "速配 %s",
                              sp_sign_text((uint8_t)((s + 4) % 12))->name);
    } else {
        lv_obj_set_style_text_color(s_c.name_label, SP_C_DIM, 0);
        lv_label_set_text(s_c.name_label, "尚未相遇");
        lv_obj_set_style_text_color(s_c.text_label, SP_C_DIM, 0);
        lv_label_set_text(s_c.text_label, "相遇后解锁性格与速配");
        lv_label_set_text(s_c.sub_label, "");
    }
}

static void dex_enter(sp_page_t *page)
{
    sp_ui_style_screen(page->scr);
    memset(&s_c, 0, sizeof(s_c));

    // 与今日星友相遇：置位并立即落盘（仅开图鉴才算见面）。
    sp_date_t today;
    if (sp_state_today(&today)) {
        sp_pet_t *pet = sp_state_pet();
        uint8_t own = pet ? pet->sign : 0;
        s_c.visitor = sp_dex_visitor(own, today);
        s_c.has_today = true;
        sp_state_dex_visit(s_c.visitor);
    }
    s_c.mask = sp_state_dex();
    s_c.all_met = sp_dex_complete(s_c.mask);

    lv_obj_t *title = lv_label_create(page->scr);
    lv_label_set_text(title, "星缘图鉴");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_c.banner = lv_label_create(page->scr);
    lv_obj_set_style_text_font(s_c.banner, SP_FONT_SMALL, 0);
    if (s_c.all_met) {
        lv_obj_set_style_text_color(s_c.banner, SP_C_GOLD, 0);
        lv_label_set_text(s_c.banner, "星图大师 · 十二星缘已集齐");
    } else if (s_c.has_today) {
        lv_obj_set_style_text_color(s_c.banner, SP_C_BLUSH, 0);
        lv_label_set_text_fmt(s_c.banner, "今日星友 %s 来访",
                              sp_sign_text(s_c.visitor)->name);
    } else {
        lv_label_set_text(s_c.banner, "");
    }
    lv_obj_align(s_c.banner, LV_ALIGN_TOP_MID, 0, 33);

    for (uint8_t i = 0; i < 12; i++) {
        uint8_t col = i % 4;
        uint8_t row = i / 4;
        lv_obj_t *fr = lv_obj_create(page->scr);
        lv_obj_remove_style_all(fr);
        lv_obj_set_size(fr, 52, CELL_H - 2);
        lv_obj_set_pos(fr, GRID_X0 + col * CELL_W, GRID_Y0 + row * CELL_H);
        lv_obj_set_style_border_width(fr, 1, 0);
        lv_obj_set_style_border_color(fr, SP_C_PANEL, 0);
        lv_obj_set_style_radius(fr, 6, 0);
        lv_obj_clear_flag(fr, LV_OBJ_FLAG_SCROLLABLE);
        s_c.cell_frame[i] = fr;

        s_c.cell_cv[i] = sp_ui_pet_canvas_create(fr, 1);
        if (s_c.cell_cv[i]) {
            lv_obj_center(s_c.cell_cv[i]);
        }
    }

    s_c.detail_cv = sp_ui_pet_canvas_create(page->scr, 2);
    if (s_c.detail_cv) {
        lv_obj_set_pos(s_c.detail_cv, 14, 158);
    }

    s_c.name_label = lv_label_create(page->scr);
    lv_obj_set_style_text_font(s_c.name_label, SP_FONT_BIG, 0);
    lv_obj_set_pos(s_c.name_label, 76, 156);

    s_c.text_label = lv_label_create(page->scr);
    lv_obj_set_style_text_font(s_c.text_label, SP_FONT_SMALL, 0);
    lv_obj_set_pos(s_c.text_label, 76, 182);

    s_c.sub_label = lv_label_create(page->scr);
    lv_obj_set_style_text_font(s_c.sub_label, SP_FONT_SMALL, 0);
    lv_obj_set_pos(s_c.sub_label, 76, 204);

    lv_obj_t *hint = lv_label_create(page->scr);
    lv_label_set_text(hint, "上下选择 长按返回");
    lv_obj_set_style_text_font(hint, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, SP_C_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    refresh_cells();
    refresh_detail();
}

static void dex_key(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    if (input->ev == SP_EV_LONG) {
        sp_audio_earcon(SP_EAR_CLICK);
        sp_app_goto(sp_page_menu());
        return;
    }
    if (input->ev != SP_EV_CLICK) {
        return;
    }
    if (input->btn == SP_BTN_UP || input->btn == SP_BTN_DOWN) {
        s_c.cursor = (uint8_t)((s_c.cursor +
                                (input->btn == SP_BTN_UP ? 11 : 1)) % 12);
        sp_audio_earcon(SP_EAR_CLICK);
        refresh_cells();
        refresh_detail();
    }
}

static void dex_tick(sp_page_t *page, uint32_t ms)
{
    (void)page;
    s_c.breath_t += ms;
    if (s_c.breath_t >= BREATH_MS) {
        s_c.breath_t = 0;
        s_c.frame_alt = !s_c.frame_alt;
        refresh_cells();
        refresh_detail();
    }
}

sp_page_t *sp_page_dex(void)
{
    s_dex_page = (sp_page_t) {
        .name = "dex",
        .enter = dex_enter,
        .key = dex_key,
        .tick = dex_tick,
    };
    return &s_dex_page;
}
