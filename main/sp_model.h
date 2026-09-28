// main/sp_model.h —— 命定星宠纯逻辑模型（日期/星座/生肖/运势/养成/语音脚本）。
//
// 本头与 sp_model.c 不依赖任何 ESP-IDF/LVGL 头文件，可用宿主机 gcc 直接编译测试。
// 所有数值规则集中在本文件常量区，便于策划调整与测试核对。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sp_voice_script.h"

// --------------------------------------------------------------- 常量 ----
// 词库规模（sp_text 词表与语音清单必须与之一致）。
#define SP_SIGN_COUNT      12
#define SP_COLOR_COUNT     12
#define SP_ACTIVITY_COUNT  24
#define SP_ITEM_COUNT      12
#define SP_WISH_COUNT      12
#define SP_THEME_COUNT     24
#define SP_DIM_QUOTE_COUNT 3    // 每维星级对应的短评数量
#define SP_DIM_COUNT       5    // 综合/爱情/事业/财运/健康

#define SP_YEAR_MIN       1900
#define SP_YEAR_MAX       2099
#define SP_BOND_MAX       100
#define SP_MOOD_MAX       100
#define SP_STREAK_CAP     30      // 连续签到显示与追赶上限
#define SP_MAX_CATCHUP    14      // 掉电跨天追赶封顶天数

#define SP_FEED_PER_DAY   3
#define SP_PET_PER_DAY    5
#define SP_PET_COOLDOWN_MIN 3
#define SP_FEED_MOOD      8
#define SP_FEED_BOND      2
#define SP_PET_MOOD       5
#define SP_PET_BOND       1
#define SP_CHECKIN_BOND   5
#define SP_CHECKIN_COINS  10
#define SP_CHECKIN_BONUS_STEP 2   // 每连续一天额外 2 星币
#define SP_CHECKIN_BONUS_CAP  5   // 额外部分封顶 10
#define SP_DAILY_MOOD_DECAY  12
#define SP_DAILY_BOND_DECAY  2

// 成长阶段亲密度阈值：<30 星幼宠；<70 星伴；其余星辉。
#define SP_STAGE2_BOND    30
#define SP_STAGE3_BOND    70

// 星座序号（按白羊座起算，0..11）。
enum {
    SP_SIGN_ARIES = 0,
    SP_SIGN_TAURUS,
    SP_SIGN_GEMINI,
    SP_SIGN_CANCER,
    SP_SIGN_LEO,
    SP_SIGN_VIRGO,
    SP_SIGN_LIBRA,
    SP_SIGN_SCORPIO,
    SP_SIGN_SAGITTARIUS,
    SP_SIGN_CAPRICORN,
    SP_SIGN_AQUARIUS,
    SP_SIGN_PISCES,
};

// 运势五维索引。
enum {
    SP_DIM_OVERALL = 0,
    SP_DIM_LOVE,
    SP_DIM_WORK,
    SP_DIM_WEALTH,
    SP_DIM_HEALTH,
};

// ------------------------------------------------------------- 日期 ----
typedef struct {
    int16_t year;
    uint8_t month;
    uint8_t day;
} sp_date_t;

bool sp_is_leap(int year);
uint8_t sp_days_in_month(int year, uint8_t month);
bool sp_date_valid(sp_date_t d);
bool sp_date_equal(sp_date_t a, sp_date_t b);
// 自 1970-01-01 的天数（支持 1900..2099，可为负）。
int32_t sp_date_serial(sp_date_t d);
// 相差天数：b - a（忽略合法性，调用前应保证日期有效）。
int32_t sp_date_diff(sp_date_t b, sp_date_t a);
sp_date_t sp_date_from_serial(int32_t serial);
sp_date_t sp_date_add_days(sp_date_t d, int32_t delta);
// 星期：0=周日 … 6=周六。
uint8_t sp_date_weekday(sp_date_t d);

// 公历十二星座判定；非法日期返回 0xFF。
uint8_t sp_zodiac_sign(uint8_t month, uint8_t day);
// 生肖序号（0=鼠 … 11=猪），按公历年。
uint8_t sp_chinese_zodiac(int year);

// ------------------------------------------------------------- 随机 ----
typedef struct {
    uint32_t state;
} sp_rng_t;

void sp_rng_seed(sp_rng_t *rng, uint32_t seed);
uint32_t sp_rng_next(sp_rng_t *rng);
// 闭区间 [lo, hi]；要求 hi>=lo 且区间非 0。
uint32_t sp_rng_range(sp_rng_t *rng, uint32_t lo, uint32_t hi);

// (星座, 日期) 的确定性种子（同输入永远相同）。
uint32_t sp_day_seed(uint8_t sign, sp_date_t date);

// ------------------------------------------------------------- 运势 ----
#define SP_JI_MAX 2

