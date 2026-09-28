// main/sp_page_settings.c —— 设置：日期时间、Wi-Fi 校时、音量、出厂复位、关于。
#include "sp_page_settings.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "sp_audio.h"
#include "sp_clock.h"
#include "sp_state.h"
#include "sp_store.h"
#include "sp_ui.h"
#include "sp_wifi.h"

#include "sp_page_home.h"
#include "sp_page_wizard.h"

enum {
    MODE_MENU = 0,
    MODE_VOLUME,
    MODE_FACTORY,
    MODE_ABOUT,
    MODE_WIFI_SCANNING,
    MODE_WIFI_LIST,
    MODE_WIFI_PASS,
    MODE_WIFI_CONNECT,
};

static const char *SET_ITEMS[] = {
    "日期时间",
    "Wi-Fi 校时",
    "音量",
    "恢复出厂",
    "关于",
};

static const char *VOL_NAMES[] = { "静音", "轻", "中", "响亮" };

#define AP_LABEL_LEN 40
#define PASS_CHARSET \
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" \
    " -_.,!@#$%&*+=?/"

typedef struct {
    uint8_t mode;
    sp_ui_menu_t menu;
    sp_ui_typebox_t typebox;

    spwifi_ap_t aps[SPWIFI_SCAN_MAX];
    size_t ap_count;
    char ap_labels[SPWIFI_SCAN_MAX][AP_LABEL_LEN];
    const char *ap_label_ptrs[SPWIFI_SCAN_MAX];
    char pass_buf[SP_WIFI_PASS_MAX];

    volatile bool scan_ready;
    volatile size_t scan_count;
    volatile spwifi_status_t run_status;
    volatile bool run_ready;
    bool scan_failed;

    lv_obj_t *status_lab;
    uint32_t done_ms;
} set_ctx_t;

static sp_page_t s_page;
static set_ctx_t s_c;

// 每次扫描/连接自增的代际号：取消后旧 worker 的迟到回调按代际丢弃。
static uint32_t s_scan_seq;
static uint32_t s_run_seq;

// ------------------------------------------------- worker -> LVGL 交接 ---
static void scan_cb(const spwifi_ap_t *aps, size_t count, void *arg)
{
    uint32_t gen = (uint32_t)(intptr_t)arg;
    if (gen != s_scan_seq) {
        return;   // 已被取消/取代的旧扫描
    }
    size_t n = count > SPWIFI_SCAN_MAX ? SPWIFI_SCAN_MAX : count;
    for (size_t i = 0; i < n; i++) {
        s_c.aps[i] = aps[i];
    }
    s_c.scan_count = n;
    s_c.scan_ready = true;
}

static void run_cb(spwifi_status_t status, void *arg)
{
    uint32_t gen = (uint32_t)(intptr_t)arg;
    if (gen != s_run_seq) {
        return;   // 已被取消/取代的旧连接
    }
    s_c.run_status = status;
    s_c.run_ready = true;
}

// ------------------------------------------------------------- 渲染 ---
static lv_obj_t *add_title(lv_obj_t *scr, const char *text)
{
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(t, SP_C_GOLD, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    return t;
}

static lv_obj_t *add_hint(lv_obj_t *scr, const char *text)
{
    lv_obj_t *h = lv_label_create(scr);
    lv_label_set_text(h, text);
    lv_obj_set_style_text_font(h, SP_FONT_SMALL, 0);
    lv_obj_set_style_text_color(h, SP_C_DIM, 0);
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -12);
    return h;
}

static void build_menu(void);
static void build_volume(void);
static void build_wifi_list(void);

