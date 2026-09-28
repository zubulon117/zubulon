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
    "看电影", "跑步", "写邮件", "复盘", "午睡", "聊天",
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
    "别怕慢，星光会替你照亮前方的路。",
    "今天也要加油呀，好运正在路上。",
    "把烦恼折成纸飞机，让它飞进夜空里。",
    "愿你的笑容比星星更亮，好运自然来。",
};

// 当日主题标题（24 种，四字且全部唯一，原创文案，不与任何网站重复）。
static const char *const s_themes[SP_THEME_COUNT] = {
    "星轨交汇", "灵感涌动", "顺势而为", "静待花开",
    "心有灵犀", "稳中求进", "柳暗花明", "守得云开",
    "拨云见日", "厚积薄发", "水到渠成", "星光不负",
    "乘风破浪", "静水流深", "云开月明", "花开满枝",
    "一路生花", "诸事顺遂", "否极泰来", "峰回路转",
    "鸿运当头", "如愿以偿", "未来可期", "好梦成真",
};

// 五维短评（每维 3 句，qid 0..2 对应星级 1-2/3/4-5）。
// 页面短评列从 x=118 起仅约 122px（16px 字体约 7 字），每条必须 <= 7 字。
// 短评只描述状态、不给活动建议，避免与同屏随机抽取的宜忌活动互相矛盾。
static const char *const s_quotes[SP_DIM_COUNT][SP_DIM_QUOTE_COUNT] = {
    { // 综合
        "低调蓄力为宜",
        "平稳即是好运",
        "好运全开之日",
    },
    { // 爱情
        "桃花略显平静",
        "平淡也有温度",
        "桃花运势正旺",
    },
    { // 事业
        "冷静应对波折",
        "稳扎稳打即可",
        "新想法受欢迎",
    },
    { // 财运
        "偏财暂未敲门",
        "收支平衡安稳",
        "或有意外收获",
    },
    { // 健康
        "身体电量偏低",
        "记得多喝热水",
        "元气满满在线",
    },
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

const char *sp_theme_title(uint8_t id)
{
    return id < SP_THEME_COUNT ? s_themes[id] : s_themes[0];
}

const char *sp_dim_quote(uint8_t dim, uint8_t qid)
{
    if (dim >= SP_DIM_COUNT) dim = 0;
    if (qid >= SP_DIM_QUOTE_COUNT) qid = 0;
    return s_quotes[dim][qid];
}

// 星缘图鉴：各星座性格一句话（≤10 字，只显示不朗读）。
static const char *const s_personalities[SP_SIGN_COUNT] = {
    "一点就燃的行动派",   // 白羊
    "慢热踏实的收藏家",   // 金牛
    "话题不落的风信使",   // 双子
    "温柔护家的月亮心",   // 巨蟹
    "自带聚光的小太阳",   // 狮子
    "细节控的完美工匠",   // 处女
    "优雅平衡的调酒师",   // 天秤
    "深藏不露的夜行者",   // 天蝎
    "自由如风的远行者",   // 射手
    "默默攀峰的实干家",   // 摩羯
    "脑洞清奇的星际客",   // 水瓶
    "心软浪漫的造梦师",   // 双鱼
};

const char *sp_sign_personality(uint8_t sign)
{
    return sign < SP_SIGN_COUNT ? s_personalities[sign] : s_personalities[0];
}
