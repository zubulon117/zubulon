// main/sp_ui.h —— UI 公共层：配色字体、顶栏、星级、菜单、字段编辑、字符输入条。
//
// 所有函数只在 LVGL 任务（页面 enter/key/tick）中调用。
// 全部视觉为本应用重设计，未使用基线 demo/ui_pixel 任何组件。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "sp_app.h"
#include "sp_model.h"

// ------------------------------------------------------------- 配色字体 ---
#define SP_C_NIGHT   lv_color_hex(0x0B1029u)  // 星夜深蓝（底）
#define SP_C_GOLD    lv_color_hex(0xF6C453u)  // 星金（强调/选中）
#define SP_C_MIST    lv_color_hex(0xB9C4E8u)  // 雾蓝白（正文）
#define SP_C_DIM     lv_color_hex(0x5A648Cu)  // 暗蓝（次要）
#define SP_C_PANEL   lv_color_hex(0x141A3Au)  // 面板
#define SP_C_BLUSH   lv_color_hex(0xF49AB0u)  // 粉
#define SP_C_OK      lv_color_hex(0x7BC77Bu)  // 叶绿
#define SP_C_WARN    lv_color_hex(0xE86A6Au)  // 朱红

LV_FONT_DECLARE(sp_font_16);
LV_FONT_DECLARE(sp_font_20);

#define SP_FONT_SMALL (&sp_font_16)
#define SP_FONT_BIG   (&sp_font_20)

void sp_ui_style_screen(lv_obj_t *scr);

// 启动期字形自检：确认关键汉字在字体中可渲染（返回失败字数，0=通过）。
uint32_t sp_ui_glyph_selfcheck(void);

// ---------------------------------------------------------------- 顶栏 ---
// 布局：中间完整年月日（最上层居中），右侧电量。星座·阶段名由主页放在宠物图下方。
typedef struct {
    lv_obj_t *center;  // 日期
    lv_obj_t *right;   // 电量
} sp_ui_topbar_t;

// date 可为空（显示 "--/--/--"）；电量 -1 时显示 "--%"。
void sp_ui_topbar_create(sp_ui_topbar_t *tb, lv_obj_t *parent,
                         const sp_date_t *date, int battery_soc);
void sp_ui_topbar_refresh(const sp_ui_topbar_t *tb,
                          const sp_date_t *date, int battery_soc);

// ---------------------------------------------------------------- 星级 ---
// 生成 5 颗星的一行（画布绘制，不依赖字体里的星号字形）。
lv_obj_t *sp_ui_star_row_create(lv_obj_t *parent, uint8_t filled);

// ---------------------------------------------------------------- 菜单 ---
typedef struct sp_ui_menu sp_ui_menu_t;
typedef void (*sp_ui_menu_pick_cb_t)(uint8_t index, void *arg);

struct sp_ui_menu {
    lv_obj_t *holder;
    const char *const *items;
    uint8_t count;
    uint8_t index;
    sp_ui_menu_pick_cb_t on_pick;
    sp_ui_menu_pick_cb_t on_back;  // 长按返回（index 参数无意义）
    void *arg;
};

void sp_ui_menu_init(sp_ui_menu_t *m, lv_obj_t *parent,
                     const char *const *items, uint8_t count,
                     sp_ui_menu_pick_cb_t on_pick,
                     sp_ui_menu_pick_cb_t on_back, void *arg);
void sp_ui_menu_key(sp_ui_menu_t *m, const sp_input_t *input);

// ----------------------------------------------------- 多字段数字编辑 ---
typedef struct {
    const char *label;
    int32_t value;
    int32_t min;
    int32_t max;
    uint8_t digits;
} sp_ui_field_t;

typedef void (*sp_ui_form_done_cb_t)(void *arg);

typedef struct {
    const sp_ui_field_t *fields;
    uint8_t count;
    uint8_t cursor;
    lv_obj_t *value_labels[6];
    sp_ui_form_done_cb_t on_done;
    sp_ui_form_done_cb_t on_back;
    void *arg;
} sp_ui_form_t;

// 表单值直接读写 fields[i].value；UP/DOWN 改当前字段，OK 下一字段（末位=完成），
// 长按 = 返回。
void sp_ui_form_init(sp_ui_form_t *f, lv_obj_t *parent,
                     const sp_ui_field_t *fields, uint8_t count,
                     sp_ui_form_done_cb_t on_done,
                     sp_ui_form_done_cb_t on_back, void *arg);
void sp_ui_form_key(sp_ui_form_t *f, const sp_input_t *input);

// ------------------------------------------------------- 三键字符输入 ---
// 字符集内循环：UP/DOWN 换字（当前格必须先选字），OK 前进一格，
// 停在末尾空格再按 OK 完成；双击回删，长按完成。buf 始终保持连续前缀。
typedef struct {
    char *buf;
    size_t cap;
    const char *charset;
    size_t pos;
    bool finished;
    lv_obj_t *label;
    lv_obj_t *hint;
    sp_ui_form_done_cb_t on_done;
    void *arg;
} sp_ui_typebox_t;

void sp_ui_typebox_init(sp_ui_typebox_t *t, lv_obj_t *parent,
                        char *buf, size_t cap, const char *charset,
                        sp_ui_form_done_cb_t on_done, void *arg);
void sp_ui_typebox_key(sp_ui_typebox_t *t, const sp_input_t *input);

// ----------------------------------------------------------- 宠物画布 ---
// 24x24 I4 画布（sp_sprites 帧直接灌入），scale=1..3 整数放大。
lv_obj_t *sp_ui_pet_canvas_create(lv_obj_t *parent, uint8_t scale);
void sp_ui_pet_canvas_show(lv_obj_t *cv, uint8_t sign, uint8_t frame);
