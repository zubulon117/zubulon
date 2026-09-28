#include "sp_state.h"

#include <string.h>

#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sp_audio.h"
#include "sp_clock.h"
#include "sp_store.h"
#include "sp_wifi.h"

static const char *TAG = "sp_state";

static sp_pet_t s_pet;
static bool s_has_pet;
static uint8_t s_volume = 1;
static uint16_t s_dex;
static uint32_t s_last_batt_ms;
static int s_batt_soc = -1;

void sp_state_init(void)
{
    sp_store_init();
    sp_clock_init();

    s_volume = sp_store_load_volume();
    s_dex = sp_store_load_dex();
    sp_audio_start(s_volume);

    if (sp_store_load_pet(&s_pet)) {
        s_has_pet = true;
        sp_date_t today;
        if (sp_clock_today(&today)) {
            uint32_t crossed = sp_pet_advance_day(&s_pet, today);
            if (crossed) {
                sp_state_save();
            }
        }
        ESP_LOGI(TAG, "pet loaded: sign=%u bond=%u stage=%u",
                 s_pet.sign, s_pet.bond, s_pet.stage);
    } else {
        s_has_pet = false;
    }

    int soc = bsp_battery_soc();
    if (soc >= 0) {
        s_batt_soc = soc;
    }
    s_last_batt_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // 冷启动 RTC 已归零（NVS 恢复只是近似值）：有已存凭据就后台联网校时，
    // 完成后由页面 enter 时的 sp_state_catchup() 自然吸收跨天变化。
    char ssid[SP_WIFI_SSID_MAX + 1];
    char pass[SP_WIFI_PASS_MAX + 1];
    if (sp_store_wifi_load(ssid, sizeof(ssid), pass, sizeof(pass)) &&
        sp_wifi_run(ssid, pass, NULL, NULL)) {
        ESP_LOGI(TAG, "boot time sync started");
    }
}

bool sp_state_has_pet(void)
{
    return s_has_pet;
}

void sp_state_create_pet(sp_date_t birthday, sp_date_t today)
{
    sp_pet_new(&s_pet, birthday, today);
    s_has_pet = true;
    sp_store_save_pet(&s_pet);
    sp_store_flush();
}

sp_pet_t *sp_state_pet(void)
{
    return s_has_pet ? &s_pet : NULL;
}

void sp_state_save(void)
{
    if (s_has_pet) {
        sp_store_save_pet(&s_pet);
    }
}

void sp_state_flush(void)
{
    sp_store_flush();
}

bool sp_state_today(sp_date_t *out)
{
    return sp_clock_today(out);
}

uint32_t sp_state_catchup(void)
{
    if (!s_has_pet) {
        return 0;
    }
    sp_date_t today;
    if (!sp_clock_today(&today)) {
        return 0;
    }
    uint32_t crossed = sp_pet_advance_day(&s_pet, today);
    if (crossed) {
        sp_state_save();
    }
    return crossed;
}

uint8_t sp_state_volume(void)
{
    return s_volume;
}

void sp_state_set_volume(uint8_t level)
{
    if (level >= SP_VOL_LEVELS) {
        return;
    }
    s_volume = level;
    sp_audio_set_level(level);
    sp_store_save_volume(level);
}

int sp_state_battery(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_batt_ms > 2000) {
        int soc = bsp_battery_soc();
        if (soc >= 0) {
            s_batt_soc = soc;
        }
        s_last_batt_ms = now;
    }
    return s_batt_soc;
}

uint16_t sp_state_dex(void)
{
    return s_dex;
}

bool sp_state_dex_visit(uint8_t sign)
{
    if (sp_dex_has(s_dex, sign)) {
        return false;
    }
    s_dex = sp_dex_visit(s_dex, sign);
    sp_store_save_dex(s_dex);
    return true;
}

void sp_state_factory_reset(void)
{
    sp_audio_stop();
    sp_store_erase_all();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}
