// main/main.c —— 命定星宠（Fated Star Pet）应用入口。
//
// 职责仅限硬件初始化与框架启动；所有界面与玩法逻辑在 sp_* 模块中。
// 本应用完全重设计 UI，不复用基线硬件测试菜单与 ui_pixel 视觉壳。
#include "sp_app.h"
#include "sp_power.h"
#include "sp_scr_splash.h"
#include "sp_state.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "命定星宠启动");

    // 共享 I2C：ES8311(codec 0x18) 与 CW2017(电量计 0x63) 复用同一总线。
    bsp_i2c_init();

    // 显示是本应用唯一输出载体，失败则无法继续。
    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG,
                 "显示/LVGL 初始化失败：MOSI=%d SCLK=%d CS=%d DC=%d BL=%d",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 音频/电量单项失败不阻塞应用：音频任务与电量 UI 均有降级路径。
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败，语音与音效将不可用");
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，界面不显示电量百分比");
    }

    // 应用状态：NVS 存档、时钟、音频 worker（内部均有失败降级）。
    sp_state_init();

    // 息屏省电（30s 降背光 / 35s 轻睡眠），并注册按键钩子。
    sp_power_init();

    sp_app_start(sp_page_splash());
}
