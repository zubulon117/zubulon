// main/sp_page_wizard.h —— 孵化引导向导 + 手动日期时间校时页。
#pragma once

#include "sp_app.h"

// 新玩家孵化：欢迎语 →（时钟无效则先校时）→ 输生日 → 星座揭幕 → 主页。
// sp_state 必须已初始化；完成后自行建档。
sp_page_t *sp_page_hatch(void);

// 仅校准日期时间（设置页 / 老存档遇到掉电时用）。
sp_page_t *sp_page_datetime(void);

// 设置向导完成后的跳转目标（由路由设置）。
void sp_wizard_set_next(sp_page_t *next);
