#include "sp_clock.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"

#include "sp_store.h"

static const char *TAG = "sp_clock";
#define RTC_MIN_YEAR SP_CLOCK_MIN_YEAR

static bool s_valid;

static void apply_tz(void)
{
    // 东八区，无夏令时。
    setenv("TZ", "CST-8", 1);
    tzset();
}

static bool read_local(struct tm *out)
{
    time_t now = time(NULL);
    if (now < 0) {
        return false;
    }
    localtime_r(&now, out);
    return true;
}

void sp_clock_init(void)
{
    apply_tz();
    struct tm tm;
    s_valid = read_local(&tm) && tm.tm_year + 1900 >= RTC_MIN_YEAR;
    if (!s_valid) {
        // RTC 冷启动归零：用 NVS 里最近落盘的时间近似恢复（精度取决于
        // 上次落盘与断电时长），使按日玩法立即可用、手动校时默认上次日期。
        int64_t saved;
        if (sp_store_load_time(&saved)) {
            struct timeval tv = { .tv_sec = (time_t)saved, .tv_usec = 0 };
            if (settimeofday(&tv, NULL) == 0) {
                s_valid = true;
                ESP_LOGI(TAG, "clock restored from NVS (approximate)");
            }
        }
    }
    if (s_valid && read_local(&tm)) {
        ESP_LOGI(TAG, "clock valid: %04d-%02d-%02d %02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
    } else if (!s_valid) {
        ESP_LOGI(TAG, "clock NOT calibrated");
    }
}

bool sp_clock_valid(void)
{
    return s_valid;
}

bool sp_clock_today(sp_date_t *out)
{
    if (!out) {
        return false;
    }
    struct tm tm;
    if (!read_local(&tm) || tm.tm_year + 1900 < RTC_MIN_YEAR) {
        return false;
    }
    out->year = (int16_t)(tm.tm_year + 1900);
    out->month = (uint8_t)(tm.tm_mon + 1);
    out->day = (uint8_t)tm.tm_mday;
    return true;
}

int32_t sp_clock_now_min(void)
{
    time_t now = time(NULL);
    if (now < 0) {
        return 0;
    }
    return (int32_t)(now / 60);
}

bool sp_clock_day_minutes(uint16_t *out)
{
    if (!out) {
        return false;
    }
    struct tm tm;
    if (!read_local(&tm) || tm.tm_year + 1900 < RTC_MIN_YEAR) {
        return false;
    }
    *out = (uint16_t)(tm.tm_hour * 60 + tm.tm_min);
    return true;
}

static bool set_clock(sp_date_t date, uint16_t minute)
{
    if (!sp_date_valid(date)) {
        return false;
    }
    if (minute > 1439) {
        return false;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = date.year - 1900;
    tm.tm_mon = date.month - 1;
    tm.tm_mday = date.day;
    tm.tm_hour = minute / 60;
    tm.tm_min = minute % 60;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    if (epoch < 0) {
        return false;
    }
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday failed");
        return false;
    }
    apply_tz();
    s_valid = true;
    sp_store_save_time((int64_t)epoch);
    return true;
}

bool sp_clock_set_date(sp_date_t date)
{
    uint16_t minute = 12 * 60;  // 默认正午，避开跨日边界
    (void)sp_clock_day_minutes(&minute);
    return set_clock(date, minute);
}

bool sp_clock_set_datetime(sp_date_t date, uint16_t minute)
{
    return set_clock(date, minute);
}

void sp_clock_sntp_begin(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "cn.pool.ntp.org",
                             "ntp.tencent.com"));
    cfg.start = true;
    esp_netif_sntp_init(&cfg);
}

bool sp_clock_sntp_wait(uint32_t timeout_ms)
{
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        ESP_LOGW(TAG, "sntp timeout");
        return false;
    }
    apply_tz();
    s_valid = true;
    sp_store_save_time((int64_t)time(NULL));
    struct tm tm;
    if (read_local(&tm)) {
        ESP_LOGI(TAG, "sntp synced: %04d-%02d-%02d %02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
    }
    return true;
}

void sp_clock_sntp_end(void)
{
    esp_netif_sntp_deinit();
}