static void render(void)
{
    lv_obj_t *scr = s_page.scr;
    sp_ui_style_screen(scr);
    lv_obj_clean(scr);

    switch (s_c.mode) {
    case MODE_MENU:
        build_menu();
        break;
    case MODE_VOLUME:
        build_volume();
        break;
    case MODE_FACTORY: {
        add_title(scr, "恢复出厂");
        lv_obj_t *b = lv_label_create(scr);
        lv_label_set_text(b, "将清除星宠档案、网络与设置\n此操作不可恢复");
        lv_obj_set_style_text_font(b, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(b, SP_C_WARN, 0);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(b, LV_ALIGN_CENTER, 0, -10);
        add_hint(scr, "确认清除  长按取消");
        break;
    }
    case MODE_ABOUT: {
        add_title(scr, "关于");
        lv_obj_t *b = lv_label_create(scr);
        lv_label_set_text(b,
            "命定星宠\n星座离线养成电子宠物\n\n"
            "每日占卜离线生成\n语音播报无需联网");
        lv_obj_set_style_text_font(b, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(b, SP_C_MIST, 0);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(b, LV_ALIGN_CENTER, 0, 0);
        add_hint(scr, "长按返回");
        break;
    }
    case MODE_WIFI_SCANNING: {
        add_title(scr, "Wi-Fi 校时");
        lv_obj_t *b = lv_label_create(scr);
        if (s_c.scan_failed) {
            lv_label_set_text(b, "无线启动失败\n请稍后再试");
            lv_obj_set_style_text_color(b, SP_C_WARN, 0);
        } else {
            lv_label_set_text(b, "正在扫描热点…");
            lv_obj_set_style_text_color(b, SP_C_MIST, 0);
        }
        lv_obj_set_style_text_font(b, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(b, LV_ALIGN_CENTER, 0, 0);
        add_hint(scr, "长按返回");
        break;
    }
    case MODE_WIFI_LIST:
        add_title(scr, "选择热点");
        build_wifi_list();
        add_hint(scr, "上下选择 确认 长按返回");
        break;
    case MODE_WIFI_PASS: {
        add_title(scr, s_c.aps[s_c.menu.index].ssid);
        lv_obj_t *holder = lv_obj_create(scr);
        lv_obj_remove_style_all(holder);
        lv_obj_set_size(holder, 228, 120);
        lv_obj_align(holder, LV_ALIGN_CENTER, 0, 10);
        lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
        sp_ui_typebox_init(&s_c.typebox, holder, s_c.pass_buf,
                           sizeof(s_c.pass_buf), PASS_CHARSET,
                           NULL, NULL);
        break;
    }
    case MODE_WIFI_CONNECT: {
        add_title(scr, "Wi-Fi 校时");
        s_c.status_lab = lv_label_create(scr);
        lv_label_set_text(s_c.status_lab, "正在连接热点…");
        lv_obj_set_style_text_font(s_c.status_lab, SP_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_c.status_lab, SP_C_MIST, 0);
        lv_obj_set_style_text_align(s_c.status_lab,
                                    LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_c.status_lab, LV_ALIGN_CENTER, 0, 0);
        add_hint(scr, "长按取消");
        break;
    }
    default:
        break;
    }
}

// --------------------------------------------------------------- 菜单 ---
static void set_pick(uint8_t index, void *arg)
{
    (void)arg;
    sp_audio_earcon(SP_EAR_SELECT);
    switch (index) {
    case 0:   // 日期时间
        sp_wizard_set_next(sp_page_settings());
        sp_app_goto(sp_page_datetime());
        break;
    case 1: { // Wi-Fi
        s_c.mode = MODE_WIFI_SCANNING;
        s_c.scan_ready = false;
        s_c.scan_count = 0;
        s_c.scan_failed = false;
        render();
        s_scan_seq++;
        if (!sp_wifi_scan(scan_cb, (void *)(intptr_t)s_scan_seq)) {
            // 已有无线任务在跑或资源不足：给出失败兜底，避免死等。
            s_c.scan_failed = true;
            render();
        }
        break;
    }
    case 2:
        s_c.mode = MODE_VOLUME;
        render();
        break;
    case 3:
        s_c.mode = MODE_FACTORY;
        render();
        break;
    default:
        s_c.mode = MODE_ABOUT;
        render();
        break;
    }
}

static void set_back(uint8_t index, void *arg)
{
    (void)index;
    (void)arg;
    sp_audio_earcon(SP_EAR_CLICK);
    sp_app_goto(sp_page_menu());
}

static void build_menu(void)
{
    add_title(s_page.scr, "设置");
    lv_obj_t *holder = lv_obj_create(s_page.scr);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, 200, 200);
    lv_obj_align(holder, LV_ALIGN_CENTER, 0, 18);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    sp_ui_menu_init(&s_c.menu, holder, SET_ITEMS, 5, set_pick, set_back,
                    NULL);
    add_hint(s_page.scr, "上下选择 确认 长按返回");
}

static void build_volume(void)
{
    lv_obj_t *v = lv_label_create(s_page.scr);
    uint8_t level = sp_state_volume();
    lv_label_set_text_fmt(v, "音量  %s", VOL_NAMES[level]);
    lv_obj_set_style_text_font(v, SP_FONT_BIG, 0);
    lv_obj_set_style_text_color(v, SP_C_GOLD, 0);
    lv_obj_align(v, LV_ALIGN_CENTER, 0, -10);
    add_hint(s_page.scr, "上下调整 确认或长按返回");
}

// ------------------------------------------------------------- Wi-Fi ---
static void build_wifi_list(void)
{
    for (size_t i = 0; i < s_c.ap_count; i++) {
        snprintf(s_c.ap_labels[i], AP_LABEL_LEN, "%s%s",
                 s_c.aps[i].ssid, s_c.aps[i].open ? "" : " 锁");
        s_c.ap_label_ptrs[i] = s_c.ap_labels[i];
    }
    lv_obj_t *holder = lv_obj_create(s_page.scr);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, 220, 220);
    lv_obj_align(holder, LV_ALIGN_CENTER, 0, 20);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);

    // on_pick/on_back 传 NULL：列表的确认/长按在 key_page 内联处理
    // （确认时要区分开放/加密网络）。
    sp_ui_menu_init(&s_c.menu, holder, s_c.ap_label_ptrs,
                    (uint8_t)s_c.ap_count, NULL, NULL, NULL);
}

