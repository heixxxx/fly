// lefdef 解析基线单测（演进三步走第 2 步：正确性回归安全网）。
// 上游金标 complete.5.8.lef / complete.5.8.def 的结构化计数断言：
//   - LEF 侧：期望值人工读文件核定后写死（大小写不敏感计数——金标文件
//     故意以小写关键字验证 parser 行为）；
//   - DEF 侧：段头声明字面 ↔ start 回调读数、段内实际条目 ↔ 对象回调
//     计数，两条独立解析路径各自印证（金标文件声明值与实际条目数普遍
//     不一致，见测试内说明）；另加 DIEAREA 非空断言。
// 该文件为 lef/def 上游解析器的最小下游消费者（后续 T4/T5 适配层前置）。
#include "baseline_drivers.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

namespace {

namespace fs = std::filesystem;

// bazel test 注入 TEST_SRCDIR/TEST_WORKSPACE：定位 runfiles 内的 data
// （同 emir/lib 单测的样例定位方式）
fs::path test_data(const char* rel) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace / rel;
    }
    return fs::path(rel);
}

TEST(LefBaselineTest, Complete58Counts) {
    lef_baseline::LefCounts counts;
    ASSERT_EQ(lef_baseline::parse_lef(
                  test_data("src/lefdef/lef/TEST/complete.5.8.lef").string().c_str(),
                  &counts),
              0)
        << "LEF 基线文件不可读";

    // 解析成功（lefrRead 返回 0）
    EXPECT_EQ(counts.status, 0);

    // 人工核定（grep -iE，LEF 关键字大小写不敏感——金标文件第 448 行
    // 故意以小写 "layer OVERLAP" 验证此行为，大小写敏感 grep 会漏计）：
    // 行首 MACRO 14（PROPERTYDEFINITIONS 段的同名属性行为缩进行不混入）、
    // 行首 LAYER 26、行首 VIA 10、MACRO 块内 PIN 35（38 行 - 3 行
    // PROPERTYDEFINITIONS 的 PIN 属性定义）。
    EXPECT_EQ(counts.macros, 14);
    EXPECT_EQ(counts.layers, 26);
    EXPECT_EQ(counts.vias, 10);
    EXPECT_EQ(counts.pins, 35);
}

TEST(DefBaselineTest, Complete58SectionDeclarationsMatchObjects) {
    def_baseline::DefCounts counts;
    ASSERT_EQ(def_baseline::parse_def(
                  test_data("src/lefdef/def/TEST/complete.5.8.def").string().c_str(),
                  &counts),
              0)
        << "DEF 基线文件不可读";

    // 解析成功（defrRead 返回 0）
    EXPECT_EQ(counts.status, 0);

    // 段基准核定（python 按段扫描）：declared = 段头声明字面值（
    // "SECTION N ;" 行）；entries = 段内以 "- " 开头的实际条目数。
    // **重要事实**：金标文件的段头声明值与段内实际条目数普遍不一致
    // （VIAS 6/11、COMPONENTS 13/43、PINS 11/19……上游故意让 parser
    // 不依赖声明值解析，声明仅作容量提示），故自洽断言拆成两条独立
    // 印证链：文件段头字面 ↔ start 回调读数（每段恰一次）；段内实际
    // 条目 ↔ 对象回调计数。
    struct Section {
        int declared;
        int entries;
        int start_count;
        int start_declared;
        int objects;
        const char* name;
    };
    const Section sections[] = {
        {6, 11, counts.via_start, counts.via_start_declared, counts.vias, "VIAS"},
        {13, 43, counts.comp_start, counts.comp_start_declared, counts.comps, "COMPONENTS"},
        {11, 19, counts.pins_start, counts.pins_start_declared, counts.pins, "PINS"},
        {6, 12, counts.net_start, counts.net_start_declared, counts.nets, "NETS"},
        {5, 6, counts.snet_start, counts.snet_start_declared, counts.snets, "SPECIALNETS"},
        {2, 2, counts.region_start, counts.region_start_declared, counts.regions, "REGIONS"},
        {3, 3, counts.groups_start, counts.groups_start_declared, counts.groups, "GROUPS"},
        {3, 3, counts.slot_start, counts.slot_start_declared, counts.slots, "SLOTS"},
        {5, 6, counts.fill_start, counts.fill_start_declared, counts.fills, "FILLS"},
        {8, 11, counts.blockage_start, counts.blockage_start_declared, counts.blockages, "BLOCKAGES"},
        {1, 2, counts.ndr_start, counts.ndr_start_declared, counts.ndrs, "NONDEFAULTRULES"},
        {10, 10, counts.styles_start, counts.styles_start_declared, counts.styles, "STYLES"},
    };
    for (const Section& s : sections) {
        EXPECT_EQ(s.start_count, 1) << s.name << " 段 start 回调应恰一次";
        EXPECT_EQ(s.start_declared, s.declared) << s.name << " 段头声明值读取";
        EXPECT_EQ(s.objects, s.entries) << s.name << " 段对象回调计数";
    }

    // GROUPS 段附带验证：group 名回调逐组触发
    EXPECT_EQ(counts.group_names, 3);

    // UNITS DISTANCE MICRONS 1000
    EXPECT_DOUBLE_EQ(counts.units, 1000.0);
}

TEST(DefBaselineTest, DieAreaNonEmpty) {
    def_baseline::DefCounts counts;
    ASSERT_EQ(def_baseline::parse_def(
                  test_data("src/lefdef/def/TEST/complete.5.8.def").string().c_str(),
                  &counts),
              0);

    // DIEAREA 恰触发一次，经完整点集聚合包围盒非空：
    // 文件 72-73 行（语句跨两行）共 6 点：
    //   "DIEAREA ( -190000 -120000 ) ( -190000 350000 ) ( 190000 350000 )
    //            ( 190000 190000 ) ( 190360 190000 ) ( 190360 -120000 ) ;"
    // （defiBox 的 xl/xh 是前两点兼容赋值，包围盒由 driver 自行聚合）
    EXPECT_EQ(counts.die_area, 1);
    EXPECT_EQ(counts.die_area_points, 6);
    EXPECT_EQ(counts.die_xl, -190000);
    EXPECT_EQ(counts.die_yl, -120000);
    EXPECT_EQ(counts.die_xh, 190360);
    EXPECT_EQ(counts.die_yh, 350000);
    EXPECT_GT(counts.die_xh, counts.die_xl);
    EXPECT_GT(counts.die_yh, counts.die_yl);
}

TEST(BaselineNegativeTest, UnreadableFileReportsFailure) {
    // 输入不可读（dev-rules §7 的第一类异常场景）：driver 返回 -1
    lef_baseline::LefCounts lef_counts;
    EXPECT_EQ(lef_baseline::parse_lef("/nonexistent/baseline.lef", &lef_counts), -1);

    def_baseline::DefCounts def_counts;
    EXPECT_EQ(def_baseline::parse_def("/nonexistent/baseline.def", &def_counts), -1);
}

}  // namespace
