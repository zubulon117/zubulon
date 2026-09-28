// main/sp_power.h —— 息屏省电：空闲降背光 + 轻睡眠（GPIO0 按键唤醒）。
//
// 策略：
//   30s 无按键        背光降到 20%
//   再 5s 无按键      存档落盘 → 音频挂起 → 关背光 → 轻睡眠
//   任意按键(GPIO0)   唤醒 → 恢复背光/音频 → 重新计时
#pragma once

#include "sp_app.h"

// 创建省电任务并注册按键钩子。
void sp_power_init(void);

// 重置空闲计时（任何按键事件经由 sp_app 钩子自动调用，一般无需手动调用）。
void sp_power_kick(void);