static void start_connect(void)
{
    uint8_t idx = s_c.menu.index;
    const char *ssid = s_c.aps[idx].ssid;
    const char *pass = s_c.aps[idx].open ? "" : s_c.pass_buf;

    s_c.mode = MODE_WIFI_CONNECT;
    s_c.run_ready = false;
    s_c.done_ms = 0;
    render();
    s_run_seq++;
    uint32_t gen = s_run_seq;
    if (!sp_wifi_run(ssid, pass, run_cb, (void *)(intptr_t)gen)) {
        lv_label_set_text(s_c.status_lab, "启动失败");
        sp_audio_earcon(SP_EAR_FAIL);
        s_c.done_ms = sp_app_millis();
    }
}

static void ap_picked(void)
{
    uint8_t idx = s_c.menu.index;
    if (idx >= s_c.ap_count) {
        return;
    }
    sp_audio_earcon(SP_EAR_SELECT);
    if (s_c.aps[idx].open) {
        s_c.pass_buf[0] = '\0';   // 开放网络不带密码，避免残留上一热点密码
        start_connect();
    } else {
        s_c.pass_buf[0] = '\0';
        s_c.mode = MODE_WIFI_PASS;
        render();
    }
}

// ----------------------------------------------------------- 页面回调 ---
static void enter_page(sp_page_t *page)
{
    memset(&s_c, 0, sizeof(s_c));
    s_c.mode = MODE_MENU;
    render();
}

