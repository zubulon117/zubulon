// main/sp_model.c —— 命定星宠纯逻辑模型实现。仅用标准 C 头，可主机直编译。
#include "sp_model.h"

// --------------------------------------------------------- 日期运算 ----
// Howard Hinnant 公历-序数互转（1970-01-01 为 0，支持负年份区间）。
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = (int64_t)y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static sp_date_t civil_from_days(int64_t z)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - yoe * 365 - yoe / 4 + yoe / 100;  // 0..365
    int64_t mp = (5 * doy + 2) / 153;                     // 0..11
    int64_t d = doy - (153 * mp + 2) / 5 + 1;             // 1..31
    unsigned m = (unsigned)(mp + (mp < 10 ? 3 : -9));
    y += (m <= 2);
    sp_date_t out = { (int16_t)y, (uint8_t)m, (uint8_t)d };
    return out;
}

bool sp_is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t sp_days_in_month(int year, uint8_t month)
{
    static const uint8_t days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) return 0;
    if (month == 2 && sp_is_leap(year)) return 29;
    return days[month - 1];
}

bool sp_date_valid(sp_date_t d)
{
    return d.year >= SP_YEAR_MIN && d.year <= SP_YEAR_MAX &&
           d.month >= 1 && d.month <= 12 &&
           d.day >= 1 && d.day <= sp_days_in_month(d.year, d.month);
}

