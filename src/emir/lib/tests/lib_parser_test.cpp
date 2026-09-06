// Liberty 解析适配层单测：最小 .lib 样例的结构化提取断言 + 序列化往返。
// 样例见 data/minitest.lib（组合/时序单元、模板引用、索引覆盖、未知组跳过）。
#include <emir/lib/cpp/lib_parser.h>
#include <emir/lib/cpp/lib_types.h>

#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

using namespace fly;

namespace fs = std::filesystem;

fs::path sample_path() {
    // bazel test 注入 TEST_SRCDIR/TEST_WORKSPACE：定位 runfiles 内的 data
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace / "src/emir/lib/tests/data/minitest.lib";
    }
    return fs::path("data") / "minitest.lib";
}

class LibParserTest : public ::testing::Test {
protected:
    LIBLibrary lib_;

    void SetUp() override {
        lib_ = lib_parse_lib_file(sample_path().string());
    }
};

TEST_F(LibParserTest, LibraryBasics) {
    EXPECT_EQ(lib_.name_, "minitest_typ");
    ASSERT_EQ(lib_.cells_.size(), 2u);   // INV_X1 + DFF_X1

    // 库头属性全量（含单位与阈值类 simple 属性）
    bool found_time_unit = false;
    for (const auto& a : lib_.header_attrs_) {
        if (a.name_ == "time_unit") {
            found_time_unit = (a.value_ == "1ns");
        }
    }
    EXPECT_TRUE(found_time_unit) << "time_unit attr should be captured verbatim";
}

TEST_F(LibParserTest, TemplatesCollected) {
    // power_2d + slew_1d
    ASSERT_EQ(lib_.templates_.size(), 2u);
    const CMLookupTableTemplate* pw = lib_.find_template("power_2d");
    ASSERT_NE(pw, nullptr);
    ASSERT_EQ(pw->variable_names_.size(), 2u);
    EXPECT_EQ(pw->variable_names_[0], "input_net_transition");
    EXPECT_EQ(pw->variable_names_[1], "total_output_net_capacitance");
    ASSERT_EQ(pw->index_sets_.size(), 2u);
    EXPECT_DOUBLE_EQ(pw->index_sets_[0][1], 0.5);
    EXPECT_DOUBLE_EQ(pw->index_sets_[1][1], 0.1);
}

TEST_F(LibParserTest, CombinationalCellPinsAndPowerTables) {
    const LIBCell* inv = lib_.find_cell("INV_X1");
    ASSERT_NE(inv, nullptr);
    EXPECT_DOUBLE_EQ(inv->area_, 2.0);
    EXPECT_FALSE(inv->is_sequence_cell_);
    ASSERT_EQ(inv->pins_.size(), 2u);   // A + ZN

    const LIBPin* a = nullptr;
    const LIBPin* zn = nullptr;
    for (const auto& p : inv->pins_) {
        if (p.name_ == "A") a = &p;
        if (p.name_ == "ZN") zn = &p;
    }
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->direction_, "input");
    EXPECT_DOUBLE_EQ(a->capacitance_, 1.5);

    ASSERT_NE(zn, nullptr);
    EXPECT_EQ(zn->direction_, "output");
    EXPECT_DOUBLE_EQ(zn->rise_capacitance_, 1.2);

    // internal_power：related_pin=A，两张功耗表（rise/fall）
    ASSERT_EQ(zn->internal_powers_.size(), 1u);
    EXPECT_EQ(zn->internal_powers_[0].related_pin_, "A");
    ASSERT_EQ(zn->internal_powers_[0].tables_.size(), 2u);

    const CMLookupTable& rise = zn->internal_powers_[0].tables_[0];
    EXPECT_EQ(rise.name_, "rise_power");
    EXPECT_EQ(rise.template_name_, "power_2d");
    EXPECT_EQ(rise.dim_, 2);
    // rise_power 带表级索引覆盖（与模板相同值），values 3x2
    ASSERT_EQ(rise.values_.size(), 6u);
    EXPECT_DOUBLE_EQ(rise.values_[0], 1.0);
    EXPECT_DOUBLE_EQ(rise.values_[5], 3.3);

    // 经非 const 路径取表做 resolve（fixture 成员 lib_ 可变）
    CMLookupTable* rise_m = nullptr;
    CMLookupTable* fall_m = nullptr;
    for (auto& c : lib_.cells_) {
        if (c.name_ != "INV_X1") continue;
        for (auto& p : c.pins_) {
            if (p.name_ != "ZN") continue;
            rise_m = &p.internal_powers_[0].tables_[0];
            fall_m = &p.internal_powers_[0].tables_[1];
        }
    }
    ASSERT_NE(rise_m, nullptr);
    ASSERT_NE(fall_m, nullptr);

    // 模板 resolve 后可插值：rise_power 在 (0.5, 0.05)：
    // slew=0.5 恰为格点（行 {2.0, 2.2}）；cap=0.05 在 [0.01,0.1] 的 4/9 处
    // → 2.0 + (4/9)*0.2 = 2.0888...
    rise_m->resolve_template(*lib_.find_template("power_2d"));
    ASSERT_TRUE(rise_m->is_ready());
    EXPECT_NEAR(rise_m->interpolate({0.5, 0.05}), 2.0 + (4.0 / 9.0) * 0.2, 1e-12);

    // fall_power 无表级 index_1（只带 values）→ resolve 用模板轴补齐
    EXPECT_EQ(fall_m->name_, "fall_power");
    fall_m->resolve_template(*lib_.find_template("power_2d"));
    ASSERT_TRUE(fall_m->is_ready());
    EXPECT_NEAR(fall_m->interpolate({0.5, 0.05}), 1.9 + (4.0 / 9.0) * 0.2, 1e-12);
}

