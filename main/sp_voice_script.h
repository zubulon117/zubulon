// main/sp_voice_script.h —— 离线语音片段 ID（纯枚举头，主机/固件共用）。
//
// 每个枚举值对应 tools/starpet/voice_inventory.json 中的一条 TTS 音频，
// 三方一致性（枚举 ↔ inventory ↔ 生成的 manifest）由校验脚本保证。
// 播报时设备按 sp_voice_plan() 输出的 ID 序列顺序播放对应 IMA-ADPCM 片段。
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    // --- 固定句式 ---
    SPV_INTRO = 0,      // "命定星宠，今日运势"
    SPV_OVERALL,        // "综合运势"
    SPV_STAR,           // "星"
    SPV_LUCKY_COLOR,    // "幸运颜色"
    SPV_COLOR_SUFFIX,   // "色"
    SPV_LUCKY_NUM,      // "幸运数字"
    SPV_YI,             // "宜"
    SPV_JI,             // "忌"
    SPV_ENDING,         // "今天也要开心哦"

    // --- 数字 0..10（含"十"，用于 1..99 的中文分解）---
    SPV_D0, SPV_D1, SPV_D2, SPV_D3, SPV_D4, SPV_D5,
    SPV_D6, SPV_D7, SPV_D8, SPV_D9, SPV_D10,

    // --- 十二星座（顺序与 sp_model 的 SP_SIGN_* 一致）---
    SPV_SIGN_ARIES,         // 白羊座
    SPV_SIGN_TAURUS,        // 金牛座
    SPV_SIGN_GEMINI,        // 双子座
    SPV_SIGN_CANCER,        // 巨蟹座
    SPV_SIGN_LEO,           // 狮子座
    SPV_SIGN_VIRGO,         // 处女座
    SPV_SIGN_LIBRA,         // 天秤座
    SPV_SIGN_SCORPIO,       // 天蝎座
    SPV_SIGN_SAGITTARIUS,   // 射手座
    SPV_SIGN_CAPRICORN,     // 摩羯座
    SPV_SIGN_AQUARIUS,      // 水瓶座
    SPV_SIGN_PISCES,        // 双鱼座

    // --- 幸运颜色（单字颜色名，后接 SPV_COLOR_SUFFIX）---
    SPV_C_RED, SPV_C_ORANGE, SPV_C_YELLOW, SPV_C_GREEN,
    SPV_C_CYAN, SPV_C_BLUE, SPV_C_PURPLE, SPV_C_PINK,
    SPV_C_WHITE, SPV_C_GRAY, SPV_C_GOLD, SPV_C_SILVER,

    // --- 宜/忌活动词条（顺序与 sp_text 的活动表一致）---
    SPV_A_BIAOBAI,      // 表白
    SPV_A_JIABAN,       // 加班
    SPV_A_CHUXING,      // 出行
    SPV_A_GOUWU,        // 购物
    SPV_A_JUHUI,        // 聚会
    SPV_A_YUNDONG,      // 运动
    SPV_A_XUEXI,        // 学习
    SPV_A_LICAI,        // 理财
    SPV_A_GOUTONG,      // 沟通
    SPV_A_XIUXI,        // 休息
    SPV_A_ZHENGLI,      // 整理
    SPV_A_ZAOSHUI,      // 早睡
    SPV_A_YUEHUI,       // 约会
    SPV_A_TOUZI,        // 投资
    SPV_A_LVXING,       // 旅行
    SPV_A_PENGREN,      // 烹饪
    SPV_A_CHANGGE,      // 唱歌
    SPV_A_FADAI,        // 发呆
    SPV_A_KANDY,        // 看电影
    SPV_A_PAOBU,        // 跑步
    SPV_A_XIEYOUJIAN,   // 写邮件
    SPV_A_FUPAN,        // 复盘
    SPV_A_WUSHUI,       // 午睡
    SPV_A_LIAOTIAN,     // 聊天

    SPV_COUNT
} spv_id_t;

// 星座序号（sp_model 定义）→ 语音片段 ID。
spv_id_t spv_sign(uint8_t sign);
// 颜色 id（0..11）→ 语音片段 ID。
spv_id_t spv_color(uint8_t color_id);
// 活动 id（0..SP_ACTIVITY_COUNT-1）→ 语音片段 ID。
spv_id_t spv_activity(uint8_t activity_id);
