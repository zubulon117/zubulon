#include "sp_wifi.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "sp_clock.h"

static const char *TAG = "sp_wifi";

// 扫描与连接共用同一 worker 串行位：同一时刻只允许一个无线任务，
// 杜绝两套协议栈并发 init/deinit；省电层据此推迟轻睡眠。
static volatile bool s_busy;

#define CONNECT_TIMEOUT_MS 15000u
#define SNTP_TIMEOUT_MS    20000u
#define SCAN_TIMEOUT_MS    12000u

#define BIT_GOT_IP   (1u << 0)
#define BIT_AUTH     (1u << 1)
#define BIT_NO_AP    (1u << 2)
#define BIT_OTHER    (1u << 3)

typedef struct {
    char ssid[33];   // 802.11 SSID 最长 32 字节 + '\0'
    char pass[64];
    spwifi_cb_t cb;
    void *arg;
} wifi_job_t;

typedef struct {
    spwifi_scan_cb_t cb;
    void *arg;
} scan_job_t;

static void notify(spwifi_status_t st, spwifi_cb_t cb, void *arg)
{
    if (cb) {
        cb(st, arg);
    }
}

// ------------------------------------------------- 协议栈最小生命周期 ---
// 扫描与连接共用同一套"起 netif+STA → 停"流程，保证退出即关射频。

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    EventGroupHandle_t bits = arg;
    if (base != WIFI_EVENT || id != WIFI_EVENT_STA_DISCONNECTED) {
        return;
    }
    wifi_event_sta_disconnected_t *ev = data;
    ESP_LOGI(TAG, "disconnected reason=%d", ev->reason);
    uint32_t bit;
    switch (ev->reason) {
    case WIFI_REASON_NO_AP_FOUND:
        bit = BIT_NO_AP;
        break;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        bit = BIT_AUTH;
        break;
    default:
        bit = BIT_OTHER;
        break;
    }
    xEventGroupSetBits(bits, bit);
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base; (void)id; (void)data;
    xEventGroupSetBits((EventGroupHandle_t)arg, BIT_GOT_IP);
}

typedef struct {
    bool netif_inited;
    bool loop_inited;
    esp_netif_t *sta;
    bool wifi_started;
} stack_t;

static esp_err_t stack_up(stack_t *st)
{
    memset(st, 0, sizeof(*st));
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        st->netif_inited = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err == ESP_OK) {
        st->loop_inited = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    st->sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg);
}

static void stack_down(stack_t *st)
{
    if (st->wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    esp_wifi_deinit();
    if (st->sta) {
        esp_netif_destroy_default_wifi(st->sta);
    }
    if (st->loop_inited) {
        esp_event_loop_delete_default();
    }
    if (st->netif_inited) {
        esp_netif_deinit();
    }
    memset(st, 0, sizeof(*st));
}

// ------------------------------------------------------------- 扫描 ---
static int ap_cmp(const void *a, const void *b)
{
    const wifi_ap_record_t *x = a, *y = b;
    return (int)y->rssi - (int)x->rssi;
}

static void scan_task(void *arg)
{
    scan_job_t job = *(scan_job_t *)arg;
    free(arg);

    spwifi_ap_t out[SPWIFI_SCAN_MAX];
    memset(out, 0, sizeof(out));   // 保证 32 字节 SSID 也 NUL 终止
    size_t n = 0;
    stack_t st;

    if (stack_up(&st) != ESP_OK) {
        ESP_LOGE(TAG, "scan stack up failed");
        job.cb(out, 0, job.arg);
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK &&
        esp_wifi_start() == ESP_OK) {
        st.wifi_started = true;
        wifi_scan_config_t sc = {
            .ssid = NULL, .bssid = NULL, .channel = 0,
            .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };
        sc.scan_time.active.min = 60;
        sc.scan_time.active.max = 180;
        esp_err_t err = esp_wifi_scan_start(&sc, true);
        if (err == ESP_OK) {
            uint16_t found = 0;
            esp_wifi_scan_get_ap_num(&found);
            ESP_LOGI(TAG, "scan found=%u", found);
            if (found > 0) {
                wifi_ap_record_t *all = calloc(found, sizeof(*all));
                if (all) {
                    uint16_t got = found;
                    esp_err_t rec = esp_wifi_scan_get_ap_records(&got, all);
                    ESP_LOGI(TAG, "records ret=%s got=%u",
                             esp_err_to_name(rec), got);
                    if (rec == ESP_OK) {
                        qsort(all, got, sizeof(*all), ap_cmp);
                        for (uint16_t i = 0; i < got && n < SPWIFI_SCAN_MAX;
                             i++) {
                            // 去重（同名只保留信号最强者）。
                            bool dup = false;
                            for (size_t k = 0; k < n; k++) {
                                if (strncmp(out[k].ssid,
                                            (char *)all[i].ssid,
                                            sizeof(out[k].ssid)) == 0) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (dup || all[i].ssid[0] == '\0') {
                                continue;
                            }
                            strncpy(out[n].ssid, (char *)all[i].ssid,
                                    sizeof(out[n].ssid) - 1);
                            out[n].ssid[sizeof(out[n].ssid) - 1] = '\0';
                            out[n].rssi = all[i].rssi;
                            out[n].open = all[i].authmode == WIFI_AUTH_OPEN;
                            n++;
                        }
                    }
                    free(all);
                }
            }
        } else {
            ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(err));
        }
    }
    stack_down(&st);
    ESP_LOGI(TAG, "scan result n=%u", (unsigned)n);
    job.cb(out, n, job.arg);
    s_busy = false;
    vTaskDelete(NULL);
}

bool sp_wifi_scan(spwifi_scan_cb_t cb, void *arg)
{
    if (!cb || s_busy) {
        return false;
    }
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return false;
    }
    job->cb = cb;
    job->arg = arg;
    s_busy = true;
    if (xTaskCreate(scan_task, "sp_scan", 4096, job, 5, NULL) != pdPASS) {
        free(job);
        s_busy = false;
        return false;
    }
    return true;
}

