// main/sp_app.c —— 命定星宠：应用框架实现（按键队列 + 单 lv_timer 派发 + 换页）。
#include "sp_app.h"

#include "bsp_button.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#define APP_TAG "sp_app"
#define INPUT_QUEUE_DEPTH 8
#define PUMP_PERIOD_MS 20

static QueueHandle_t s_input_queue;
static lv_timer_t *s_pump_timer;
static sp_page_t *s_current;
static sp_page_t *s_pending;
static int64_t s_last_pump_us;
static void (*s_input_hook)(const sp_input_t *input);

// 按键回调：运行于共享 esp_timer 任务，只入队、立即返回。
static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_queue) {
        return;
    }
    const sp_input_t input = {
        .btn = (sp_btn_t)btn,
        .ev = (sp_btn_ev_t)ev,
    };
    // 队列满时丢弃最旧事件，保证新按键不被堵死（快速连按场景）。
    if (xQueueSend(s_input_queue, &input, 0) != pdTRUE) {
        sp_input_t dropped;
        if (xQueueReceive(s_input_queue, &dropped, 0) == pdTRUE) {
            (void)xQueueSend(s_input_queue, &input, 0);
        }
    }
}

static void switch_page(sp_page_t *page)
{
    sp_page_t *old = s_current;
    if (old != NULL) {
        if (old->exit != NULL) {
            old->exit(old);
        }
        if (old->scr != NULL) {
            lv_obj_delete(old->scr);
            old->scr = NULL;
        }
    }

    s_current = page;
    if (page == NULL) {
        return;
    }
    page->scr = lv_obj_create(NULL);
    if (page->enter != NULL) {
        page->enter(page);
    }
    lv_screen_load(page->scr);
}

static void apply_pending(void)
{
    if (s_pending == NULL) {
        return;
    }
    sp_page_t *next = s_pending;
    s_pending = NULL;
    switch_page(next);
    // 换页瞬间队列里的残余按键属于旧页面上下文，全部作废，防止误触新页面。
    if (s_input_queue) {
        xQueueReset(s_input_queue);
    }
}

// LVGL 任务中的唯一驱动循环：排空按键队列 → 页面 tick → 处理换页。
static void pump_cb(lv_timer_t *timer)
{
    (void)timer;
    int64_t now_us = esp_timer_get_time();
    uint32_t dt_ms = (uint32_t)((now_us - s_last_pump_us) / 1000);
    s_last_pump_us = now_us;

    sp_input_t input;
    while (xQueueReceive(s_input_queue, &input, 0) == pdTRUE) {
        if (s_pending != NULL) {
            break;  // 已请求换页，旧页面的后续按键全部丢弃。
        }
        if (s_input_hook != NULL) {
            s_input_hook(&input);
        }
        if (s_current != NULL && s_current->key != NULL) {
            s_current->key(s_current, &input);
        }
    }

    if (s_pending == NULL && s_current != NULL && s_current->tick != NULL) {
        s_current->tick(s_current, dt_ms);
    }

    apply_pending();
}

void sp_app_goto(sp_page_t *new_page)
{
    // 仅登记；实际换页在 LVGL 任务的 pump 中完成。
    s_pending = new_page;
}

sp_page_t *sp_app_current(void)
{
    return s_current;
}

uint32_t sp_app_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void sp_app_set_input_hook(void (*hook)(const sp_input_t *input))
{
    s_input_hook = hook;
}

void sp_app_start(sp_page_t *first_page)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(sp_input_t));
    if (s_input_queue == NULL) {
        ESP_LOGE(APP_TAG, "按键队列创建失败");
        return;
    }

    esp_err_t err = bsp_button_init(on_button, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "按键初始化失败: %s", esp_err_to_name(err));
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(APP_TAG, "LVGL 加锁失败，应用无法启动");
        return;
    }
    s_last_pump_us = esp_timer_get_time();
    s_pump_timer = lv_timer_create(pump_cb, PUMP_PERIOD_MS, NULL);
    switch_page(first_page);
    bsp_lvgl_unlock();
    ESP_LOGI(APP_TAG, "命定星宠应用已启动");
}