typedef struct {
    uint8_t stars[SP_DIM_COUNT]; // [综合,爱情,事业,财运,健康]，均 1..5
    uint8_t theme_id;            // 当日主题标题 id
    uint8_t match_sign;          // 速配星座 id
    uint8_t color_id;            // 0..SP_COLOR_COUNT-1
    uint8_t lucky_num;           // 1..99
    uint8_t item_id;             // 0..SP_ITEM_COUNT-1
    uint8_t yi[2];               // 两个"宜"活动 id，保证互不相同
    uint8_t ji[SP_JI_MAX];       // 1..2 个"忌"活动 id；无第二项时为 SP_ID_NONE
    uint8_t wish_id;             // 0..SP_WISH_COUNT-1
    uint8_t quote_id[SP_DIM_COUNT]; // 每维短评 id（0..SP_DIM_QUOTE_COUNT-1）
} sp_fortune_t;

#define SP_ID_NONE 0xFFu

// 生成当日运势。同一 (sign, date) 结果逐字节一致。
void sp_fortune_today(uint8_t sign, sp_date_t date, sp_fortune_t *out);

// 心情表现：结合基础心情与当日综合星级，返回 0=普通 1=开心（供页面选帧）。
uint8_t sp_mood_tone(uint8_t mood, uint8_t overall_stars);

// 语音播报脚本：把当日运势编译为片段 ID 序列；返回写入数量。
// out 可为 NULL（仅计算长度）；cap 为容量，不足时截断。
size_t sp_voice_plan(uint8_t sign, const sp_fortune_t *f,
                     spv_id_t *out, size_t cap);
// 1..99 的中文数字片段分解；返回写入数量。
size_t sp_voice_number(int number, spv_id_t *out, size_t cap);

// ------------------------------------------------------------- 养成 ----
typedef struct {
    sp_date_t birthday;        // 玩家公历生日
    uint8_t sign;              // 星座序号（由生日算出并固化）

    uint8_t bond;              // 亲密度 0..100
    uint8_t mood;              // 心情 0..100
    uint16_t coins;            // 星币
    uint8_t stage;             // 成长阶段 0..2（由 bond 推导，存档时冗余保存）

    uint8_t feed_today;        // 当日已喂食次数
    uint8_t pet_today;         // 当日已抚摸次数
    uint8_t checked_in;        // 当日是否已许愿签到
    uint16_t streak;           // 连续签到天数
    sp_date_t last_checkin;    // 上次签到日期
    int32_t last_pet_min;      // 上次抚摸的"分钟序号"，-1=从未
    sp_date_t last_day;        // 上次完成跨天结算的日期
    uint8_t fortune_seen;      // 当日运势是否已开签
} sp_pet_t;

typedef enum {
    SP_ACT_OK = 0,        // 成功
    SP_ACT_LIMIT,         // 当日次数用尽
    SP_ACT_COOLDOWN,      // 抚摸冷却中
    SP_ACT_DONE,          // 今日已签到
} sp_act_result_t;

// 以生日建档（today 为首日）。自动计算星座，初始 bond=0/mood=60。
void sp_pet_new(sp_pet_t *pet, sp_date_t birthday, sp_date_t today);
// 钳制所有字段到合法范围（读档后必须调用）。
void sp_pet_sanitize(sp_pet_t *pet);
// 由 bond 推导阶段 0..2。
uint8_t sp_bond_stage(uint8_t bond);

// now_min：当前相对任意固定纪元的分钟序号（设备侧传 time(null)/60 即可）。
sp_act_result_t sp_pet_feed(sp_pet_t *pet);
sp_act_result_t sp_pet_pet(sp_pet_t *pet, int32_t now_min);
sp_act_result_t sp_pet_checkin(sp_pet_t *pet, sp_date_t today);

// 跨天结算：把 pet 推进到 current（含每日衰减、次数重置、掉电追赶封顶）。
// 返回实际跨过的天数（0 表示无需结算）。
uint32_t sp_pet_advance_day(sp_pet_t *pet, sp_date_t current);

// ------------------------------------------------------------- 图鉴 ----
// 星缘图鉴：按日星友来访 + 已相遇位图（bit n = 相遇过星座 n）。
#define SP_DEX_ALL 0x0FFFu

// 该日来访的星友星座：以 (own_sign, date) 做确定性种子，保证异于自己。
uint8_t sp_dex_visitor(uint8_t own_sign, sp_date_t date);
bool sp_dex_has(uint16_t mask, uint8_t sign);
// 相遇置位；返回新位图（越界 sign 原样返回）。
uint16_t sp_dex_visit(uint16_t mask, uint8_t sign);
// 十二星座全部相遇（星图大师达成）。
bool sp_dex_complete(uint16_t mask);
