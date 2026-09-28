#include "sp_ui.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sp_sprites.h"
#include "sp_text.h"

// 解码 UTF-8 下一个码点，返回消耗字节数（0=串结束）。避免依赖 LVGL 私有头。
static int utf8_next(const char *p, uint32_t *cp)
{
    unsigned char c = (unsigned char)*p;
    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    uint32_t v = c & ((1u << (7 - n)) - 1);
    for (int i = 1; i < n; i++) {
        v = (v << 6) | ((unsigned char)p[i] & 0x3Fu);
    }
    *cp = v;
    return n;
}

// ----------------------------------------------------------- 基础样式 ---
void sp_ui_style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, SP_C_NIGHT, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

uint32_t sp_ui_glyph_selfcheck(void)
{
    static const char *SAMPLES[] = {
        "命定星宠今日运势综合爱情事业财运健康",
        "幸运数字颜色宜吉凶吉祥物",
        "亲密心情星币喂食抚摸许愿签到阶段星幼伴辉",
        "设置日期时间网络连接扫描密码开放校时关于",
        "音量恢复出厂确认取消返回长按双击上下确认",
        "白羊金双巨蟹狮女天秤蝎射摩羯瓶鱼生肖",
        "加载中请稍候成功失败冷却已满次数",
        "星缘图鉴尚未相遇后解锁性格速配集齐大师来访",
        "，。！？：（）~",
    };
    uint32_t missing = 0;
    for (size_t s = 0; s < sizeof(SAMPLES) / sizeof(SAMPLES[0]); s++) {
        const char *p = SAMPLES[s];
        while (*p) {
            uint32_t cp;
            int adv = utf8_next(p, &cp);
            if (adv <= 0) {
                break;
            }
            lv_font_glyph_dsc_t dsc;
            if (!lv_font_get_glyph_dsc(SP_FONT_SMALL, &dsc, cp, 0)) {
                missing++;
            }
            p += adv;
        }
    }
    return missing;
}

// --------------------------------------------------------------- 顶栏 ---
static lv_obj_t *make_bar_label(lv_obj_t *parent, lv_align_t align,
                                int32_t x, int32_t y, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

void sp_ui_topbar_create(sp_ui_topbar_t *tb, lv_obj_t *parent,
                         const sp_date_t *date, int battery_soc)
{
    memset(tb, 0, sizeof(*tb));
    // 完整年月日置于最上层居中；右侧电量。星座·阶段名由主页另放。
    tb->center = make_bar_label(parent, LV_ALIGN_TOP_MID, 0, 4, SP_C_GOLD);
    tb->right = make_bar_label(parent, LV_ALIGN_TOP_RIGHT, -6, 4, SP_C_MIST);
    sp_ui_topbar_refresh(tb, date, battery_soc);
}

void sp_ui_topbar_refresh(const sp_ui_topbar_t *tb,
                          const sp_date_t *date, int battery_soc)
{
    if (!tb || !tb->center) {
        return;
    }
    if (date) {
        lv_label_set_text_fmt(tb->center, "%d/%02d/%02d",
                              (int)date->year, date->month, date->day);
    } else {
        lv_label_set_text(tb->center, "--/--/--");
    }
    if (battery_soc >= 0) {
        lv_label_set_text_fmt(tb->right, "%d%%", battery_soc);
    } else {
        lv_label_set_text(tb->right, "");
    }
}

// --------------------------------------------------------------- 星级 ---
// 10x10 像素星（1=星），ARGB8888 画布，背景全透明。
static const uint8_t STAR_MASK[10][10] = {
    {0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0},
    {0,0,0,1,1,1,1,0,0,0},
    {0,0,0,1,1,1,1,0,0,0},
    {1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1},
    {0,1,1,1,1,1,1,1,1,0},
    {0,1,1,0,1,1,0,1,1,0},
    {1,1,0,0,0,0,0,0,1,1},
    {1,0,0,0,0,0,0,0,0,1},
};

static void canvas_free_evt(lv_event_t *e)
{
    void *buf = lv_event_get_user_data(e);
    free(buf);
}

lv_obj_t *sp_ui_star_row_create(lv_obj_t *parent, uint8_t filled)
{
    if (filled > 5) {
        filled = 5;
    }
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 5 * 12, 12);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_pad_column(cont, 2, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        uint8_t *buf = malloc(10 * 10 * 4);
        if (!buf) {
            continue;
        }
        lv_color32_t on = (lv_color32_t) {
            .red = 0xF6, .green = 0xC4, .blue = 0x53, .alpha = 0xFF
        };
        lv_color32_t off = (lv_color32_t) {
            .red = 0x5A, .green = 0x64, .blue = 0x8C, .alpha = 0xFF
        };
        lv_color32_t empty = (lv_color32_t) {
            .red = 0, .green = 0, .blue = 0, .alpha = 0
        };
        uint8_t *p = buf;
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                lv_color32_t px = STAR_MASK[y][x] ?
                                  ((uint8_t)i < filled ? on : off) : empty;
                *p++ = px.blue;
                *p++ = px.green;
                *p++ = px.red;
                *p++ = px.alpha;
            }
        }
        lv_obj_t *cv = lv_canvas_create(cont);
        lv_canvas_set_buffer(cv, buf, 10, 10, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_add_event_cb(cv, canvas_free_evt, LV_EVENT_DELETE, buf);
    }
    return cont;
}

