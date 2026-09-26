// main/sp_record.c —— 定长存档记录编校实现。纯标准 C（CRC32 自带表），主机可测。
#include "sp_record.h"

#include <string.h>

// 负载字段偏移（共 34 字节，余量预留将来扩展）。
#define F_BIRTH_Y      0   // i16
#define F_BIRTH_M      2   // u8
#define F_BIRTH_D      3
#define F_SIGN         4
#define F_BOND         5
#define F_MOOD         6
#define F_COINS        7   // u16
#define F_STAGE        9
#define F_FEED         10
#define F_PET_CNT      11
#define F_CHECKED      12
#define F_STREAK       13  // u16
#define F_LC_Y         15  // i16，上次签到
#define F_LC_M         17
#define F_LC_D         18
#define F_LAST_PET_MIN 19  // i32
#define F_LD_Y         23  // i16，上次结算日
#define F_LD_M         25
#define F_LD_D         26
#define F_FORTUNE_SEEN 27
#define F_PAYLOAD_LEN  28

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

uint32_t sp_record_crc32(const uint8_t *data, size_t len)
{
    return crc32_update(0, data, len);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_i16(uint8_t *p, int16_t v)
{
    put_u16(p, (uint16_t)v);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t get_i16(const uint8_t *p)
{
    return (int16_t)get_u16(p);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void sp_record_from_pet(const sp_pet_t *pet_in, uint32_t sequence,
                        sp_record_t *out)
{
    sp_pet_t pet;
    memcpy(&pet, pet_in, sizeof(pet));
    sp_pet_sanitize(&pet);

    memset(out, 0, sizeof(*out));
    uint8_t *h = out->bytes;
    put_u32(h + 0, SP_RECORD_MAGIC);
    put_u16(h + 4, SP_RECORD_VERSION);
    put_u16(h + 6, F_PAYLOAD_LEN);
    put_u32(h + 8, sequence);

    uint8_t *p = h + SP_RECORD_HEADER;
    put_i16(p + F_BIRTH_Y, pet.birthday.year);
    p[F_BIRTH_M] = pet.birthday.month;
    p[F_BIRTH_D] = pet.birthday.day;
    p[F_SIGN] = pet.sign;
    p[F_BOND] = pet.bond;
    p[F_MOOD] = pet.mood;
    put_u16(p + F_COINS, pet.coins);
    p[F_STAGE] = pet.stage;
    p[F_FEED] = pet.feed_today;
    p[F_PET_CNT] = pet.pet_today;
    p[F_CHECKED] = pet.checked_in;
    put_u16(p + F_STREAK, pet.streak);
    put_i16(p + F_LC_Y, pet.last_checkin.year);
    p[F_LC_M] = pet.last_checkin.month;
    p[F_LC_D] = pet.last_checkin.day;
    put_u32(p + F_LAST_PET_MIN, (uint32_t)pet.last_pet_min);
    put_i16(p + F_LD_Y, pet.last_day.year);
    p[F_LD_M] = pet.last_day.month;
    p[F_LD_D] = pet.last_day.day;
    p[F_FORTUNE_SEEN] = pet.fortune_seen;

    put_u32(h + SP_RECORD_SIZE - 4,
            crc32_update(0, h, SP_RECORD_SIZE - 4));
}

bool sp_record_validate_buf(const uint8_t *raw, size_t len)
{
    if (raw == NULL || len != SP_RECORD_SIZE) return false;
    if (get_u32(raw + 0) != SP_RECORD_MAGIC) return false;
    if (get_u16(raw + 4) != SP_RECORD_VERSION) return false;
    uint16_t plen = get_u16(raw + 6);
    if (plen == 0 || plen > SP_RECORD_PAYLOAD) return false;
    uint32_t stored = get_u32(raw + SP_RECORD_SIZE - 4);
    uint32_t calc = crc32_update(0, raw, SP_RECORD_SIZE - 4);
    return stored == calc;
}

bool sp_record_validate(const sp_record_t *rec)
{
    return rec != NULL && sp_record_validate_buf(rec->bytes, SP_RECORD_SIZE);
}

uint32_t sp_record_sequence(const sp_record_t *rec)
{
    return get_u32(rec->bytes + 8);
}

bool sp_record_to_pet(const sp_record_t *rec, sp_pet_t *out)
{
    if (!sp_record_validate(rec)) return false;
    const uint8_t *p = rec->bytes + SP_RECORD_HEADER;

    memset(out, 0, sizeof(*out));
    out->birthday = (sp_date_t){ get_i16(p + F_BIRTH_Y),
                                 p[F_BIRTH_M], p[F_BIRTH_D] };
    out->sign = p[F_SIGN];
    out->bond = p[F_BOND];
    out->mood = p[F_MOOD];
    out->coins = get_u16(p + F_COINS);
    out->stage = p[F_STAGE];
    out->feed_today = p[F_FEED];
    out->pet_today = p[F_PET_CNT];
    out->checked_in = p[F_CHECKED];
    out->streak = get_u16(p + F_STREAK);
    out->last_checkin = (sp_date_t){ get_i16(p + F_LC_Y),
                                     p[F_LC_M], p[F_LC_D] };
    out->last_pet_min = (int32_t)get_u32(p + F_LAST_PET_MIN);
    out->last_day = (sp_date_t){ get_i16(p + F_LD_Y),
                                 p[F_LD_M], p[F_LD_D] };
    out->fortune_seen = p[F_FORTUNE_SEEN];

    sp_pet_sanitize(out);
    return true;
}

const sp_record_t *sp_record_newer(const sp_record_t *a, const sp_record_t *b)
{
    bool a_ok = sp_record_validate(a);
    bool b_ok = sp_record_validate(b);
    if (a_ok && b_ok) {
        return sp_record_sequence(b) > sp_record_sequence(a) ? b : a;
    }
    if (a_ok) return a;
    if (b_ok) return b;
    return NULL;
}
