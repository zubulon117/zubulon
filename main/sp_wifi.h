// main/sp_wifi.h —— 按需 Wi-Fi 校时：一次性 STA 连接 → SNTP 同步 → 断电关射频。
//
// 设计约束：
//   - 不常驻 Wi-Fi；sp_wifi_run() 拉起 worker 完成连接与校时后自动停射频；
//   - 连接 15s 超时；认证/找不到 AP 立即失败；
//   - 拿到 IP 后同步 SNTP（20s 超时）；
//   - 回调在 worker 任务上下文执行，UI 层须自行把结果投递到 LVGL 任务。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SPWIFI_CONNECTING = 0,  // 已开始连接
    SPWIFI_GOT_IP,          // 已拿到 IP（开始 SNTP）
    SPWIFI_SYNCED,          // SNTP 同步成功
    SPWIFI_AUTH_FAIL,       // 密码错误/认证失败
    SPWIFI_NO_AP,           // 找不到该 SSID
    SPWIFI_TIMEOUT,         // 连接或校时超时
} spwifi_status_t;

#define SPWIFI_SCAN_MAX 8

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;        // true=开放网络（免密）
} spwifi_ap_t;

typedef void (*spwifi_cb_t)(spwifi_status_t status, void *arg);
// 扫描结束回调（成功 count>0；失败/为空 count=0）。运行在 worker 上下文。
typedef void (*spwifi_scan_cb_t)(const spwifi_ap_t *aps, size_t count,
                                 void *arg);

// 异步扫描周边 AP：结果按信号去重排序，最多 SPWIFI_SCAN_MAX 个。
bool sp_wifi_scan(spwifi_scan_cb_t cb, void *arg);

// 异步执行：立即返回；整个生命周期约 0..35s。
// ssid/pass 会被复制，调用方可立即释放。返回 false=资源不足未启动
// （已有扫描/连接 worker 在跑时也返回 false）。
bool sp_wifi_run(const char *ssid, const char *pass,
                 spwifi_cb_t cb, void *arg);

// 是否有无线 worker 在运行（起栈、连接、SNTP 全程为 true）。
// 为 true 时调用方不应发起新的扫描/连接，省电层也不应进入轻睡眠。
bool sp_wifi_busy(void);