// --------------------------------------------------------------- 菜单 ---
static void menu_render(sp_ui_menu_t *m)
{
    lv_obj_clean(m->holder);
    // holder 尺寸为百分比，需先强制结算布局，否则 content_height 为 0，
    // visible 被钳到 1（表现为菜单一次只显示一项）。
    lv_obj_update_layout(m->holder);
    int32_t avail_h = lv_obj_get_content_height(m->holder);
    int32_t row_h = 28;
    int8_t visible = (int8_t)(avail_h / row_h);
    if (visible > m->count) {
        visible = m->count;
    }
    if (visible < 1) {
        visible = 1;
    }
    int8_t start = (int8_t)m->index - visible / 2;
    if (start < 0) {
        start = 0;
    }
    if (start > m->count - visible) {
        start = m->count - visible;
    }

    for (int8_t i = start; i < start + visible; i++) {
        bool sel = (i == m->index);
        // 空列表或空项防御：扫描结果为空时 items 可能为 NULL。
        const char *text = (i < m->count && m->items[i]) ? m->items[i] : "";
        lv_obj_t *row = lv_label_create(m->holder);
        lv_label_set_text_fmt(row, "%s %s", sel ? ">" : " ", text);
        lv_obj_set_style_text_font(row, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(row, sel ? SP_C_GOLD : SP_C_MIST, 0);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, (i - start) * row_h + 2);
    }
}

void sp_ui_menu_init(sp_ui_menu_t *m, lv_obj_t *parent,
                     const char *const *items, uint8_t count,
                     sp_ui_menu_pick_cb_t on_pick,
                     sp_ui_menu_pick_cb_t on_back, void *arg)
{
    memset(m, 0, sizeof(*m));
    m->items = items;
    m->count = count;
    m->on_pick = on_pick;
    m->on_back = on_back;
    m->arg = arg;
    m->holder = lv_obj_create(parent);
    lv_obj_remove_style_all(m->holder);
    lv_obj_set_size(m->holder, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(m->holder, 6, 0);
    lv_obj_clear_flag(m->holder, LV_OBJ_FLAG_SCROLLABLE);
    menu_render(m);
}

void sp_ui_menu_key(sp_ui_menu_t *m, const sp_input_t *input)
{
    if (input->ev != SP_EV_CLICK && input->ev != SP_EV_LONG) {
        return;
    }
    if (input->ev == SP_EV_LONG) {
        if (m->on_back) {
            m->on_back(m->index, m->arg);
        }
        return;
    }
    if (input->btn == SP_BTN_UP) {
        if (m->index > 0) {
            m->index--;
        }
        menu_render(m);
    } else if (input->btn == SP_BTN_DOWN) {
        if (m->index < m->count - 1) {
            m->index++;
        }
        menu_render(m);
    } else {
        if (m->on_pick) {
            m->on_pick(m->index, m->arg);
        }
    }
}

// ----------------------------------------------------------- 数字表单 ---
static void form_render(sp_ui_form_t *f)
{
    for (uint8_t i = 0; i < f->count; i++) {
        if (!f->value_labels[i]) {
            continue;
        }
        char fmt[8];
        snprintf(fmt, sizeof(fmt), "%%0%dd", f->fields[i].digits);
        lv_label_set_text_fmt(f->value_labels[i], fmt,
                              (int)f->fields[i].value);
        lv_obj_set_style_text_color(f->value_labels[i],
                                    i == f->cursor ? SP_C_GOLD : SP_C_MIST, 0);
    }
}

void sp_ui_form_init(sp_ui_form_t *f, lv_obj_t *parent,
                     const sp_ui_field_t *fields, uint8_t count,
                     sp_ui_form_done_cb_t on_done,
                     sp_ui_form_done_cb_t on_back, void *arg)
{
    memset(f, 0, sizeof(*f));
    f->fields = fields;
    f->count = count;
    f->on_done = on_done;
    f->on_back = on_back;
    f->arg = arg;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 70);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_center(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lab = lv_label_create(col);
        lv_label_set_text(lab, fields[i].label);
        lv_obj_set_style_text_font(lab, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lab, SP_C_DIM, 0);

        f->value_labels[i] = lv_label_create(col);
        lv_obj_set_style_text_font(f->value_labels[i], SP_FONT_BIG, 0);
    }
    form_render(f);
}