static void key_page(sp_page_t *page, const sp_input_t *input)
{
    (void)page;
    switch (s_c.mode) {
    case MODE_MENU:
        sp_ui_menu_key(&s_c.menu, input);
        break;

    case MODE_VOLUME:
        if (input->ev == SP_EV_CLICK &&
            (input->btn == SP_BTN_UP || input->btn == SP_BTN_DOWN)) {
            uint8_t v = sp_state_volume();
            if (input->btn == SP_BTN_UP && v < SP_VOL_LEVELS - 1) {
                v++;
            } else if (input->btn == SP_BTN_DOWN && v > 0) {
                v--;
            }
            sp_state_set_volume(v);
            sp_audio_earcon(SP_EAR_CLICK);
            render();
        } else if (input->ev == SP_EV_CLICK && input->btn == SP_BTN_OK) {
            sp_app_goto(sp_page_menu());
        } else if (input->ev == SP_EV_LONG) {
            sp_app_goto(sp_page_menu());
        }
        break;

    case MODE_FACTORY:
        if (input->ev == SP_EV_CLICK && input->btn == SP_BTN_OK) {
            sp_state_factory_reset();   // 不返回
        } else if (input->ev == SP_EV_LONG) {
            s_c.mode = MODE_MENU;
            render();
        }
        break;

    case MODE_ABOUT:
        if (input->ev == SP_EV_LONG) {
            s_c.mode = MODE_MENU;
            render();
        }
        break;

    case MODE_WIFI_SCANNING:
        if (input->ev == SP_EV_LONG) {
            s_c.mode = MODE_MENU;
            render();
        }
        break;

    case MODE_WIFI_LIST:
        if (input->ev == SP_EV_LONG) {
            s_c.mode = MODE_MENU;
            render();
        } else if (input->ev == SP_EV_CLICK) {
            // 复用菜单的焦点移动。
            sp_ui_menu_key(&s_c.menu, input);
            if (input->btn == SP_BTN_OK) {
                ap_picked();
            }
        }
        break;

    case MODE_WIFI_PASS:
        // 输入完成由 typebox 完成回调处理：这里在完成时启动连接。
        if (input->ev == SP_EV_LONG) {
            // 长按既可能是"完成"也可能是返回：输入框有内容视为完成，
            // 空内容视为返回。
            if (s_c.pass_buf[0]) {
                sp_ui_typebox_key(&s_c.typebox, input);
                if (s_c.typebox.finished) {
                    start_connect();
                }
            } else {
                s_c.mode = MODE_WIFI_LIST;
                render();
            }
        } else {
            sp_ui_typebox_key(&s_c.typebox, input);
            if (s_c.typebox.finished) {
                start_connect();
            }
        }
        break;

    case MODE_WIFI_CONNECT:
        if (input->ev == SP_EV_LONG) {
            s_c.mode = MODE_MENU;
            render();
        }
        break;

    default:
        break;
    }
}

static const char *run_text(spwifi_status_t st)
{
    switch (st) {
    case SPWIFI_CONNECTING: return "正在连接热点…";
    case SPWIFI_GOT_IP:     return "已联网，正在校时…";
    case SPWIFI_SYNCED:     return "校时成功";
    case SPWIFI_AUTH_FAIL:  return "密码错误";
    case SPWIFI_NO_AP:      return "找不到热点";
    case SPWIFI_TIMEOUT:    return "连接超时";
    default:                return "未知状态";
    }
}

static void tick_page(sp_page_t *page, uint32_t ms)
{
    (void)page;
    (void)ms;

    if (s_c.mode == MODE_WIFI_SCANNING && s_c.scan_ready) {
        s_c.ap_count = s_c.scan_count;
        s_c.scan_ready = false;
        if (s_c.scan_count == 0) {
            // 无热点：回到菜单（简单处理）。
            s_c.mode = MODE_MENU;
            render();
        } else {
            s_c.mode = MODE_WIFI_LIST;
            render();
        }
    }

    if (s_c.mode == MODE_WIFI_CONNECT && s_c.run_ready) {
        spwifi_status_t st = (spwifi_status_t)s_c.run_status;
        s_c.run_ready = false;
        if (s_c.status_lab) {
            lv_label_set_text(s_c.status_lab, run_text(st));
        }
        if (st == SPWIFI_SYNCED) {
            uint8_t idx = s_c.menu.index;
            sp_store_wifi_save(s_c.aps[idx].ssid,
                               s_c.aps[idx].open ? "" : s_c.pass_buf);
            sp_audio_earcon(SP_EAR_SUCCESS);
            s_c.done_ms = sp_app_millis();
        } else if (st == SPWIFI_AUTH_FAIL || st == SPWIFI_NO_AP ||
                   st == SPWIFI_TIMEOUT) {
            sp_audio_earcon(SP_EAR_FAIL);
            s_c.done_ms = sp_app_millis();
        }
    }

    // 终态停留 2s 后回菜单。
    if (s_c.mode == MODE_WIFI_CONNECT && s_c.done_ms &&
        sp_app_millis() - s_c.done_ms > 2000) {
        s_c.done_ms = 0;
        s_c.mode = MODE_MENU;
        render();
    }
}

sp_page_t *sp_page_settings(void)
{
    s_page = (sp_page_t) {
        .name = "settings",
        .enter = enter_page,
        .key = key_page,
        .tick = tick_page,
    };
    return &s_page;
}
