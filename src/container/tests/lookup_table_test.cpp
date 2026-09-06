// CMLookupTable / CMLookupTableTemplate 单元测试：
// 模板 resolve、1/2/3 维多线性插值、边界 clamp、序列化往返、错误路径。
#include <container/cpp/lookup_table.h>

#include <gtest/gtest.h>
#include <cmath>
#include <stdexcept>

namespace {

using fly::CMLookupTable;
using fly::CMLookupTableTemplate;

CMLookupTableTemplate make_2d_template() {
    CMLookupTableTemplate t;
    t.variable_names_ = {"input_net_transition", "total_output_net_capacitance"};
    t.index_sets_ = {{0.0, 1.0, 2.0},      // index_1：slew
                     {0.0, 10.0}};          // index_2：cap
    return t;
}

TEST(LookupTableTest, ResolveTemplateFillsAxes) {
    CMLookupTable tbl;
    tbl.name_ = "rise_power";
    tbl.dim_ = 2;
    tbl.template_name_ = "pw_2d";
    tbl.values_ = {0, 1, 10, 11, 20, 21};  // 3x2 行优先

    tbl.resolve_template(make_2d_template());
    ASSERT_TRUE(tbl.is_ready());
    EXPECT_EQ(tbl.variable_names_[0], "input_net_transition");
    // 覆盖轴保留表级索引：轴 1 先置覆盖再 resolve。
    CMLookupTable tbl2 = tbl;
    tbl2.index_sets_[1] = {0.0, 5.0};
    tbl2.values_ = {0, 2, 10, 12, 20, 22};
    tbl2.resolve_template(make_2d_template());
    EXPECT_EQ(tbl2.index_sets_[1][1], 5.0);
    EXPECT_EQ(tbl2.variable_names_[1], "total_output_net_capacitance");
}

TEST(LookupTableTest, Interpolate2DCornersExactAndCenterLinear) {
    CMLookupTable tbl;
    tbl.name_ = "rise_power";
    tbl.dim_ = 2;
    tbl.values_ = {0, 1, 10, 11, 20, 21};
    tbl.resolve_template(make_2d_template());

    // 角点精确
    EXPECT_DOUBLE_EQ(tbl.interpolate({0.0, 0.0}), 0.0);
    EXPECT_DOUBLE_EQ(tbl.interpolate({0.0, 10.0}), 1.0);
    EXPECT_DOUBLE_EQ(tbl.interpolate({2.0, 0.0}), 20.0);
    EXPECT_DOUBLE_EQ(tbl.interpolate({2.0, 10.0}), 21.0);

    // 中心 (1.0, 5.0)：slew=1.0 恰为格点（行 {10,11}），cap 中点 → 10.5
    EXPECT_DOUBLE_EQ(tbl.interpolate({1.0, 5.0}), 10.5);

    // 真四角平均 (0.5, 5.0)：slew 在 0/1 格点中点 → (0+1+10+11)/4 = 5.5
    EXPECT_DOUBLE_EQ(tbl.interpolate({0.5, 5.0}), 5.5);

    // 单轴中点 (1.0, 0.0) = v[1][0] = 10
    EXPECT_DOUBLE_EQ(tbl.interpolate({1.0, 0.0}), 10.0);
}

TEST(LookupTableTest, InterpolateClampsOutOfBounds) {
    CMLookupTable tbl;
    tbl.name_ = "rise_power";
    tbl.dim_ = 2;
    tbl.values_ = {0, 1, 10, 11, 20, 21};
    tbl.resolve_template(make_2d_template());

    EXPECT_DOUBLE_EQ(tbl.interpolate({-5.0, 99.0}), 1.0);   // clamp → (0, 10) 角点
    EXPECT_DOUBLE_EQ(tbl.interpolate({99.0, -5.0}), 20.0);  // clamp → (2, 0) 角点
}

TEST(LookupTableTest, Interpolate1D) {
    CMLookupTable tbl;
    tbl.name_ = "slew_rise";
    tbl.dim_ = 1;
    tbl.variable_names_ = {"input_net_transition"};
    tbl.index_sets_ = {{0.0, 2.0}};
    tbl.values_ = {4.0, 8.0};
    ASSERT_TRUE(tbl.is_ready());

    EXPECT_DOUBLE_EQ(tbl.interpolate({0.5}), 5.0);
    EXPECT_DOUBLE_EQ(tbl.interpolate({1.0}), 6.0);
}

TEST(LookupTableTest, InterpolateSinglePointAxis) {
    CMLookupTable tbl;
    tbl.name_ = "const_table";
    tbl.dim_ = 1;
    tbl.variable_names_ = {"input_net_transition"};
    tbl.index_sets_ = {{1.0}};
    tbl.values_ = {42.0};
    ASSERT_TRUE(tbl.is_ready());
    EXPECT_DOUBLE_EQ(tbl.interpolate({0.7}), 42.0);
}

TEST(LookupTableTest, Interpolate3D) {
    CMLookupTable tbl;
    tbl.name_ = "ccs_current";
    tbl.dim_ = 3;
    tbl.variable_names_ = {"a", "b", "c"};
    tbl.index_sets_ = {{0.0, 1.0}, {0.0, 1.0}, {0.0, 1.0}};
    // 2x2x2 行优先：values[(a,b,c)] = 4a + 2b + c
    tbl.values_ = {0, 1, 2, 3, 4, 5, 6, 7};
    ASSERT_TRUE(tbl.is_ready());

    EXPECT_DOUBLE_EQ(tbl.interpolate({0.0, 0.0, 0.0}), 0.0);
    EXPECT_DOUBLE_EQ(tbl.interpolate({1.0, 1.0, 1.0}), 7.0);
    // (0.5, 0.5, 0.5) = 全 8 角点平均 = 3.5
    EXPECT_DOUBLE_EQ(tbl.interpolate({0.5, 0.5, 0.5}), 3.5);
    // (0.25, 0, 0)：沿 a 轴 1/4 处 = 0.75*0 + 0.25*4 = 1.0
    EXPECT_DOUBLE_EQ(tbl.interpolate({0.25, 0.0, 0.0}), 1.0);
}

TEST(LookupTableTest, SerializeRoundTripTableAndTemplate) {
    CMLookupTableTemplate tmpl = make_2d_template();
    CMLookupTable tbl;
    tbl.name_ = "fall_power";
    tbl.dim_ = 2;
    tbl.template_name_ = "pw_2d";
    tbl.values_ = {0, 1, 10, 11, 20, 21};

    CMString blob;
    FLY_ENCODE(tmpl, blob);
    CMLookupTableTemplate tmpl_back;
    FLY_DECODE(blob, CMLookupTableTemplate, tmpl_back);
    EXPECT_EQ(tmpl_back.variable_names_, tmpl.variable_names_);
    EXPECT_EQ(tmpl_back.index_sets_, tmpl.index_sets_);

    CMString blob2;
    FLY_ENCODE(tbl, blob2);
    CMLookupTable tbl_back;
    FLY_DECODE(blob2, CMLookupTable, tbl_back);
    EXPECT_EQ(tbl_back.name_, "fall_power");
    EXPECT_EQ(tbl_back.dim_, 2);
    EXPECT_EQ(tbl_back.template_name_, "pw_2d");
    EXPECT_EQ(tbl_back.values_, tbl.values_);

    // 反序列化后的表同样可 resolve + 插值
    tbl_back.resolve_template(tmpl_back);
    EXPECT_DOUBLE_EQ(tbl_back.interpolate({0.5, 5.0}), 5.5);
}

TEST(LookupTableTest, ResolveRejectsValueCountMismatch) {
    CMLookupTable tbl;
    tbl.name_ = "bad";
    tbl.dim_ = 2;
    tbl.values_ = {1, 2, 3};  // 期望 6 个
    EXPECT_THROW(tbl.resolve_template(make_2d_template()), std::invalid_argument);
}

TEST(LookupTableTest, InterpolateRejectsWrongCoordCount) {
    CMLookupTable tbl;
    tbl.name_ = "rise_power";
    tbl.dim_ = 2;
    tbl.values_ = {0, 1, 10, 11, 20, 21};
    tbl.resolve_template(make_2d_template());
    EXPECT_THROW(tbl.interpolate({1.0}), std::invalid_argument);
    EXPECT_THROW(tbl.interpolate({1.0, 2.0, 3.0}), std::invalid_argument);
}

TEST(LookupTableTest, InterpolateRejectsUnresolvedTemplateRef) {
    CMLookupTable tbl;
    tbl.name_ = "rise_power";
    tbl.dim_ = 2;
    tbl.template_name_ = "pw_2d";
    tbl.values_ = {0, 1, 10, 11, 20, 21};
    // 未 resolve：无轴定义 → not ready
    EXPECT_THROW(tbl.interpolate({1.0, 5.0}), std::invalid_argument);
}

}  // namespace