TEST_F(LibParserTest, TimingArcs) {
    const LIBCell* inv = lib_.find_cell("INV_X1");
    ASSERT_NE(inv, nullptr);
    const LIBPin* zn = nullptr;
    for (const auto& p : inv->pins_) {
        if (p.name_ == "ZN") zn = &p;
    }
    ASSERT_NE(zn, nullptr);
    ASSERT_EQ(zn->timings_.size(), 1u);
    EXPECT_EQ(zn->timings_[0].related_pin_, "A");
    EXPECT_EQ(zn->timings_[0].timing_sense_, "negative_unate");
    EXPECT_EQ(zn->timings_[0].timing_type_, "combinational");
    // cell_rise/cell_fall/rise_transition/fall_transition 四张 1D 表
    ASSERT_EQ(zn->timings_[0].tables_.size(), 4u);
    const CMLookupTable& cell_rise = zn->timings_[0].tables_[0];
    EXPECT_EQ(cell_rise.name_, "cell_rise");
    EXPECT_EQ(cell_rise.template_name_, "slew_1d");
    ASSERT_EQ(cell_rise.values_.size(), 3u);
    EXPECT_NEAR(cell_rise.values_[1], 0.12, 1e-12);
}

TEST_F(LibParserTest, SequentialCell) {
    const LIBCell* dff = lib_.find_cell("DFF_X1");
    ASSERT_NE(dff, nullptr);
    EXPECT_TRUE(dff->is_sequence_cell_) << "ff group should mark sequential cell";
    const LIBPin* q = nullptr;
    for (const auto& p : dff->pins_) {
        if (p.name_ == "Q") q = &p;
    }
    ASSERT_NE(q, nullptr);
    ASSERT_EQ(q->timings_.size(), 1u);
    EXPECT_EQ(q->timings_[0].timing_type_, "rising_edge");
}

TEST_F(LibParserTest, UnknownGroupsCounted) {
    // operating_conditions 未结构化收集 → 统计里可见
    bool found = false;
    for (size_t i = 0; i < lib_.skipped_group_names_.size(); ++i) {
        if (lib_.skipped_group_names_[i] == "operating_conditions") {
            EXPECT_GE(lib_.skipped_group_counts_[i], 1);
            found = true;
        }
    }
    EXPECT_TRUE(found) << "skipped operating_conditions should be counted";
}

TEST_F(LibParserTest, CellIndexLookup) {
    lib_.build_cell_index();
    EXPECT_NE(lib_.find_cell("INV_X1"), nullptr);
    EXPECT_NE(lib_.find_cell("DFF_X1"), nullptr);
    EXPECT_EQ(lib_.find_cell("NOT_EXIST"), nullptr);
}

TEST_F(LibParserTest, SerializeRoundTrip) {
    CMString blob;
    FLY_ENCODE(lib_, blob);
    LIBLibrary back;
    FLY_DECODE(blob, LIBLibrary, back);

    EXPECT_EQ(back.name_, "minitest_typ");
    ASSERT_EQ(back.cells_.size(), 2u);
    EXPECT_EQ(back.templates_.size(), lib_.templates_.size());
    // 反序列化后结构完好（抽查时序单元标记）
    const LIBCell* dff = back.find_cell("DFF_X1");
    // find_cell 惰性建索引
    ASSERT_NE(dff, nullptr);
    EXPECT_TRUE(dff->is_sequence_cell_);
}

TEST(LibParserDeathTest, ParseErrors) {
    // 文件不存在
    EXPECT_THROW(lib_parse_lib_file("/nonexistent/xxx.lib"), std::runtime_error);
    // 语法错误
    fs::path bad = fs::temp_directory_path() / "emir_bad.lib";
    {
        std::ofstream f(bad);
        f << "library (broken) { cell (C) { pin (P) { direction : input; }\n";  // 缺右括号
    }
    EXPECT_THROW(lib_parse_lib_file(bad.string()), std::runtime_error);
    fs::remove(bad);
}

}  // namespace
