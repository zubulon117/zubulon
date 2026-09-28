// main/sp_app.h —— 命定星宠：应用框架（页面导航 + 按键分发）。
//
// 设计（借鉴 demo/tamagezi 的已验证模式，全部为本应用重写）：
//   * 按键回调运行于共享 esp_timer 任务，只负责把事件投入队列；
//   * 一个 20ms lv_timer（运行在 LVGL 任务）排空队列并派发事件给当前页；
//   * 因此页面回调内可直接操作 LVGL，无需 bsp_lvgl_lock；
//   * 页面切换只能在 LVGL 任务里发生：sp_app_goto() 仅登记请求，
//     由 pump 在当前事件派发结束后统一换页（先 exit 旧页、删旧屏，再 enter 新页）。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

// 与 bsp_button 的三键一一对应，应用层使用自己的类型以与驱动解耦。
typedef enum {
    SP_BTN_UP = 0,
    SP_BTN_DOWN,
    SP_BTN_OK,
} sp_btn_t;

typedef enum {
    SP_EV_PRESS = 0,  // 按下瞬间（即时反馈用）
    SP_EV_CLICK,      // 单击
    SP_EV_DOUBLE,     // 双击
    SP_EV_LONG,       // 长按
} sp_btn_ev_t;

typedef struct {
    sp_btn_t btn;
    sp_btn_ev_t ev;
} sp_input_t;

typedef struct sp_page sp_page_t;

// 页面契约：
//   enter：在 page->scr 上构建 UI（框架已先建好空白屏幕）。
//   exit ：停止本页拥有的定时器/任务；框架随后删除 page->scr，不要自己删屏。
//   key  ：处理一个按键事件；可调用 sp_app_goto() 请求换页。
//   tick ：每 20ms 一次的帧心跳（可空）；ms 为距上次 tick 的毫秒数。
// 各回调全部运行在 LVGL 任务。
struct sp_page {
    const char *name;
    void (*enter)(sp_page_t *page);
    void (*exit)(sp_page_t *page);
    void (*key)(sp_page_t *page, const sp_input_t *input);
    void (*tick)(sp_page_t *page, uint32_t ms);
    void *ctx;
    lv_obj_t *scr;
};

// 启动框架：注册按键回调、创建 pump 定时器并装载首页。
// 必须在显示/LVGL 初始化成功后调用；内部自行处理 LVGL 加锁。
void sp_app_start(sp_page_t *first_page);

// 请求切换到 new_page（延迟到当前 key/tick 返回后生效）。
// 可在页面 key/tick 回调中安全调用；重复调用以最后一次为准。
void sp_app_goto(sp_page_t *new_page);

// 返回当前页面（仅 LVGL 任务中有效）。
sp_page_t *sp_app_current(void);

// 距启动的毫秒数（esp_timer 包装，便于页面做动画计时）。
uint32_t sp_app_millis(void);

// 注册按键钩子：每个入队的按键事件在派发给页面之前调用一次
// （运行于 LVGL 任务，供省电模块重置空闲计时）。
void sp_app_set_input_hook(void (*hook)(const sp_input_t *input));
