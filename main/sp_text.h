// main/sp_text.h —— 全部中文词库表（星座/生肖/颜色/活动/幸运物/寄语/阶段）。
//
// 这是字体子集与语音素材的唯一词表来源：
//   * 字体生成器扫描本文件与 UI 源码中的字符串得到码点清单；
//   * 语音生成器仅为需要朗读的词条（星座名/颜色/活动/固定句式）产出音频；
//   * 幸运物与寄语只显示不朗读，不占语音预算。
#pragma once

#include <stdint.h>

#include "sp_model.h"   // 词库规模常量（SP_SIGN_COUNT 等）

#define SP_ZODIAC_COUNT    12
#define SP_STAGE_COUNT     3

typedef struct {
    const char *name;     // 星座中文名，如"白羊座"
    const char *range;    // 日期范围，如"3.21-4.19"
    const char *element;  // 火象/土象/风象/水象
} sp_sign_text_t;

const sp_sign_text_t *sp_sign_text(uint8_t sign);
const char *sp_zodiac_name(uint8_t zodiac);   // 鼠牛虎兔...
const char *sp_dim_name(uint8_t dim);         // 五维名称
const char *sp_stage_name(uint8_t stage);     // 星幼宠/星伴/星辉
const char *sp_color_name(uint8_t color_id);  // 红、橙...
uint32_t sp_color_hex(uint8_t color_id);      // 界面用 0xRRGGBB
const char *sp_activity_name(uint8_t id);     // 宜/忌活动词条
const char *sp_item_name(uint8_t id);         // 幸运物
const char *sp_wish_text(uint8_t id);         // 寄语整句
const char *sp_theme_title(uint8_t id);       // 当日主题标题
const char *sp_dim_quote(uint8_t dim, uint8_t qid); // 五维短评
