// tests/test_sp_dex.c —— 星缘图鉴主机测试：访客推导与位图操作。
#include <stdio.h>

#include "sp_model.h"

static int failures;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            printf("FAIL: %s\n", (msg));                              \
            failures++;                                               \
        }                                                             \
    } while (0)

int main(void)
{
    // 确定性：同 (own, date) 恒等。
    sp_date_t d = {2026, 9, 28};
    CHECK(sp_dex_visitor(0, d) == sp_dex_visitor(0, d),
          "visitor deterministic");
    CHECK(sp_dex_visitor(9, d) == sp_dex_visitor(9, d),
          "visitor deterministic (own=9)");

    // 12 个本命 x 400 天：访客恒合法且恒异于自己。
    for (uint8_t own = 0; own < 12; own++) {
        for (int32_t i = 0; i < 400; i++) {
            sp_date_t t = sp_date_from_serial(20000 + i);
            uint8_t v = sp_dex_visitor(own, t);
            CHECK(v < 12, "visitor in range");
            CHECK(v != own, "visitor != own");
        }
    }

    // 400 天访客分布不退化（同一本命应见过 >= 8 种星友）。
    {
        uint16_t seen = 0;
        for (int32_t i = 0; i < 400; i++) {
            sp_date_t t = sp_date_from_serial(20000 + i);
            seen |= (uint16_t)(1u << sp_dex_visitor(3, t));
        }
        int kinds = 0;
        for (int i = 0; i < 12; i++) {
            if (seen & (1u << i)) {
                kinds++;
            }
        }
        CHECK(kinds >= 8, "visitor variety over 400 days");
    }

    // 位图操作：置位、查询、集齐。
    uint16_t m = 0;
    CHECK(!sp_dex_has(m, 0), "empty mask has none");
    m = sp_dex_visit(m, 5);
    CHECK(sp_dex_has(m, 5), "visit sets bit");
    CHECK(!sp_dex_has(m, 6), "other bit untouched");
    CHECK(!sp_dex_complete(m), "incomplete after 1");
    for (uint8_t i = 0; i < 12; i++) {
        m = sp_dex_visit(m, i);
    }
    CHECK(m == SP_DEX_ALL, "all bits set after 12 visits");
    CHECK(sp_dex_complete(m), "complete when SP_DEX_ALL");

    // 越界防御。
    CHECK(!sp_dex_has(m, 12), "OOB sign never has");
    CHECK(sp_dex_visit(m, 200) == m, "OOB visit returns mask unchanged");
    CHECK(sp_dex_visitor(12, d) != 12, "OOB own clamped");

    if (failures) {
        printf("test_sp_dex: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_sp_dex: ALL PASS\n");
    return 0;
}
