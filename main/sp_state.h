// main/sp_state.h —— 应用全局状态：宠物档案、今日日期、音量、电池缓存。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sp_model.h"

// 初始化顺序：NVS 存档 → 时钟 → 音频 → 读档 →（时钟有效时）跨天追赶。
void sp_state_init(void);

bool sp_state_has_pet(void);
// 建档并立即持久化（引导完成时调用）。
void sp_state_create_pet(sp_date_t birthday, sp_date_t today);

sp_pet_t *sp_state_pet(void);
// 异步存档（内部含节流，日常操作可频繁调用）。
void sp_state_save(void);
// 进睡前同步落盘。
void sp_state_flush(void);

// 当前日期；时钟无效返回 false。
bool sp_state_today(sp_date_t *out);
// 推进跨天（主页进入/从设置返回时调用），返回跨过的天数。
uint32_t sp_state_catchup(void);

uint8_t sp_state_volume(void);
void sp_state_set_volume(uint8_t level);

// 电池电量百分比，-1=不可用；内部缓存，2s 内不重复读。
int sp_state_battery(void);

// 星缘图鉴：当前位图；相遇一位并立即落盘，返回是否为初次相遇。
uint16_t sp_state_dex(void);
bool sp_state_dex_visit(uint8_t sign);

// 抹除全部应用数据并重启（回到引导）。
void sp_state_factory_reset(void);
