#include "sp_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sp_record.h"

static const char *TAG = "sp_store";
static const char NS_APP[] = "sp";
static const char NS_WIFI[] = "spwifi";
static const char KEY_A[] = "rec_a";
static const char KEY_B[] = "rec_b";
static const char KEY_VOL[] = "vol";
static const char KEY_SSID[] = "ssid";
static const char KEY_PASS[] = "pass";

static QueueHandle_t s_save_queue;
static TaskHandle_t s_save_task;
static volatile uint32_t s_sequence;
static volatile bool s_dirty;   // 队列里有待落盘数据

static void save_task(void *arg)
{
    (void)arg;
    sp_pet_t pet;
    for (;;) {
        if (xQueueReceive(s_save_queue, &pet, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        uint32_t seq = ++s_sequence;
        sp_record_t rec;
        sp_record_from_pet(&pet, seq, &rec);
        const char *key = (seq & 1u) ? KEY_A : KEY_B;

        nvs_handle_t h;
        esp_err_t err = nvs_open(NS_APP, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_blob(h, key, rec.bytes, sizeof(rec.bytes));
            if (err == ESP_OK) err = nvs_commit(h);
            nvs_close(h);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        }
        // 落盘后若队列已空则清除忙标记（flush 用）。
        if (uxQueueMessagesWaiting(s_save_queue) == 0) {
            s_dirty = false;
        }
    }
}

bool sp_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery, erasing");
        if (nvs_flash_erase() != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return false;
    }
    if (s_save_queue) {
        return true;
    }
    s_save_queue = xQueueCreate(1, sizeof(sp_pet_t));
    if (!s_save_queue) {
        return false;
    }
    if (xTaskCreate(save_task, "sp_save", 3072, NULL, 3,
                    &s_save_task) != pdPASS) {
        vQueueDelete(s_save_queue);
        s_save_queue = NULL;
        s_save_task = NULL;
        return false;
    }
    return true;
}

static bool read_slot(nvs_handle_t h, const char *key, sp_record_t *out)
{
    size_t len = sizeof(out->bytes);
    esp_err_t err = nvs_get_blob(h, key, out->bytes, &len);
    return err == ESP_OK && len == sizeof(out->bytes) &&
           sp_record_validate(out);
}

bool sp_store_load_pet(sp_pet_t *out)
{
    if (!out) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NS_APP, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    sp_record_t a, b;
    bool a_ok = read_slot(h, KEY_A, &a);
    bool b_ok = read_slot(h, KEY_B, &b);
    nvs_close(h);

    const sp_record_t *chosen = sp_record_newer(a_ok ? &a : NULL,
                                                b_ok ? &b : NULL);
    if (!chosen) {
        return false;
    }
    s_sequence = sp_record_sequence(chosen);
    return sp_record_to_pet(chosen, out);
}

bool sp_store_save_pet(const sp_pet_t *pet)
{
    if (!s_save_queue || !pet) {
        return false;
    }
    s_dirty = true;
    // 覆盖式入队：只保留最新状态。
    return xQueueOverwrite(s_save_queue, pet) == pdPASS;
}

void sp_store_flush(void)
{
    if (!s_save_queue) {
        return;
    }
    // 通知机制：等 dirty 清零，最多 1.5s（一次 NVS 写入约 30..100ms）。
    for (int i = 0; i < 150 && s_dirty; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void sp_store_erase_all(void)
{
    sp_store_flush();
    nvs_handle_t h;
    if (nvs_open(NS_APP, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    if (nvs_open(NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    s_sequence = 0;
}

uint8_t sp_store_load_volume(void)
{
    nvs_handle_t h;
    uint8_t level = 1;  // 默认中低档
    if (nvs_open(NS_APP, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, KEY_VOL, &level) != ESP_OK) {
            level = 1;
        }
        nvs_close(h);
    }
    if (level >= SP_VOL_LEVELS) {
        level = 1;
    }
    return level;
}

void sp_store_save_volume(uint8_t level)
{
    if (level >= SP_VOL_LEVELS) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NS_APP, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, KEY_VOL, level);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool sp_store_wifi_save(const char *ssid, const char *pass)
{
    // 802.11 SSID 最长正好 32 字节，允许其通过（33+ 才非法）。
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > SP_WIFI_SSID_MAX) {
        return false;
    }
    if (!pass) {
        pass = "";
    }
    if (strlen(pass) >= SP_WIFI_PASS_MAX) {
        return false;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return false;
    }
    // 只记录长度与结果，绝不打印明文。
    err = nvs_set_str(h, KEY_SSID, ssid);
    if (err == ESP_OK && pass[0]) {
        err = nvs_set_str(h, KEY_PASS, pass);
    } else if (err == ESP_OK) {
        nvs_erase_key(h, KEY_PASS);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi creds save failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

static bool load_str(nvs_handle_t h, const char *key, char *buf, size_t cap)
{
    size_t need = cap;
    esp_err_t err = nvs_get_str(h, key, buf, &need);
    if (err != ESP_OK) {
        return false;
    }
    if (need > cap) {
        return false;
    }
    buf[cap - 1] = '\0';
    return true;
}

bool sp_store_wifi_load(char *ssid, size_t ssid_cap,
                        char *pass, size_t pass_cap)
{
    if (!ssid || ssid_cap == 0) {
        return false;
    }
    ssid[0] = '\0';
    if (pass && pass_cap) {
        pass[0] = '\0';
    }
    nvs_handle_t h;
    if (nvs_open(NS_WIFI, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = load_str(h, KEY_SSID, ssid, ssid_cap);
    if (ok && pass && pass_cap) {
        (void)load_str(h, KEY_PASS, pass, pass_cap);  // 无密码属正常
    }
    nvs_close(h);
    return ok;
}

void sp_store_wifi_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}
