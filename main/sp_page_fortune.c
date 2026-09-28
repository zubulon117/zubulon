// main/sp_page_fortune.c —— 每日离线运势：开签、五维星级、幸运信息、语音播报。
// 进入页面先播占卜开场动画：小魔女 + 占卜球渐亮，球内逐星点亮并连出
// 本命星座；单击跳过。未开签时场景常驻为开签背景（球呼吸、星闪烁）。
#include "sp_page_fortune.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
// 在已有场景之上补开签文案（动画结束后场景驻留）。
static void add_closed_texts(lv_obj_t *scr)
{
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "今日星签");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *body = small_label(scr, SP_C_MIST, 0, -34,
                                 LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(body, "星光已经写好今天的答案\n按确认开签");
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hint = small_label(scr, SP_C_DIM, 0, -10,
                                 LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(hint, "长按返回");
}

// 时钟无效等无法占卜时的静态提示页（无动画）。
static void build_closed(lv_obj_t *scr)
{
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "今日星签");
    lv_obj_set_style_text_font(title, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(title, SP_C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -46);

    lv_obj_t *body = small_label(scr, SP_C_MIST, 0, 0, LV_ALIGN_CENTER);
    lv_label_set_text(body, "时钟未校准，无法开签\n请先在设置中校准日期");
    lv_obj_set_style_text_color(body, SP_C_WARN, 0);
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

// ----------------------------------------------------- 占卜开场动画 ---
// 小魔女 + 占卜球：球内按本命星座连线。全部画布走 ARGB8888 + 自绘，
// 避开 LVGL 9.5 索引色/缩放解码器的已知问题；缓冲随屏删除自动释放。

#define BALL_W      96
#define BALL_CX     48
#define BALL_CY     48
#define BALL_R      42
#define BALL_R2     (BALL_R * BALL_R)
#define WITCH_W     22
#define WITCH_H     26
// 远景画布：女巫 + 小占卜球（球心/半径为画布内坐标）。
#define WIDE_W      120
#define WIDE_H      88
#define WIDE_BALL_CX 60
#define WIDE_BALL_CY 58
#define WIDE_BALL_R  14
#define WIDE_T0     900     // 远景保持时长，之后开始推镜
#define FADE_MS     800     // 远景→近景交叉淡化时长
#define INTRO_MS    3500
#define STAR_T0     1800    // 近景中首颗星座星出现时刻
#define STAR_STEP   90
#define LINE_T0     2300    // 首段连线时刻
#define LINE_STEP   110
#define LINE_DRAW   240

// 12 星座简化连线星图：星点/边均用 0..99 归一化坐标，y 向下。
static const uint8_t CST_N[SP_SIGN_COUNT] = {
    5, 6, 6, 6, 7, 6, 5, 9, 8, 6, 7, 7,
};
static const uint8_t CST_STARS[SP_SIGN_COUNT][9][2] = {
    { {18,66},{30,55},{45,42},{58,38},{72,45} },                      // 白羊
    { {18,30},{30,45},{38,50},{45,62},{58,50},{75,32} },              // 金牛
    { {35,15},{36,38},{30,60},{55,15},{54,38},{62,60} },              // 双子
    { {50,18},{50,35},{42,40},{58,40},{45,65},{58,62} },              // 巨蟹
    { {30,72},{20,58},{18,42},{26,30},{38,24},{60,42},{66,58} },      // 狮子
    { {35,78},{45,58},{58,45},{72,30},{52,22},{38,32} },              // 处女
    { {50,20},{25,55},{72,58},{38,80},{62,82} },                      // 天秤
    { {28,18},{40,24},{50,20},{60,36},{68,50},{70,64},{58,76},{42,78},
      {34,68} },                                                      // 天蝎
    { {30,40},{45,30},{58,35},{62,50},{50,58},{35,55},{70,25},{75,40} }, // 射手
    { {15,35},{30,50},{50,58},{70,50},{85,32},{58,70} },              // 摩羯
    { {20,30},{35,42},{50,35},{65,48},{78,40},{60,62},{45,70} },      // 水瓶
    { {25,35},{35,28},{40,38},{30,45},{60,55},{80,65},{88,56} },      // 双鱼
};
static const uint8_t CST_NE[SP_SIGN_COUNT] = {
    4, 5, 5, 5, 7, 6, 5, 8, 9, 5, 6, 7,
};
static const uint8_t CST_EDGES[SP_SIGN_COUNT][9][2] = {
    { {0,1},{1,2},{2,3},{3,4} },
    { {0,1},{1,2},{2,3},{3,4},{4,5} },
    { {0,1},{1,2},{3,4},{4,5},{1,4} },
    { {0,1},{1,2},{1,3},{2,4},{3,5} },
    { {0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,0} },
    { {0,1},{1,2},{2,3},{2,4},{4,5},{5,1} },
    { {0,1},{0,2},{1,3},{2,4},{3,4} },
    { {0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7},{7,8} },
    { {0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{2,6},{6,7},{7,3} },
    { {0,1},{1,2},{2,3},{3,4},{2,5} },
    { {0,1},{1,2},{2,3},{3,4},{3,5},{5,6} },
    { {0,1},{1,2},{2,3},{3,0},{2,4},{4,5},{5,6} },
};

// 球内点缀星（"天上"的背景星），与星座星图区分开。
static const uint8_t SKY_STARS[][2] = {
    {12,20},{30,10},{52,8},{70,18},{84,30},{88,52},{80,72},{62,86},
    {40,90},{20,80},{8,58},{8,36},{44,26},{66,70},{26,44},{58,14},
};
#define SKY_N (sizeof(SKY_STARS) / sizeof(SKY_STARS[0]))

// 小魔女像素稿（22x26，正面，双臂下垂扶向身前占卜球；球体与双手
// 由远景画布在球体之后叠加绘制，形成"双手放在水晶球上"）。
static const char *WITCH_ART[WITCH_H] = {
    "..........HH..........",
    "..........HH..........",
    ".........HHHH.........",
    "........HHHHHH........",
    ".......HHHHHHHH.......",
    "......HHHsHHHHH.......",
    ".DDDDDDDDDDDDDDDDDD...",
    ".......GGGGGG.........",
    "......GGSSSSGG........",
    "......GSESSESG........",
    "......GSSSSSSG........",
    ".......SSSSSS.........",
    "........AAAA..........",
    ".......AAaAAA.........",
    "......AAaAAaAA........",
    "......AAaAAaAAA.......",
    "......AAAAAAAAAA......",
    ".....AAAAAAAAAAAA.....",
    "....AAAAAAAAAAAAAA....",
    "....SAAAAAAAAAAAAS....",
    ".....AAAAAAAAAAA......",
    "......AAAAAAAA........",
    ".......AAAAAA.........",
    ".......AA..AA.........",
    ".......EE..EE.........",
    ".......EE..EE.........",
};
static const struct { char c; uint32_t argb; } WITCH_PAL[] = {
    { 'H', 0xFF8C5AB8 },  // 帽紫
    { 'D', 0xFF4A2E7A },  // 帽檐深紫
    { 'G', 0xFFC98A4C },  // 姜黄发
    { 'S', 0xFFFFD9B3 },  // 皮肤
    { 'E', 0xFF241C48 },  // 眼/靴
    { 'A', 0xFF3A4CA8 },  // 裙蓝
    { 'a', 0xFF5A6CD0 },  // 裙浅
    { 's', 0xFFF6C453 },  // 金星扣/杖头星
    { 'B', 0xFF8A5A33 },  // 杖木
};

typedef struct {
    bool intro;          // 动画播放中
    bool scene;          // 场景存在（含驻留）
    uint32_t t;          // 场景时间（ms）
    uint8_t sign;        // 展示的星座
    lv_obj_t *wide;      // 远景画布（推镜完成后删除释放）
    lv_obj_t *ball;      // 近景占卜球画布
} intro_t;

static intro_t s_ix;

// 通用 ARGB 画布（缓冲随对象删除释放，模式同 sp_ui 宠物画布）。
static void buf_free_evt(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static lv_obj_t *argb_canvas(lv_obj_t *parent, int32_t w, int32_t h)
{
    uint8_t *buf = malloc((size_t)w * h * 4);
    if (!buf) {
        return NULL;
    }
    memset(buf, 0, (size_t)w * h * 4);
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(cv, buf_free_evt, LV_EVENT_DELETE, buf);
    return cv;
}

static void put_px(uint8_t *px, int32_t w, int32_t x, int32_t y,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int o = (y * w + x) * 4;
    px[o + 0] = b;
    px[o + 1] = g;
    px[o + 2] = r;
    px[o + 3] = a;
}

static void blend_px(uint8_t *px, int32_t w, int32_t x, int32_t y,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int o = (y * w + x) * 4;
    if (px[o + 3] == 0) {
        put_px(px, w, x, y, r, g, b, a);
        return;
    }
    // 简化 alpha 混合（背景球体为不透明，仅内部叠加用）。
    uint32_t ia = 255u - a;
    px[o + 0] = (uint8_t)((b * a + px[o + 0] * ia) / 255);
    px[o + 1] = (uint8_t)((g * a + px[o + 1] * ia) / 255);
    px[o + 2] = (uint8_t)((r * a + px[o + 2] * ia) / 255);
    px[o + 3] = 255;
}

// 星图坐标 → 球内像素（0..99 → 半径 32 内）。
static void cst_px(uint8_t nx, uint8_t ny, int32_t *x, int32_t *y)
{
    *x = BALL_CX + ((int32_t)nx - 50) * 64 / 100;
    *y = BALL_CY + ((int32_t)ny - 50) * 64 / 100;
}

static float clamp01(float v)
{
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

// 星星 i 在 now 时的出现比例 0..1。
static float star_k(const intro_t *ix, uint32_t now, int i)
{
    if (!ix->intro) {
        return 1.0f;
    }
    return clamp01((float)(int32_t)(now - STAR_T0 - i * STAR_STEP) / 150.0f);
}

static void ball_paint(void)
{
    uint8_t *px = (uint8_t *)lv_canvas_get_buf(s_ix.ball);
    if (!px) {
        return;
    }
    uint32_t now = s_ix.t;

    // 球体亮度：推镜期间已亮，逐星阶段到满；驻留期呼吸。
    float glow;
    if (s_ix.intro) {
        float k = clamp01((float)(int32_t)(now - WIDE_T0) / (float)FADE_MS);
        glow = 0.75f + 0.25f * (k * k * (3.0f - 2.0f * k));
    } else {
        glow = 0.72f + 0.28f
               * sinf((float)(now % 2400) * 6.2832f / 2400.0f);
    }

    for (int32_t y = 0; y < BALL_W; y++) {
        for (int32_t x = 0; x < BALL_W; x++) {
            int32_t dx = x - BALL_CX;
            int32_t dy = y - BALL_CY;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 > BALL_R2 + 900) {
                continue;  // 光晕圈外保持透明
            }
            if (d2 > BALL_R2) {
                // 外圈光晕：随 glow 亮起、向外衰减。
                float k = 1.0f - (float)(d2 - BALL_R2) / 900.0f;
                uint8_t a = (uint8_t)(glow * k * 110.0f);
                put_px(px, BALL_W, x, y, 140, 170, 255, a);
                continue;
            }
            float t = (float)d2 / BALL_R2;  // 0 中心 → 1 边缘
            float core = 1.15f - 0.55f * t;
            uint8_t r = (uint8_t)((26 + 52 * glow) * core);
            uint8_t g = (uint8_t)((22 + 38 * glow) * core);
            uint8_t b = (uint8_t)((86 + 74 * glow) * core);
            // 左上玻璃高光。
            int32_t hx = dx + 14;
            int32_t hy = dy + 16;
            if (hx * hx + hy * hy < 110) {
                r = (uint8_t)(r + (255 - r) * 45 / 100);
                g = (uint8_t)(g + (255 - g) * 45 / 100);
                b = (uint8_t)(b + (255 - b) * 45 / 100);
            }
            put_px(px, BALL_W, x, y, r, g, b, 255);
        }
    }

    // 背景点缀星（天上）。
    for (size_t i = 0; i < SKY_N; i++) {
        float tw = 0.5f + 0.5f
                   * sinf((float)((now + (uint32_t)i * 730) % 2100)
                          * 6.2832f / 2100.0f);
        uint8_t a = (uint8_t)((36.0f + 70.0f * tw) * (0.35f + 0.65f * glow));
        blend_px(px, BALL_W, SKY_STARS[i][0], SKY_STARS[i][1],
                 200, 215, 255, a);
    }

    // 连线（逐段推进）。
    uint8_t n = CST_N[s_ix.sign];
    uint8_t ne = CST_NE[s_ix.sign];
    for (int e = 0; e < ne; e++) {
        float p = s_ix.intro
                  ? clamp01((float)(int32_t)(now - LINE_T0 - e * LINE_STEP)
                            / (float)LINE_DRAW)
                  : 1.0f;
        if (p <= 0) {
            continue;
        }
        int32_t ax, ay, bx, by;
        cst_px(CST_STARS[s_ix.sign][CST_EDGES[s_ix.sign][e][0]][0],
               CST_STARS[s_ix.sign][CST_EDGES[s_ix.sign][e][0]][1],
               &ax, &ay);
        cst_px(CST_STARS[s_ix.sign][CST_EDGES[s_ix.sign][e][1]][0],
               CST_STARS[s_ix.sign][CST_EDGES[s_ix.sign][e][1]][1],
               &bx, &by);
        int32_t ex = ax + (int32_t)((bx - ax) * p);
        int32_t ey = ay + (int32_t)((by - ay) * p);
        // DDA 逐像素。
        int32_t steps = abs(ex - ax) > abs(ey - ay) ? abs(ex - ax)
                                                    : abs(ey - ay);
        if (steps < 1) {
            steps = 1;
        }
        for (int32_t s = 0; s <= steps; s++) {
            int32_t lx = ax + (ex - ax) * s / steps;
            int32_t ly = ay + (ey - ay) * s / steps;
            blend_px(px, BALL_W, lx, ly, 255, 214, 130,
                     (uint8_t)(150 + 70 * glow));
        }
    }

    // 星座星（逐颗亮起，带十字光芒与闪烁）。
    for (int i = 0; i < n; i++) {
        float k = star_k(&s_ix, now, i);
        if (k <= 0) {
            continue;
        }
        float tw = 0.75f + 0.25f
                   * sinf((float)((now + (uint32_t)i * 617) % 1700)
                          * 6.2832f / 1700.0f);
        int32_t sx, sy;
        cst_px(CST_STARS[s_ix.sign][i][0], CST_STARS[s_ix.sign][i][1],
               &sx, &sy);
        uint8_t cr = (uint8_t)(255 * k);
        uint8_t cg = (uint8_t)((235 * tw) * k);
        uint8_t cb = (uint8_t)((180 * tw) * k);
        blend_px(px, BALL_W, sx, sy, cr, cg, cb, 255);
        if (k > 0.55f) {  // 出现后半段补十字光芒
            blend_px(px, BALL_W, sx + 1, sy, cr, cg, cb, 210);
            blend_px(px, BALL_W, sx - 1, sy, cr, cg, cb, 210);
            blend_px(px, BALL_W, sx, sy + 1, cr, cg, cb, 210);
            blend_px(px, BALL_W, sx, sy - 1, cr, cg, cb, 210);
        }
    }
    lv_obj_invalidate(s_ix.ball);
}

// 远景一帧：女巫（2x）+ 渐亮水晶球 + 扶球双手 + 环绕星光。
static void wide_paint(void)
{
    uint8_t *px = (uint8_t *)lv_canvas_get_buf(s_ix.wide);
    if (!px) {
        return;
    }
    memset(px, 0, WIDE_W * WIDE_H * 4);

    // 女巫 2x 放大绘制（画布内偏移 (38,8)，下半身被身前球体遮挡）。
    for (int32_t y = 0; y < WITCH_H; y++) {
        const char *row = WITCH_ART[y];
        for (int32_t x = 0; x < WITCH_W; x++) {
            char c = row[x];
            if (c == '.') {
                continue;
            }
            for (size_t p = 0; p < sizeof(WITCH_PAL) / sizeof(WITCH_PAL[0]);
                 p++) {
                if (WITCH_PAL[p].c == c) {
                    uint32_t argb = WITCH_PAL[p].argb;
                    uint8_t r = (uint8_t)(argb >> 16);
                    uint8_t g = (uint8_t)(argb >> 8);
                    uint8_t b = (uint8_t)argb;
                    int32_t bx = 38 + x * 2;
                    int32_t by = 8 + y * 2;
                    put_px(px, WIDE_W, bx, by, r, g, b, 255);
                    put_px(px, WIDE_W, bx + 1, by, r, g, b, 255);
                    put_px(px, WIDE_W, bx, by + 1, r, g, b, 255);
                    put_px(px, WIDE_W, bx + 1, by + 1, r, g, b, 255);
                    break;
                }
            }
        }
    }

    // 占卜球渐亮（远景阶段"球开始发光"）。
    uint32_t now = s_ix.t;
    float glow;
    if (s_ix.intro) {
        glow = clamp01((float)(int32_t)(now - 150) / 750.0f);
        glow = glow * glow * (3.0f - 2.0f * glow);
    } else {
        glow = 0.8f + 0.2f * sinf((float)(now % 2400) * 6.2832f / 2400.0f);
    }
    int32_t r2 = WIDE_BALL_R * WIDE_BALL_R;
    for (int32_t y = 0; y < WIDE_H; y++) {
        for (int32_t x = 0; x < WIDE_W; x++) {
            int32_t dx = x - WIDE_BALL_CX;
            int32_t dy = y - WIDE_BALL_CY;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 > r2 + 36) {
                continue;
            }
            if (d2 > r2) {  // 光晕圈
                float k = 1.0f - (float)(d2 - r2) / 36.0f;
                put_px(px, WIDE_W, x, y, 140, 170, 255,
                       (uint8_t)(glow * k * 90.0f));
                continue;
            }
            float t = (float)d2 / r2;
            float core = 1.2f - 0.5f * t;
            uint8_t r = (uint8_t)((24 + 60 * glow) * core);
            uint8_t g = (uint8_t)((20 + 44 * glow) * core);
            uint8_t b = (uint8_t)((80 + 90 * glow) * core);
            int32_t hx = dx + 5;
            int32_t hy = dy + 6;
            if (hx * hx + hy * hy < 12) {  // 左上玻璃高光
                r = (uint8_t)(r + (255 - r) * 45 / 100);
                g = (uint8_t)(g + (255 - g) * 45 / 100);
                b = (uint8_t)(b + (255 - b) * 45 / 100);
            }
            put_px(px, WIDE_W, x, y, r, g, b, 255);
        }
    }

    // 双手压在球缘（肤色块画在球体之后，形成"双手放在水晶球上"）。
    for (int32_t y = 46; y <= 50; y++) {
        for (int32_t x = 48; x <= 53; x++) {
            put_px(px, WIDE_W, x, y, 0xFF, 0xD9, 0xB3, 255);
        }
        for (int32_t x = 67; x <= 72; x++) {
            put_px(px, WIDE_W, x, y, 0xFF, 0xD9, 0xB3, 255);
        }
    }

    // 球亮起后环绕出现的星光闪点。
    static const int8_t SPK[6][2] = {
        { 0, -20 }, { 16, -13 }, { -18, -9 }, { 21, 3 }, { -14, 8 }, { 7, 20 },
    };
    for (int i = 0; i < 6; i++) {
        if (glow < 0.45f + 0.08f * i) {
            continue;
        }
        float tw = 0.5f + 0.5f
                   * sinf((float)((now + (uint32_t)i * 613) % 1300)
                          * 6.2832f / 1300.0f);
        blend_px(px, WIDE_W, WIDE_BALL_CX + SPK[i][0],
                 WIDE_BALL_CY + SPK[i][1], 255, 230, 160,
                 (uint8_t)(120 + 120 * tw));
    }
    lv_obj_invalidate(s_ix.wide);
}

// 动画结束（或跳过）：已开签则切正文，否则近景驻留并补开签文案。
static void intro_finish(void)
{
    s_ix.intro = false;
    if (!s_ix.scene) {
        return;
    }
    if (s_ix.wide) {  // 跳过时直接弃远景
        lv_obj_del(s_ix.wide);
        s_ix.wide = NULL;
    }
    if (s_ix.ball) {
        lv_obj_set_style_opa(s_ix.ball, 255, 0);
        lv_image_set_scale(s_ix.ball, 256);
    }
    if (s_opened) {
        // 清屏会释放场景画布，先摘除引用避免 tick 触碰悬垂指针。
        s_ix.scene = false;
        s_ix.ball = NULL;
        lv_obj_clean(s_page.scr);
        build_opened(s_page.scr, sp_state_pet());
    } else {
        add_closed_texts(s_page.scr);
    }
}

static void intro_build(lv_obj_t *scr, uint8_t sign)
{
    memset(&s_ix, 0, sizeof(s_ix));
    s_ix.intro = true;
    s_ix.scene = true;
    s_ix.sign = sign;
    s_ix.t = 0;

    s_ix.ball = argb_canvas(scr, BALL_W, BALL_W);
    if (!s_ix.ball) {
        s_ix.scene = false;  // 内存不足：放弃动画走静态页
        return;
    }
    lv_image_set_pivot(s_ix.ball, BALL_CX, BALL_CY);
    lv_obj_set_pos(s_ix.ball, 72, 52);

    s_ix.wide = argb_canvas(scr, WIDE_W, WIDE_H);
    if (s_ix.wide) {
        lv_obj_set_pos(s_ix.wide, 60, 46);
        lv_obj_set_style_opa(s_ix.ball, 0, 0);  // 远景在前，近景待推镜淡入
        lv_image_set_scale(s_ix.ball, 320);
    } else {
        lv_image_set_scale(s_ix.ball, 256);     // 无远景：直接近景
    }
}

static void tick_page(sp_page_t *page, uint32_t ms)
{
    (void)page;
    if (!s_ix.scene || !s_ix.ball) {
        return;
    }
    s_ix.t += ms;
    if (s_ix.intro && s_ix.t >= INTRO_MS) {
        intro_finish();
        return;
    }
    if (s_ix.wide) {
        wide_paint();
        if (s_ix.t >= WIDE_T0) {  // 推镜：远景淡出、近景放大淡入后回正
            float f = clamp01((float)(int32_t)(s_ix.t - WIDE_T0)
                              / (float)FADE_MS);
            lv_obj_set_style_opa(s_ix.wide, (lv_opa_t)(255 - 255 * f), 0);
            lv_obj_set_style_opa(s_ix.ball, (lv_opa_t)(255 * f), 0);
            lv_image_set_scale(s_ix.ball, (uint32_t)(320 - 64 * f));
            if (f >= 1.0f) {
                lv_obj_del(s_ix.wide);  // 推镜完成，释放远景内存
                s_ix.wide = NULL;
            }
        }
    }
    ball_paint();
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

    // 清屏会释放场景画布，先摘除引用避免 tick 触碰悬垂指针。
    s_ix.scene = false;
    s_ix.wide = NULL;
    s_ix.ball = NULL;
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
    }

    // 占卜开场动画（时钟有效且画布内存足够）；失败则退回静态页。
    if (pet && sp_clock_valid()) {
        intro_build(page->scr, pet->sign);
        if (s_ix.scene) {
            return;
        }
    }
    if (s_opened) {
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
    if (input->ev != SP_EV_CLICK) {
        return;
    }
    // 开场动画期间：任意单击跳过，不触发开签。
    if (s_ix.intro) {
        intro_finish();
        return;
    }
    if (input->btn != SP_BTN_OK) {
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
        .tick = tick_page,
    };
    return &s_page;
}