void sp_ui_form_key(sp_ui_form_t *f, const sp_input_t *input)
{
    if (input->ev != SP_EV_CLICK && input->ev != SP_EV_LONG) {
        return;
    }
    if (input->ev == SP_EV_LONG) {
        if (f->on_back) {
            f->on_back(f->arg);
        }
        return;
    }
    sp_ui_field_t *cur = (sp_ui_field_t *)&f->fields[f->cursor];
    if (input->btn == SP_BTN_UP) {
        if (cur->value < cur->max) {
            cur->value++;
        }
    } else if (input->btn == SP_BTN_DOWN) {
        if (cur->value > cur->min) {
            cur->value--;
        }
    } else {
        if (f->cursor < f->count - 1) {
            f->cursor++;
        } else if (f->on_done) {
            f->on_done(f->arg);
            return;
        }
    }
    form_render(f);
}

// ----------------------------------------------------------- 字符输入 ---
static void typebox_render(sp_ui_typebox_t *t)
{
    lv_label_set_text(t->label, t->buf[0] ? t->buf : "_");
}

void sp_ui_typebox_init(sp_ui_typebox_t *t, lv_obj_t *parent,
                        char *buf, size_t cap, const char *charset,
                        sp_ui_form_done_cb_t on_done, void *arg)
{
    memset(t, 0, sizeof(*t));
    t->buf = buf;
    t->cap = cap;
    t->charset = charset;
    t->on_done = on_done;
    t->arg = arg;
    if (cap) {
        buf[0] = '\0';
    }

    t->label = lv_label_create(parent);
    lv_obj_set_style_text_font(t->label, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(t->label, SP_C_GOLD, 0);
    lv_obj_align(t->label, LV_ALIGN_CENTER, 0, -12);

    t->hint = lv_label_create(parent);
    lv_label_set_text(t->hint, "上下换字 确认前进 双删 长按完成");
    lv_obj_set_style_text_font(t->hint, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(t->hint, SP_C_DIM, 0);
    lv_obj_align(t->hint, LV_ALIGN_CENTER, 0, 24);
    typebox_render(t);
}

static size_t charset_len(const char *s)
{
    size_t n = 0;
    while (*s) {
        uint32_t cp;
        int adv = utf8_next(s, &cp);
        if (adv <= 0) {
            break;
        }
        n++;
        s += adv;
    }
    return n;
}

void sp_ui_typebox_key(sp_ui_typebox_t *t, const sp_input_t *input)
{
    if (t->finished || input->ev == SP_EV_PRESS) {
        return;
    }
    size_t cs_n = charset_len(t->charset);

    if (input->ev == SP_EV_LONG) {
        t->finished = true;
        if (t->on_done) {
            t->on_done(t->arg);
        }
        return;
    }
    if (input->ev == SP_EV_DOUBLE) {
        // 回删一格。
        size_t len = strlen(t->buf);
        if (len > 0) {
            // 字符集为 ASCII，直接按字节退格。
            t->buf[len - 1] = '\0';
            if (t->pos > len - 1) {
                t->pos = len - 1;
            }
        }
        typebox_render(t);
        return;
    }
    if (input->ev != SP_EV_CLICK) {
        return;
    }

    if (input->btn == SP_BTN_OK) {
        size_t len = strlen(t->buf);
        // 不变式：buf 始终是连续前缀，pos <= len，且每格必须先选字。
        if (t->pos < len) {
            t->pos++;                 // 已有字的格：光标前进
        } else if (len > 0) {
            // 停在末尾空格且已有内容：完成输入。
            t->finished = true;
            if (t->on_done) {
                t->on_done(t->arg);
            }
            return;
        }
        // 首格尚未选字：忽略，避免在 buf 中留下 '\0' 空洞。
        return;
    }

    // UP/DOWN 改当前格字符（ASCII 字符集）。
    size_t len = strlen(t->buf);
    if (t->pos >= t->cap - 1) {
        return;
    }
    int8_t delta = input->btn == SP_BTN_UP ? 1 : -1;
    char cur = t->pos < len ? t->buf[t->pos] : '\0';
    int idx = -1;
    if (cur) {
        const char *p = strchr(t->charset, cur);
        if (p) {
            idx = (int)(p - t->charset);
        }
    }
    idx += delta;
    if (idx < 0) {
        idx = (int)cs_n - 1;
    }
    if (idx >= (int)cs_n) {
        idx = 0;
    }
    t->buf[t->pos] = t->charset[idx];
    if (t->pos == len) {
        t->buf[t->pos + 1] = '\0';
    }
    typebox_render(t);
}

// ----------------------------------------------------------- 宠物画布 ---
#define SP_PET_CANVAS_W 24
#define SP_PET_CANVAS_H 24
// 原数据为 I4（低 nibble 在前），渲染时由我们自己查表展开为 ARGB8888，
// 完全绕开 LVGL 9.5 索引色解码器在画布缩放下的缓存生命周期问题。
#define SP_PET_ARGB_BPP 4
#define SP_PET_ARGB_PX_BYTES (SP_PET_CANVAS_W * SP_PET_CANVAS_H * SP_PET_ARGB_BPP)

lv_obj_t *sp_ui_pet_canvas_create(lv_obj_t *parent, uint8_t scale)
{
    uint8_t *buf = malloc(SP_PET_ARGB_PX_BYTES);
    if (!buf) {
        return NULL;
    }
    memset(buf, 0, SP_PET_ARGB_PX_BYTES);

    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, buf, SP_PET_CANVAS_W, SP_PET_CANVAS_H,
                         LV_COLOR_FORMAT_ARGB8888);
    if (scale < 1) {
        scale = 1;
    }
    if (scale > 3) {
        scale = 3;
    }
    lv_image_set_scale(cv, 256u * scale);
    lv_obj_add_event_cb(cv, canvas_free_evt, LV_EVENT_DELETE, buf);
    return cv;
}

static void pet_canvas_paint(lv_obj_t *cv, const uint8_t *src,
                             const uint32_t *pal)
{
    uint8_t *px = (uint8_t *)lv_canvas_get_buf(cv);
    if (!px) {
        return;
    }
    for (int y = 0; y < SP_PET_CANVAS_H; y++) {
        for (int x = 0; x < SP_PET_CANVAS_W; x++) {
            uint8_t byte = src[y * SP_PET_CANVAS_W / 2 + x / 2];
            uint8_t nibble = (x & 1) ? (byte >> 4) : (byte & 0x0F);
            uint32_t argb = pal[nibble];
            int o = (y * SP_PET_CANVAS_W + x) * SP_PET_ARGB_BPP;
            px[o + 0] = (uint8_t)(argb & 0xFF);         // B
            px[o + 1] = (uint8_t)((argb >> 8) & 0xFF);  // G
            px[o + 2] = (uint8_t)((argb >> 16) & 0xFF); // R
            px[o + 3] = (uint8_t)((argb >> 24) & 0xFF); // A
        }
    }
    lv_obj_invalidate(cv);
}

void sp_ui_pet_canvas_show(lv_obj_t *cv, uint8_t sign, uint8_t frame)
{
    if (!cv) {
        return;
    }
    pet_canvas_paint(cv, sp_sprite_frame(sign, frame),
                     sp_sprite_palette(sign));
}

void sp_ui_pet_canvas_show_ex(lv_obj_t *cv, uint8_t sign, uint8_t frame,
                              bool silhouette)
{
    if (!cv) {
        return;
    }
    if (!silhouette) {
        sp_ui_pet_canvas_show(cv, sign, frame);
        return;
    }
    // 剪影调色板：非透明项统一映射为暗蓝，栈上 16 项无额外常驻内存。
    uint32_t mono[16];
    const uint32_t dim = 0xFF2A3358u;
    mono[0] = 0;
    for (int i = 1; i < 16; i++) {
        mono[i] = dim;
    }
    pet_canvas_paint(cv, sp_sprite_frame(sign, frame), mono);
}