bool sp_date_equal(sp_date_t a, sp_date_t b)
{
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

int32_t sp_date_serial(sp_date_t d)
{
    return (int32_t)days_from_civil(d.year, d.month, d.day);
}

int32_t sp_date_diff(sp_date_t b, sp_date_t a)
{
    return sp_date_serial(b) - sp_date_serial(a);
}

sp_date_t sp_date_from_serial(int32_t serial)
{
    return civil_from_days(serial);
}

sp_date_t sp_date_add_days(sp_date_t d, int32_t delta)
{
    return civil_from_days(days_from_civil(d.year, d.month, d.day) + delta);
}

uint8_t sp_date_weekday(sp_date_t d)
{
    // 1970-01-01 为周四(=4)，(serial+4) 对 7 取模，0=周日。
    int32_t w = (sp_date_serial(d) + 4) % 7;
    return (uint8_t)(w < 0 ? w + 7 : w);
}

// ------------------------------------------------------- 星座 / 生肖 ----
// 每月分界日：day >= cutoff 即进入该月"后段星座"。
static const uint8_t s_sign_cutoff[12] = {
    20, 19, 21, 20, 21, 22, 23, 23, 23, 24, 23, 22
};
// 分界日（含）之后的星座序号，顺序 1..12 月。
static const uint8_t s_sign_after[12] = {
    SP_SIGN_AQUARIUS, SP_SIGN_PISCES, SP_SIGN_ARIES, SP_SIGN_TAURUS,
    SP_SIGN_GEMINI, SP_SIGN_CANCER, SP_SIGN_LEO, SP_SIGN_VIRGO,
    SP_SIGN_LIBRA, SP_SIGN_SCORPIO, SP_SIGN_SAGITTARIUS, SP_SIGN_CAPRICORN,
};

uint8_t sp_zodiac_sign(uint8_t month, uint8_t day)
{
    if (month < 1 || month > 12) return 0xFF;
    uint8_t after = s_sign_after[month - 1];
    if (day < 1 || day > 31) return 0xFF;
    if (day >= s_sign_cutoff[month - 1]) return after;
    return (after + 11) % 12;  // 分界前为上一个星座
}

uint8_t sp_chinese_zodiac(int year)
{
    int z = (year - 4) % 12;
    if (z < 0) z += 12;
    return (uint8_t)z;
}

// ----------------------------------------------------------- 随机数 ----
void sp_rng_seed(sp_rng_t *rng, uint32_t seed)
{
    rng->state = seed != 0 ? seed : 0x9E3779B9u;
}

uint32_t sp_rng_next(sp_rng_t *rng)
{
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t sp_rng_range(sp_rng_t *rng, uint32_t lo, uint32_t hi)
{
    if (hi < lo) return lo;
    uint32_t span = hi - lo + 1;
    return lo + (sp_rng_next(rng) % span);
}

uint32_t sp_day_seed(uint8_t sign, sp_date_t date)
{
    uint32_t h = (uint32_t)sp_date_serial(date);
    h ^= ((uint32_t)sign + 1u) * 0x9E3779B1u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h != 0 ? h : 0x9E3779B9u;
}

// ------------------------------------------------------------- 图鉴 ----
uint8_t sp_dex_visitor(uint8_t own_sign, sp_date_t date)
{
    if (own_sign >= SP_SIGN_COUNT) {
        own_sign = 0;
    }
    // 除自己外的 11 个星座中确定性取一：0..10 映射跳过 own_sign。
    sp_rng_t rng;
    sp_rng_seed(&rng, sp_day_seed(own_sign, date));
    uint32_t idx = sp_rng_range(&rng, 0, SP_SIGN_COUNT - 2);
    return (uint8_t)(idx < own_sign ? idx : idx + 1);
}

bool sp_dex_has(uint16_t mask, uint8_t sign)
{
    return sign < SP_SIGN_COUNT && ((mask >> sign) & 1u) != 0u;
}

uint16_t sp_dex_visit(uint16_t mask, uint8_t sign)
{
    if (sign < SP_SIGN_COUNT) {
        mask |= (uint16_t)(1u << sign);
    }
    return mask;
}

bool sp_dex_complete(uint16_t mask)
{
    return (mask & SP_DEX_ALL) == SP_DEX_ALL;
}

// ------------------------------------------------------------- 运势 ----
static uint8_t clamp_star(int v)
{
    if (v < 1) return 1;
    if (v > 5) return 5;
    return (uint8_t)v;
}

void sp_fortune_today(uint8_t sign, sp_date_t date, sp_fortune_t *out)
{
    sp_rng_t rng;
    sp_rng_seed(&rng, sp_day_seed(sign, date));

    for (int i = 1; i < SP_DIM_COUNT; ++i) {
        out->stars[i] = (uint8_t)sp_rng_range(&rng, 1, 5);
    }
    // 综合由四维加权取整：爱情5 事业6 财运5 健康4（总权 20）。
    int weighted = 5 * out->stars[SP_DIM_LOVE] + 6 * out->stars[SP_DIM_WORK] +
                   5 * out->stars[SP_DIM_WEALTH] + 4 * out->stars[SP_DIM_HEALTH];
    out->stars[SP_DIM_OVERALL] = clamp_star((weighted + 10) / 20);

    // 方案 B：主题标题、速配星座、各维短评。
    out->theme_id   = (uint8_t)sp_rng_range(&rng, 0, SP_THEME_COUNT - 1);
    out->match_sign = (uint8_t)sp_rng_range(&rng, 0, SP_SIGN_COUNT - 1);
    for (int i = 0; i < SP_DIM_COUNT; ++i) {
        out->quote_id[i] = (uint8_t)sp_rng_range(&rng, 0, SP_DIM_QUOTE_COUNT - 1);
    }

    out->color_id = (uint8_t)sp_rng_range(&rng, 0, SP_COLOR_COUNT - 1);
    out->lucky_num = (uint8_t)sp_rng_range(&rng, 1, 99);
    out->item_id = (uint8_t)sp_rng_range(&rng, 0, SP_ITEM_COUNT - 1);

    // 部分 Fisher-Yates：从 24 个活动中抽出 4 个互不相同的 id。
    uint8_t pool[SP_ACTIVITY_COUNT];
    for (int i = 0; i < SP_ACTIVITY_COUNT; ++i) pool[i] = (uint8_t)i;
    for (int i = 0; i < 4; ++i) {
        uint32_t j = (uint32_t)i + sp_rng_range(&rng, 0,
                                                (uint32_t)(SP_ACTIVITY_COUNT - 1 - i));
        uint8_t t = pool[i];
        pool[i] = pool[j];
        pool[j] = t;
    }
    out->yi[0] = pool[0];
    out->yi[1] = pool[1];
    out->ji[0] = pool[2];
    // 约四成日子有两项"忌"。
    out->ji[1] = (sp_rng_next(&rng) % 5 < 2) ? pool[3] : SP_ID_NONE;

    out->wish_id = (uint8_t)sp_rng_range(&rng, 0, SP_WISH_COUNT - 1);
}

uint8_t sp_mood_tone(uint8_t mood, uint8_t overall_stars)
{
    return (overall_stars >= 4 || mood >= 75) ? 1 : 0;
}

// ------------------------------------------------------- 语音脚本 ----
spv_id_t spv_sign(uint8_t sign)
{
    return (spv_id_t)(SPV_SIGN_ARIES + (sign % SP_SIGN_COUNT));
}

spv_id_t spv_color(uint8_t color_id)
{
    return (spv_id_t)(SPV_C_RED + (color_id % SP_COLOR_COUNT));
}

spv_id_t spv_activity(uint8_t activity_id)
{
    return (spv_id_t)(SPV_A_BIAOBAI + (activity_id % SP_ACTIVITY_COUNT));
}

static size_t append_clip(spv_id_t id, spv_id_t *out, size_t cap, size_t n)
{
    if (out != NULL && n < cap) out[n] = id;
    return n + 1;
}

size_t sp_voice_number(int number, spv_id_t *out, size_t cap)
{
    size_t n = 0;
    if (number < 0) number = 0;
    if (number > 99) number = 99;

    if (number <= 10) {
        n = append_clip((spv_id_t)(SPV_D0 + number), out, cap, n);
    } else if (number < 20) {
        n = append_clip(SPV_D10, out, cap, n);
        int unit = number - 10;
        if (unit > 0) n = append_clip((spv_id_t)(SPV_D0 + unit), out, cap, n);
    } else {
        int tens = number / 10;
        int unit = number % 10;
        n = append_clip((spv_id_t)(SPV_D0 + tens), out, cap, n);
        n = append_clip(SPV_D10, out, cap, n);
        if (unit > 0) n = append_clip((spv_id_t)(SPV_D0 + unit), out, cap, n);
    }
    return n;
}

size_t sp_voice_plan(uint8_t sign, const sp_fortune_t *f,
                     spv_id_t *out, size_t cap)
{
    size_t n = 0;
    n = append_clip(SPV_INTRO, out, cap, n);
    n = append_clip(spv_sign(sign), out, cap, n);
    n = append_clip(SPV_OVERALL, out, cap, n);
    n = append_clip((spv_id_t)(SPV_D1 + (f->stars[SP_DIM_OVERALL] - 1)),
                    out, cap, n);  // 一..五
    n = append_clip(SPV_STAR, out, cap, n);
    n = append_clip(SPV_LUCKY_COLOR, out, cap, n);
    n = append_clip(spv_color(f->color_id), out, cap, n);
    n = append_clip(SPV_COLOR_SUFFIX, out, cap, n);
    n = append_clip(SPV_LUCKY_NUM, out, cap, n);
    spv_id_t digits[3];
    size_t dn = sp_voice_number(f->lucky_num, digits, 3);
    for (size_t i = 0; i < dn; ++i) n = append_clip(digits[i], out, cap, n);
    n = append_clip(SPV_YI, out, cap, n);
    n = append_clip(spv_activity(f->yi[0]), out, cap, n);
    n = append_clip(spv_activity(f->yi[1]), out, cap, n);
    n = append_clip(SPV_JI, out, cap, n);
    n = append_clip(spv_activity(f->ji[0]), out, cap, n);
    if (f->ji[1] != SP_ID_NONE) {
        n = append_clip(spv_activity(f->ji[1]), out, cap, n);
    }
    n = append_clip(SPV_ENDING, out, cap, n);
    return n;
}

// ------------------------------------------------------------- 养成 ----
uint8_t sp_bond_stage(uint8_t bond)
{
    if (bond >= SP_STAGE3_BOND) return 2;
    if (bond >= SP_STAGE2_BOND) return 1;
    return 0;
}

static void refresh_stage(sp_pet_t *pet)
{
    pet->stage = sp_bond_stage(pet->bond);
}

void sp_pet_new(sp_pet_t *pet, sp_date_t birthday, sp_date_t today)
{
    sp_date_t zero = { 0, 0, 0 };
    pet->birthday = birthday;
    pet->sign = sp_zodiac_sign(birthday.month, birthday.day);
    pet->bond = 0;
    pet->mood = 60;
    pet->coins = 0;
    pet->stage = 0;
    pet->feed_today = 0;
    pet->pet_today = 0;
    pet->checked_in = 0;
    pet->streak = 0;
    pet->last_checkin = zero;
    pet->last_pet_min = -1;
    pet->last_day = today;
    pet->fortune_seen = 0;
    sp_pet_sanitize(pet);
}

void sp_pet_sanitize(sp_pet_t *pet)
{
    if (!sp_date_valid(pet->birthday)) {
        pet->birthday = (sp_date_t){ 2000, 1, 1 };
    }
    pet->sign = sp_zodiac_sign(pet->birthday.month, pet->birthday.day);
    if (pet->mood > SP_MOOD_MAX) pet->mood = SP_MOOD_MAX;
    if (pet->bond > SP_BOND_MAX) pet->bond = SP_BOND_MAX;
    if (pet->feed_today > SP_FEED_PER_DAY) pet->feed_today = SP_FEED_PER_DAY;
    if (pet->pet_today > SP_PET_PER_DAY) pet->pet_today = SP_PET_PER_DAY;
    pet->checked_in = pet->checked_in ? 1 : 0;
    pet->fortune_seen = pet->fortune_seen ? 1 : 0;
    if (pet->streak > SP_STREAK_CAP) pet->streak = SP_STREAK_CAP;
    if (!sp_date_valid(pet->last_day)) pet->last_day = pet->birthday;
    refresh_stage(pet);
}

sp_act_result_t sp_pet_feed(sp_pet_t *pet)
{
    if (pet->feed_today >= SP_FEED_PER_DAY) return SP_ACT_LIMIT;
    pet->feed_today++;
    pet->mood = (uint8_t)(pet->mood + SP_FEED_MOOD > SP_MOOD_MAX
                          ? SP_MOOD_MAX : pet->mood + SP_FEED_MOOD);
    pet->bond = (uint8_t)(pet->bond + SP_FEED_BOND > SP_BOND_MAX
                          ? SP_BOND_MAX : pet->bond + SP_FEED_BOND);
    refresh_stage(pet);
    return SP_ACT_OK;
}

sp_act_result_t sp_pet_pet(sp_pet_t *pet, int32_t now_min)
{
    if (pet->pet_today >= SP_PET_PER_DAY) return SP_ACT_LIMIT;
    // now_min < 0 表示时钟不可用（调用方约定）；时钟回拨导致差值为负时
    // 也不判冷却，避免设备永远提示"刚摸过"。
    if (now_min >= 0 && pet->last_pet_min >= 0 &&
        now_min - pet->last_pet_min < SP_PET_COOLDOWN_MIN &&
        now_min - pet->last_pet_min >= 0) {
        return SP_ACT_COOLDOWN;
    }
    pet->pet_today++;
    pet->last_pet_min = now_min;
    pet->mood = (uint8_t)(pet->mood + SP_PET_MOOD > SP_MOOD_MAX
                          ? SP_MOOD_MAX : pet->mood + SP_PET_MOOD);
    if (pet->bond + SP_PET_BOND <= SP_BOND_MAX) pet->bond += SP_PET_BOND;
    refresh_stage(pet);
    return SP_ACT_OK;
}

sp_act_result_t sp_pet_checkin(sp_pet_t *pet, sp_date_t today)
{
    if (pet->checked_in) return SP_ACT_DONE;
    sp_date_t yesterday = sp_date_add_days(today, -1);
    if (pet->streak > 0 && sp_date_equal(pet->last_checkin, yesterday)) {
        pet->streak = (uint16_t)(pet->streak + 1 > SP_STREAK_CAP
                                 ? SP_STREAK_CAP : pet->streak + 1);
    } else {
        pet->streak = 1;
    }
    uint16_t bonus = (uint16_t)((pet->streak - 1 > SP_CHECKIN_BONUS_CAP
                                 ? SP_CHECKIN_BONUS_CAP : pet->streak - 1)
                                * SP_CHECKIN_BONUS_STEP);
    pet->coins = (uint16_t)(pet->coins + SP_CHECKIN_COINS + bonus);
    pet->bond = (uint8_t)(pet->bond + SP_CHECKIN_BOND > SP_BOND_MAX
                          ? SP_BOND_MAX : pet->bond + SP_CHECKIN_BOND);
    pet->checked_in = 1;
    pet->last_checkin = today;
    refresh_stage(pet);
    return SP_ACT_OK;
}

uint32_t sp_pet_advance_day(sp_pet_t *pet, sp_date_t current)
{
    int32_t d = sp_date_diff(current, pet->last_day);
    if (d <= 0) return 0;
    uint32_t applied = (uint32_t)d > SP_MAX_CATCHUP ? SP_MAX_CATCHUP : (uint32_t)d;
    for (uint32_t i = 0; i < applied; ++i) {
        pet->mood = pet->mood > SP_DAILY_MOOD_DECAY
                    ? (uint8_t)(pet->mood - SP_DAILY_MOOD_DECAY) : 0;
        pet->bond = pet->bond > SP_DAILY_BOND_DECAY
                    ? (uint8_t)(pet->bond - SP_DAILY_BOND_DECAY) : 0;
    }
    pet->feed_today = 0;
    pet->pet_today = 0;
    pet->checked_in = 0;
    pet->fortune_seen = 0;
    pet->last_day = current;
    refresh_stage(pet);
    return applied;
}
