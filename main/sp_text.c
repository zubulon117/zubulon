// main/sp_text.c —— 中文词库表实现。星座顺序与 sp_model 的 SP_SIGN_* 一致。
#include "sp_text.h"

static const sp_sign_text_t s_signs[SP_SIGN_COUNT] = {
    { "白羊座", "3.21-4.19",  "火象" },
    { "金牛座", "4.20-5.20",  "土象" },
    { "双子座", "5.21-6.21",  "风象" },
    { "巨蟹座", "6.22-7.22",  "水象" },
    { "狮子座", "7.23-8.22",  "火象" },
    { "处女座", "8.23-9.22",  "土象" },
    { "天秤座", "9.23-10.23", "风象" },
    { "天蝎座", "10.24-11.22","水象" },
    { "射手座", "11.23-12.21","火象" },
    { "摩羯座", "12.22-1.19", "土象" },
    { "水瓶座", "1.20-2.18",  "风象" },
    { "双鱼座", "2.19-3.20",  "水象" },
};

static const char *const s_zodiac[SP_ZODIAC_COUNT] = {
    "鼠", "牛", "虎", "兔", "龙", "蛇",
    "马", "羊", "猴", "鸡", "狗", "猪",
};

static const char *const s_dims[SP_DIM_COUNT] = {
    "综合", "爱情", "事业", "财运", "健康",
};

static const char *const s_stages[SP_STAGE_COUNT] = {
    "星幼宠", "星伴", "星辉",
};

static const char *const s_colors_name[SP_COLOR_COUNT] = {
    "红", "橙", "黄", "绿", "青", "蓝",
    "紫", "粉", "白", "灰", "金", "银",
};

static const uint32_t s_colors_hex[SP_COLOR_COUNT] = {
    0xE8504Fu, 0xF28B3Bu, 0xF6C453u, 0x6FC06Au,
    0x3FC7B8u, 0x4C8FE0u, 0x9B6BD0u, 0xF27AA0u,
    0xEFF1F8u, 0x9AA3B8u, 0xD9A93Eu, 0xC3CADAu,
};

// 顺序必须与 sp_voice_script.h 的 SPV_A_* 一致。
static const char *const s_activities[SP_ACTIVITY_COUNT] = {
    "表白", "加班", "出行", "购物", "聚会", "运动",
    "学习", "理财", "沟通", "休息", "整理", "早睡",
    "约会", "投资", "旅行", "烹饪", "唱歌", "发呆",
};

static const char *const s_items[SP_ITEM_COUNT] = {
    "四叶草", "星星瓶", "月光石", "小铃铛",
    "羽毛笔", "水晶球", "糖果罐", "星星书签",
    "钥匙扣", "小香薰", "风铃",   "贝壳",
};

static const char *const s_wishes[SP_WISH_COUNT] = {
    "星光不问赶路人，今天的你自有好运。",
    "慢一点也没关系，月亮会等你。",
    "把心愿说给星星听，它会替你记住。",
    "今天适合笑出声，好运喜欢开心的人。",
    "你的温柔正在发光，记得也抱抱自己。",
    "小小的一步，也是星图里崭新的一笔。",
    "愿你今天遇到的风，都是温柔的。",
    "星星睡了，你的努力还亮着，睡前再夸自己一次。",
};

const sp_sign_text_t *sp_sign_text(uint8_t sign)
{
    return sign < SP_SIGN_COUNT ? &s_signs[sign] : &s_signs[0];
}

const char *sp_zodiac_name(uint8_t zodiac)
{
    return zodiac < SP_ZODIAC_COUNT ? s_zodiac[zodiac] : s_zodiac[0];
}

const char *sp_dim_name(uint8_t dim)
{
    return dim < SP_DIM_COUNT ? s_dims[dim] : s_dims[0];
}

const char *sp_stage_name(uint8_t stage)
{
    return stage < SP_STAGE_COUNT ? s_stages[stage] : s_stages[0];
}

const char *sp_color_name(uint8_t color_id)
{
    return color_id < SP_COLOR_COUNT ? s_colors_name[color_id] : s_colors_name[0];
}

uint32_t sp_color_hex(uint8_t color_id)
{
    return color_id < SP_COLOR_COUNT ? s_colors_hex[color_id] : s_colors_hex[0];
}

const char *sp_activity_name(uint8_t id)
{
    return id < SP_ACTIVITY_COUNT ? s_activities[id] : s_activities[0];
}

const char *sp_item_name(uint8_t id)
{
    return id < SP_ITEM_COUNT ? s_items[id] : s_items[0];
}

const char *sp_wish_text(uint8_t id)
{
    return id < SP_WISH_COUNT ? s_wishes[id] : s_wishes[0];
}
