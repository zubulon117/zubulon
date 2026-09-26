// tests/test_sp_model.c —— 命定星宠纯逻辑模型主机测试（gcc 直跑，无第三方框架）。
// 编译见 tools/validate.sh 中 test_sp_model 条目。
#include "sp_model.h"

#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static sp_date_t mkd(int y, int m, int d)
{
    sp_date_t r = { (int16_t)y, (uint8_t)m, (uint8_t)d };
    return r;
}

static void test_dates(void)
{
    CHECK(sp_is_leap(2000));
    CHECK(!sp_is_leap(1900));
    CHECK(sp_is_leap(2024));
    CHECK(!sp_is_leap(2025));
    CHECK(sp_days_in_month(2024, 2) == 29);
    CHECK(sp_days_in_month(2025, 2) == 28);
    CHECK(sp_days_in_month(2025, 13) == 0);

    CHECK(sp_date_valid(mkd(2024, 2, 29)));
    CHECK(!sp_date_valid(mkd(2023, 2, 29)));
    CHECK(!sp_date_valid(mkd(1899, 1, 1)));
    CHECK(!sp_date_valid(mkd(2100, 1, 1)));
    CHECK(!sp_date_valid(mkd(2025, 0, 1)));
    CHECK(!sp_date_valid(mkd(2025, 1, 32)));

    // 序列/反算/差值。
    sp_date_t d = mkd(2025, 6, 1);
    int32_t s = sp_date_serial(d);
    CHECK(sp_date_equal(sp_date_from_serial(s), d));
    CHECK(sp_date_diff(mkd(2025, 6, 2), d) == 1);
    CHECK(sp_date_diff(mkd(2025, 5, 31), d) == -1);
    CHECK(sp_date_equal(sp_date_add_days(mkd(2024, 2, 28), 1), mkd(2024, 2, 29)));
    CHECK(sp_date_equal(sp_date_add_days(mkd(2024, 12, 31), 1), mkd(2025, 1, 1)));
    CHECK(sp_date_equal(sp_date_add_days(mkd(1900, 1, 1), -1), mkd(1899, 12, 31)));

    // 星期抽样（2025-01-01 周三=3；2000-01-01 周六=6）。
    CHECK(sp_date_weekday(mkd(2025, 1, 1)) == 3);
    CHECK(sp_date_weekday(mkd(2000, 1, 1)) == 6);
}

static void test_signs(void)
{
    // 12 星座代表日期。
    struct { int m, d; uint8_t sign; } cases[] = {
        { 4,  1, SP_SIGN_ARIES },      { 4, 19, SP_SIGN_ARIES },
        { 5,  5, SP_SIGN_TAURUS },    { 6,  1, SP_SIGN_GEMINI },
        { 7,  1, SP_SIGN_CANCER },    { 8,  1, SP_SIGN_LEO },
        { 9,  1, SP_SIGN_VIRGO },     { 10, 10, SP_SIGN_LIBRA },
        { 11, 10, SP_SIGN_SCORPIO },  { 12, 10, SP_SIGN_SAGITTARIUS },
        { 1,  10, SP_SIGN_CAPRICORN },{ 2,  1, SP_SIGN_AQUARIUS },
        { 3,  1, SP_SIGN_PISCES },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(sp_zodiac_sign((uint8_t)cases[i].m, (uint8_t)cases[i].d)
              == cases[i].sign);
    }

    // 全部分界日两侧。
    struct { int m, d; uint8_t before, after; } bounds[] = {
        { 1, 20, SP_SIGN_CAPRICORN, SP_SIGN_AQUARIUS },
        { 2, 19, SP_SIGN_AQUARIUS, SP_SIGN_PISCES },
        { 3, 21, SP_SIGN_PISCES, SP_SIGN_ARIES },
        { 4, 20, SP_SIGN_ARIES, SP_SIGN_TAURUS },
        { 5, 21, SP_SIGN_TAURUS, SP_SIGN_GEMINI },
        { 6, 22, SP_SIGN_GEMINI, SP_SIGN_CANCER },
        { 7, 23, SP_SIGN_CANCER, SP_SIGN_LEO },
        { 8, 23, SP_SIGN_LEO, SP_SIGN_VIRGO },
        { 9, 23, SP_SIGN_VIRGO, SP_SIGN_LIBRA },
        { 10, 24, SP_SIGN_LIBRA, SP_SIGN_SCORPIO },
        { 11, 23, SP_SIGN_SCORPIO, SP_SIGN_SAGITTARIUS },
        { 12, 22, SP_SIGN_SAGITTARIUS, SP_SIGN_CAPRICORN },
    };
    for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i) {
        CHECK(sp_zodiac_sign((uint8_t)bounds[i].m,
                             (uint8_t)(bounds[i].d - 1)) == bounds[i].before);
        CHECK(sp_zodiac_sign((uint8_t)bounds[i].m,
                             (uint8_t)bounds[i].d) == bounds[i].after);
    }
    CHECK(sp_zodiac_sign(13, 1) == 0xFF);

    // 生肖：2020 鼠、2025 蛇、2024 龙、1900 鼠。
    CHECK(sp_chinese_zodiac(2020) == 0);
    CHECK(sp_chinese_zodiac(2025) == 5);
    CHECK(sp_chinese_zodiac(2024) == 4);
    CHECK(sp_chinese_zodiac(1900) == 0);
    CHECK(sp_chinese_zodiac(2099) == 7);  // 羊
}