bool sp_wifi_busy(void)
{
    return s_busy;
}

// ------------------------------------------------------------- 连接 ---
static void wifi_task(void *arg)
{
    wifi_job_t job = *(wifi_job_t *)arg;
    free(arg);

    EventGroupHandle_t bits = xEventGroupCreate();
    stack_t st;
    esp_event_handler_instance_t h_wifi = NULL, h_ip = NULL;
    spwifi_status_t result = SPWIFI_TIMEOUT;

    if (stack_up(&st) != ESP_OK) {
        ESP_LOGE(TAG, "connect stack up failed");
        notify(SPWIFI_TIMEOUT, job.cb, job.arg);
        vEventGroupDelete(bits);
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_wifi, bits, &h_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        on_ip, bits, &h_ip);

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, job.ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, job.pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        goto done;
    }
    st.wifi_started = true;

    notify(SPWIFI_CONNECTING, job.cb, job.arg);
    esp_wifi_connect();

    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(CONNECT_TIMEOUT_MS);
    uint8_t retries = 0;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            goto done;
        }
        EventBits_t ev = xEventGroupWaitBits(
            bits, BIT_GOT_IP | BIT_AUTH | BIT_NO_AP | BIT_OTHER,
            pdTRUE, pdFALSE, deadline - now);
        if (ev & BIT_GOT_IP) {
            break;
        }
        if (ev & BIT_AUTH) {
            result = SPWIFI_AUTH_FAIL;
            goto done;
        }
        if (ev & BIT_NO_AP) {
            result = SPWIFI_NO_AP;
            goto done;
        }
        if (ev & BIT_OTHER && ++retries <= 4) {
            esp_wifi_connect();
        }
    }

    notify(SPWIFI_GOT_IP, job.cb, job.arg);
    sp_clock_sntp_begin();
    if (sp_clock_sntp_wait(SNTP_TIMEOUT_MS)) {
        result = SPWIFI_SYNCED;
    } else {
        result = SPWIFI_TIMEOUT;
    }
    sp_clock_sntp_end();

done:
    if (h_wifi) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              h_wifi);
    }
    if (h_ip) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              h_ip);
    }
    stack_down(&st);
    notify(result, job.cb, job.arg);
    vEventGroupDelete(bits);
    s_busy = false;
    vTaskDelete(NULL);
}

bool sp_wifi_run(const char *ssid, const char *pass,
                 spwifi_cb_t cb, void *arg)
{
    if (!ssid || strlen(ssid) == 0 || s_busy) {
        return false;
    }
    wifi_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return false;
    }
    strncpy(job->ssid, ssid, sizeof(job->ssid) - 1);
    if (pass) {
        strncpy(job->pass, pass, sizeof(job->pass) - 1);
    }
    job->cb = cb;
    job->arg = arg;

    s_busy = true;
    if (xTaskCreate(wifi_task, "sp_wifi", 4096, job, 5, NULL) != pdPASS) {
        free(job);
        s_busy = false;
        return false;
    }
    return true;
}
