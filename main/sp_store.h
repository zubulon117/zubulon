// main/sp_store.h —— NVS 持久化：宠物 A/B 双槽存档、Wi-Fi 凭据、音量、时间戳。
//
// 命名空间：
//   "sp"      rec_a / rec_b : 64B sp_record_t，交替写入，选序号新的合法槽
//             vol           : u8 音量档 0..3
//             ts            : u64 最近已知时间（epoch 秒，保存任务每 5min 落盘）
//   "spwifi"  ssid / pass   : 字符串
//
// 存档写入走独立保存任务 + 单槽覆盖队列（最新状态优先，避免堆积磨损）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sp_model.h"

#define SP_VOL_LEVELS 4   // 0 静音 + 3 档
#define SP_WIFI_SSID_MAX 32
#define SP_WIFI_PASS_MAX 64

// 初始化 NVS（必要时擦除恢复）并启动保存任务。幂等。
bool sp_store_init(void);

// 读档：两槽都非法返回 false（新设备）。
bool sp_store_load_pet(sp_pet_t *out);
// 异步存档：立即返回，后台任务落盘；序号自增、A/B 交替。
bool sp_store_save_pet(const sp_pet_t *pet);
// 阻塞等待队列中的存档落盘（换页/进睡前调用），超时 1.5s。
void sp_store_flush(void);
// 抹除全部应用数据（宠物 + Wi-Fi + 设置）。
void sp_store_erase_all(void);

// 音量档 0..3（SP_VOL_LEVELS-1）。
uint8_t sp_store_load_volume(void);
void sp_store_save_volume(uint8_t level);

// Wi-Fi 凭据。密码永不出现在日志中。
bool sp_store_wifi_save(const char *ssid, const char *pass);
bool sp_store_wifi_load(char *ssid, size_t ssid_cap,
                        char *pass, size_t pass_cap);
void sp_store_wifi_clear(void);

// 最近已知时间（epoch 秒）。冷启动 RTC 归零后用于近似恢复，
// 使手动校时默认上次日期、按日玩法在联网校时前仍可用。
// 早于 2024 的值视为无效，拒绝保存/加载。
void sp_store_save_time(int64_t epoch);
bool sp_store_load_time(int64_t *out);