static void fortune_valid(uint8_t sign, sp_date_t date)
{
    sp_fortune_t f;
    sp_fortune_today(sign, date, &f);
    for (int i = 0; i < SP_DIM_COUNT; ++i) {
        CHECK(f.stars[i] >= 1 && f.stars[i] <= 5);
    }
    CHECK(f.color_id < SP_COLOR_COUNT);
    CHECK(f.lucky_num >= 1 && f.lucky_num <= 99);
    CHECK(f.item_id < SP_ITEM_COUNT);
    CHECK(f.yi[0] < SP_ACTIVITY_COUNT);
    CHECK(f.yi[1] < SP_ACTIVITY_COUNT);
    CHECK(f.yi[0] != f.yi[1]);
    CHECK(f.ji[0] < SP_ACTIVITY_COUNT);
    CHECK(f.ji[0] != f.yi[0] && f.ji[0] != f.yi[1]);
    if (f.ji[1] != SP_ID_NONE) {
        CHECK(f.ji[1] < SP_ACTIVITY_COUNT);
        CHECK(f.ji[1] != f.ji[0]);
        CHECK(f.ji[1] != f.yi[0] && f.ji[1] != f.yi[1]);
    }
    CHECK(f.wish_id < SP_WISH_COUNT);
}

static void test_fortune(void)
{
    sp_date_t d = mkd(2025, 6, 1);

    // 确定性：同输入逐字节一致。
    sp_fortune_t a, b;
    sp_fortune_today(SP_SIGN_CANCER, d, &a);
    sp_fortune_today(SP_SIGN_CANCER, d, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    // 不同星座同一天一般不同（至少幸运数字或颜色有差异的概率极高）。
    sp_fortune_t c;
    sp_fortune_today(SP_SIGN_LEO, d, &c);
    CHECK(memcmp(&a, &c, sizeof(a)) != 0);

    // 值域与互斥：跨 12 星座、60 天全部合法。
    int distinct_overall = 0;
    bool seen[5] = { 0 };
    for (uint8_t sign = 0; sign < 12; ++sign) {
        for (int day = 0; day < 60; ++day) {
            sp_date_t dd = sp_date_add_days(mkd(2025, 1, 1), day);
            sp_fortune_t f;
            sp_fortune_today(sign, dd, &f);
            fortune_valid(sign, dd);
            if (!seen[f.stars[SP_DIM_OVERALL] - 1]) {
                seen[f.stars[SP_DIM_OVERALL] - 1] = true;
                distinct_overall++;
            }
            (void)f;
        }
    }
    CHECK(distinct_overall >= 3);

    // 心情表现。
    CHECK(sp_mood_tone(20, 2) == 0);
    CHECK(sp_mood_tone(90, 1) == 1);
    CHECK(sp_mood_tone(50, 5) == 1);
    CHECK(sp_mood_tone(50, 3) == 0);
}

static void test_pet(void)
{
    sp_date_t birth = mkd(1998, 7, 10);  // 巨蟹
    sp_date_t day0 = mkd(2025, 6, 1);
    sp_pet_t pet;
    sp_pet_new(&pet, birth, day0);
    CHECK(pet.sign == SP_SIGN_CANCER);
    CHECK(pet.bond == 0 && pet.mood == 60 && pet.coins == 0);
    CHECK(pet.stage == 0);
    CHECK(sp_date_equal(pet.last_day, day0));

    // 喂食上限与数值。
    for (int i = 0; i < SP_FEED_PER_DAY; ++i) {
        CHECK(sp_pet_feed(&pet) == SP_ACT_OK);
    }
    CHECK(sp_pet_feed(&pet) == SP_ACT_LIMIT);
    CHECK(pet.feed_today == SP_FEED_PER_DAY);
    CHECK(pet.mood == 60 + SP_FEED_PER_DAY * SP_FEED_MOOD);  // 未达 100
    CHECK(pet.bond == SP_FEED_PER_DAY * SP_FEED_BOND);

    // 抚摸冷却与上限。
    CHECK(sp_pet_pet(&pet, 0) == SP_ACT_OK);
    CHECK(sp_pet_pet(&pet, 1) == SP_ACT_COOLDOWN);
    CHECK(sp_pet_pet(&pet, SP_PET_COOLDOWN_MIN) == SP_ACT_OK);
    pet.pet_today = SP_PET_PER_DAY;
    CHECK(sp_pet_pet(&pet, 100000) == SP_ACT_LIMIT);

    // 签到：首日连签=1，奖励 10；次日连签=2，奖励 12。
    sp_pet_t p2;
    sp_pet_new(&p2, birth, day0);
    CHECK(sp_pet_checkin(&p2, day0) == SP_ACT_OK);
    CHECK(p2.streak == 1 && p2.coins == SP_CHECKIN_COINS);
    CHECK(sp_pet_checkin(&p2, day0) == SP_ACT_DONE);
    sp_date_t day1 = sp_date_add_days(day0, 1);
    sp_pet_advance_day(&p2, day1);
    CHECK(sp_pet_checkin(&p2, day1) == SP_ACT_OK);
    CHECK(p2.streak == 2);
    CHECK(p2.coins == SP_CHECKIN_COINS + SP_CHECKIN_COINS + SP_CHECKIN_BONUS_STEP);

    // 断签重置。
    sp_date_t day5 = sp_date_add_days(day0, 5);
    sp_pet_advance_day(&p2, day5);
    CHECK(sp_pet_checkin(&p2, day5) == SP_ACT_OK);
    CHECK(p2.streak == 1);

    // 阶段阈值。
    CHECK(sp_bond_stage(0) == 0);
    CHECK(sp_bond_stage(29) == 0);
    CHECK(sp_bond_stage(30) == 1);
    CHECK(sp_bond_stage(69) == 1);
    CHECK(sp_bond_stage(70) == 2);
    CHECK(sp_bond_stage(100) == 2);

    // 跨天结算：衰减、重置每日次数。
    sp_pet_t p3;
    sp_pet_new(&p3, birth, day0);
    p3.mood = 50;
    p3.bond = 40;
    p3.feed_today = SP_FEED_PER_DAY;
    sp_pet_advance_day(&p3, day1);
    CHECK(p3.mood == 50 - SP_DAILY_MOOD_DECAY);
    CHECK(p3.bond == 40 - SP_DAILY_BOND_DECAY);
    CHECK(p3.feed_today == 0 && p3.checked_in == 0);
    CHECK(p3.fortune_seen == 0);
    CHECK(sp_date_equal(p3.last_day, day1));

    // 心情/亲密有下限，不致死。
    p3.mood = 0;
    p3.bond = 0;
    sp_pet_advance_day(&p3, sp_date_add_days(day0, 2));
    CHECK(p3.mood == 0 && p3.bond == 0);

    // 掉电追赶封顶：跨 30 天只结算 14 次（亲密度可区分 14 次=72 与 30 次=40）。
    sp_pet_t p4;
    sp_pet_new(&p4, birth, day0);
    p4.mood = 100;
    p4.bond = 100;
    uint32_t applied = sp_pet_advance_day(&p4, sp_date_add_days(day0, 30));
    CHECK(applied == SP_MAX_CATCHUP);
    CHECK(p4.bond == 100 - SP_MAX_CATCHUP * SP_DAILY_BOND_DECAY);
    // 心情 100 经 9 天衰减即触底 0，不出现负值，也不累加 30 次。
    CHECK(p4.mood == 0);

    // sanitize 钳制。
    sp_pet_t bad;
    memset(&bad, 0, sizeof(bad));
    bad.bond = 200;
    bad.mood = 250;
    bad.feed_today = 99;
    bad.streak = 999;
    sp_pet_sanitize(&bad);
    CHECK(bad.bond == SP_BOND_MAX && bad.mood == SP_MOOD_MAX);
    CHECK(bad.feed_today == SP_FEED_PER_DAY);
    CHECK(bad.streak == SP_STREAK_CAP);
    CHECK(sp_date_valid(bad.birthday));
    CHECK(bad.sign == sp_zodiac_sign(bad.birthday.month, bad.birthday.day));
}

static void test_voice(void)
{
    // 数字分解：1/7/10/11/20/23/99。
    spv_id_t num[3];
    CHECK(sp_voice_number(1, num, 3) == 1 && num[0] == SPV_D1);
    CHECK(sp_voice_number(7, num, 3) == 1 && num[0] == SPV_D7);
    CHECK(sp_voice_number(10, num, 3) == 1 && num[0] == SPV_D10);
    size_t n = sp_voice_number(11, num, 3);
    CHECK(n == 2 && num[0] == SPV_D10 && num[1] == SPV_D1);
    n = sp_voice_number(20, num, 3);
    CHECK(n == 2 && num[0] == SPV_D2 && num[1] == SPV_D10);
    n = sp_voice_number(23, num, 3);
    CHECK(n == 3 && num[0] == SPV_D2 && num[1] == SPV_D10 && num[2] == SPV_D3);
    n = sp_voice_number(99, num, 3);
    CHECK(n == 3 && num[0] == SPV_D9 && num[1] == SPV_D10 && num[2] == SPV_D9);

    // 完整脚本：固定顺序与关键片段。
    sp_fortune_t f;
    memset(&f, 0, sizeof(f));
    f.stars[SP_DIM_OVERALL] = 5;
    f.color_id = 3;
    f.lucky_num = 20;
    f.yi[0] = 0;
    f.yi[1] = 1;
    f.ji[0] = 2;
    f.ji[1] = SP_ID_NONE;
    spv_id_t plan[32];
    n = sp_voice_plan(SP_SIGN_CANCER, &f, plan, 32);
    CHECK(n >= 12 && n <= 20);
    CHECK(plan[0] == SPV_INTRO);
    CHECK(plan[1] == spv_sign(SP_SIGN_CANCER));
    size_t i = 2;
    CHECK(plan[i++] == SPV_OVERALL);
    CHECK(plan[i++] == SPV_D5);
    CHECK(plan[i++] == SPV_STAR);
    CHECK(plan[i++] == SPV_LUCKY_COLOR);
    CHECK(plan[i++] == spv_color(3));
    CHECK(plan[i++] == SPV_COLOR_SUFFIX);
    CHECK(plan[i++] == SPV_LUCKY_NUM);
    CHECK(plan[i++] == SPV_D2);
    CHECK(plan[i++] == SPV_D10);
    CHECK(plan[i++] == SPV_YI);
    CHECK(plan[i++] == spv_activity(0));
    CHECK(plan[i++] == spv_activity(1));
    CHECK(plan[i++] == SPV_JI);
    CHECK(plan[i++] == spv_activity(2));
    CHECK(plan[n - 1] == SPV_ENDING);

    // 所有 id 在枚举范围内。
    for (size_t k = 0; k < n; ++k) {
        CHECK(plan[k] >= 0 && plan[k] < SPV_COUNT);
    }

    // cap 截断安全（NULL 缓冲只计数）。
    size_t full = sp_voice_plan(SP_SIGN_CANCER, &f, NULL, 0);
    CHECK(full == n);
    spv_id_t tiny[4];
    CHECK(sp_voice_plan(SP_SIGN_CANCER, &f, tiny, 4) == full);

    // 含两个"忌"的脚本也合法。
    f.ji[1] = 4;
    n = sp_voice_plan(SP_SIGN_CANCER, &f, plan, 32);
    bool found4 = false;
    for (size_t k = 0; k < n; ++k) found4 |= (plan[k] == spv_activity(4));
    CHECK(found4);
}

int main(void)
{
    test_dates();
    test_signs();
    test_fortune();
    test_pet();
    test_voice();
    if (s_failures == 0) {
        printf("test_sp_model: ALL PASS\n");
        return 0;
    }
    printf("test_sp_model: %d FAILURE(S)\n", s_failures);
    return 1;
}
