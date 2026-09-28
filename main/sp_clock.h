// main/sp_clock.h —— 设备时钟：CST-8 时区、RTC 有效性、手动校时、SNTP 同步。
//
// 时钟只记录"本地日期 + 当天分钟"。RTC 年份 < 2024 视为掉电未校准
// （冷启动默认 1970），UI 必须引导手动校时或联网 SNTP。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sp_model.h"

// RTC 年份小于该值视为掉电未校准（冷启动默认 1970）；手动校时的年份
// 也不得低于该值，否则会出现"标记有效但今日不可用"的半失效状态。
#define SP_CLOCK_MIN_YEAR 2024

// 设置时区并判断当前 RTC 是否有效。幂等。
void sp_clock_init(void);

bool sp_clock_valid(void);
// 当前本地日期；时钟无效时返回 false。
bool sp_clock_today(sp_date_t *out);
// 当前"分钟序号"（time(NULL)/60），供抚摸冷却使用；无效时返回 0。
int32_t sp_clock_now_min(void);
// 当天 0..1439 分钟；无效返回 false。
bool sp_clock_day_minutes(uint16_t *out);

// 手动校时：设定本地日期，保留当天时分（无效则默认 12:00）。
// 成功后时钟转为有效。
bool sp_clock_set_date(sp_date_t date);
// 手动校时：完整设定本地日期与当天时分（minute 0..1439）。
bool sp_clock_set_datetime(sp_date_t date, uint16_t minute);

// SNTP 三段式（由 spwifi 在拿到 IP 后调用）：
// begin 配置多服务器并启动；wait 阻塞至同步完成或超时；end 停止释放。
void sp_clock_sntp_begin(void);
bool sp_clock_sntp_wait(uint32_t timeout_ms);
void sp_clock_sntp_end(void);
