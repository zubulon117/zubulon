#include "sp_power.h"

#include "bsp_display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

#include "sp_app.h"
#include "sp_audio.h"
#include "sp_state.h"
#include "sp_wifi.h"

#define TAG "sp_power"

#define DIM_AFTER_MS     30000
#define SLEEP_AFTER_MS   35000
#define POLL_MS          500
#define DIM_PERCENT      20
#define FULL_PERCENT     100
#define POWER_TASK_STACK 3072

// 三键 ADC 共用 GPIO0，按下时节点被拉低。
// 注意：ESP32-C3 无 ext0 唤醒源，轻睡眠使用 gpio 低电平唤醒。
#define WAKE_GPIO GPIO_NUM_0

static TaskHandle_t s_task;
static volatile uint32_t s_last_activity_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void enter_light_sleep(void)
{
    ESP_LOGI(TAG, "enter light sleep");
    sp_state_flush();
    sp_audio_sleep();
    bsp_display_backlight(0);

    // GPIO0 低电平唤醒（板外已有 10k 上拉，任意键按下都会拉低节点）。
    gpio_wakeup_enable(WAKE_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    // 唤醒源在睡眠后保留即可；按键走 ADC 轮询，不依赖该数字中断配置。
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

    bsp_display_backlight(FULL_PERCENT);
    sp_audio_wake();
    s_last_activity_ms = now_ms();
    ESP_LOGI(TAG, "woke up");
}

static void power_task(void *arg)
{
    (void)arg;
    bool dimmed = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_MS));
        uint32_t idle = now_ms() - s_last_activity_ms;

        if (idle < DIM_AFTER_MS) {
            if (dimmed) {
                bsp_display_backlight(FULL_PERCENT);
                dimmed = false;
            }
            continue;
        }
        if (idle < SLEEP_AFTER_MS) {
            if (!dimmed) {
                bsp_display_backlight(DIM_PERCENT);
                dimmed = true;
            }
            continue;
        }
        if (!dimmed) {
            bsp_display_backlight(DIM_PERCENT);
            dimmed = true;
        }
        if (sp_wifi_busy()) {
            // 连接/SNTP 进行中：保持暗屏但不冻结射频，下个轮询再判断。
            continue;
        }
        dimmed = false;
        enter_light_sleep();
    }
}

static void input_hook(const sp_input_t *input)
{
    (void)input;
    sp_power_kick();
}

void sp_power_kick(void)
{
    s_last_activity_ms = now_ms();
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

void sp_power_init(void)
{
    s_last_activity_ms = now_ms();
    xTaskCreate(power_task, "sp_power", POWER_TASK_STACK, NULL, 4,
                &s_task);
    sp_app_set_input_hook(input_hook);
}
