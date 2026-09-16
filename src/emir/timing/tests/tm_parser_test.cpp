// TWF 解析器单测：字段映射/单位换算/分组合并/兜底计数/序列化往返。
// 样例 data/sample.twf 按命令手册 25.10 版示例构造（TIME_SCALE 1e-12，
// 全部期望值按文件值 × 1e-3 = ns 换算）。
#include <emir/timing/cpp/tm_parser.h>
#include <emir/timing/cpp/tm_types.h>

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

using namespace fly;

namespace fs = std::filesystem;

fs::path data_path(const char* name) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace /
               "src/emir/timing/tests/data" / name;
    }
    return fs::path("data") / name;
}

CMString read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class TmParserTest : public ::testing::Test {
protected:
    TMTimingFile out_;

    void SetUp() override {
        out_ = tm_parse_twf_file(data_path("sample.twf").string());
    }

    static void expect_range(const TMRange& r, double lo, double hi) {
        EXPECT_NEAR(r.min_, lo, 1e-9);
        EXPECT_NEAR(r.max_, hi, 1e-9);
    }
};

TEST_F(TmParserTest, HeaderAndClocks) {
    EXPECT_EQ(out_.design_, "top");
    EXPECT_EQ(out_.version_, "0.1");
    EXPECT_DOUBLE_EQ(out_.time_scale_sec_, 1e-12);
    EXPECT_DOUBLE_EQ(out_.vth_low_, 20.0);
    EXPECT_DOUBLE_EQ(out_.vth_high_, 80.0);
    ASSERT_EQ(out_.default_input_slew_.size(), 4u);
    ASSERT_EQ(out_.clocks_.size(), 3u);
    EXPECT_EQ(out_.clocks_[0].name_, "Iclk1");
    EXPECT_NEAR(out_.clocks_[0].period_, 10.0, 1e-9);
    EXPECT_NEAR(out_.clocks_[0].posedge_, 0.0, 1e-12);
    EXPECT_NEAR(out_.clocks_[0].negedge_, 5.0, 1e-9);
    EXPECT_EQ(out_.clocks_[1].name_, "Iclk2");
    EXPECT_NEAR(out_.clocks_[1].period_, 15.0, 1e-9);
    // 小数周期（CircuitNet 实证形态）
    EXPECT_EQ(out_.clocks_[2].name_, "frac_clk");
    EXPECT_NEAR(out_.clocks_[2].period_, 0.0005, 1e-12);
    EXPECT_NEAR(out_.clocks_[2].negedge_, 0.00025, 1e-12);
}

TEST_F(TmParserTest, NetRecordFields) {
    const TMNameTiming* in1 = out_.find_entry("in1");
    ASSERT_NE(in1, nullptr);
    EXPECT_EQ(in1->clock_id_, 0u);   // Iclk1
    EXPECT_TRUE(in1->is_rise_arrival());
    EXPECT_TRUE(in1->is_fall_arrival());
    EXPECT_TRUE(in1->is_rise_slew());
    EXPECT_TRUE(in1->is_fall_slew());
    EXPECT_FALSE(in1->is_pin_kind());
    EXPECT_FALSE(in1->is_constant());
    EXPECT_FALSE(in1->is_multi_source());
    expect_range(in1->rise_arrival_, 0.0, 0.2);
    expect_range(in1->fall_arrival_, 0.0, 0.2);
    expect_range(in1->rise_slew_, 0.12, 0.12);
    expect_range(in1->fall_slew_, 0.12, 0.12);
}

TEST_F(TmParserTest, NullClockSource) {
    const TMNameTiming* sel = out_.find_entry("sel");
    ASSERT_NE(sel, nullptr);
    EXPECT_FALSE(sel->clock_id_.is_valid());
    EXPECT_TRUE(sel->is_rise_arrival());
}

TEST_F(TmParserTest, MergeAcrossGroups) {
    // clk1 两现：Iclk2（首现 = 主时钟）+ Iclk1（差异 → multi_source）；
    // 下降窗口取并集（第二分组的 5.0:5.2）
    const TMNameTiming* clk1 = out_.find_entry("clk1");
    ASSERT_NE(clk1, nullptr);
    EXPECT_EQ(clk1->clock_id_, 1u);
    EXPECT_TRUE(clk1->is_multi_source());
    expect_range(clk1->rise_arrival_, 0.0, 0.2);
    expect_range(clk1->fall_arrival_, 5.0, 5.2);
    EXPECT_TRUE(clk1->is_fall_slew());
}

TEST_F(TmParserTest, MissingClockAndCounters) {
    const TMNameTiming* ghost = out_.find_entry("ghost_clk_net");
    ASSERT_NE(ghost, nullptr);
    EXPECT_FALSE(ghost->clock_id_.is_valid());
    EXPECT_EQ(out_.missing_clock_count_, 1u);
    // esc/net：源电阻/富余量有值弃收各 2；in1 富余量 2；out1 富余量 2
    EXPECT_EQ(out_.dropped_source_res_count_, 2u);
    EXPECT_EQ(out_.dropped_slack_count_, 6u);
    // bad_rec 字段数不足 8 → 条目级跳过；UNKNOWN_KIND → 未知构造
    EXPECT_EQ(out_.bad_record_count_, 1u);
    EXPECT_EQ(out_.unknown_construct_count_, 1u);
    // C/D 结尾标记：C = esc/net + u1/a；D = in1/sel/clk1×2/ghost/out1
    EXPECT_EQ(out_.cd_flag_c_count_, 2u);
    EXPECT_EQ(out_.cd_flag_d_count_, 6u);
}

TEST_F(TmParserTest, EscapedAndSpecialEntries) {
    // 引号名反斜杠转义清理
    const TMNameTiming* esc = out_.find_entry("esc/net");
    ASSERT_NE(esc, nullptr);
    expect_range(esc->rise_arrival_, 0.01, 0.02);
    expect_range(esc->rise_slew_, 0.005, 0.005);
    // -pin 风味条目
    const TMNameTiming* pin = out_.find_entry("u1/a");
    ASSERT_NE(pin, nullptr);
    EXPECT_TRUE(pin->is_pin_kind());
    // CONSTANT 条目（(NET CONSTANT "name") 形态）
    const TMNameTiming* konst = out_.find_entry("const_net");
    ASSERT_NE(konst, nullptr);
    EXPECT_TRUE(konst->is_constant());
    EXPECT_FALSE(konst->is_rise_arrival());
    EXPECT_FALSE(konst->is_rise_slew());
    ASSERT_EQ(out_.entries_.size(), 8u);   // bad_rec 不入库
}

TEST_F(TmParserTest, WrappedRecordLine) {
    const TMNameTiming* out1 = out_.find_entry("out1");
    ASSERT_NE(out1, nullptr);
    expect_range(out1->rise_arrival_, 1.2, 2.6);
    expect_range(out1->rise_slew_, 0.0127, 0.0127);
    expect_range(out1->fall_arrival_, 1.2, 2.6);
    expect_range(out1->fall_slew_, 0.0108, 0.0108);
}

TEST_F(TmParserTest, SerializationRoundTrip) {
    CMString blob;
    FLY_ENCODE(out_, blob);
    TMTimingFile back;
    FLY_DECODE(blob, TMTimingFile, back);
    back.rebuild_indexes();
    EXPECT_EQ(back.design_, "top");
    // DELIMITERS 收录随序列化往返保持（strip_prefix 段匹配消费口）
    EXPECT_EQ(back.delimiters_, out_.delimiters_);
    ASSERT_EQ(back.clocks_.size(), out_.clocks_.size());
    ASSERT_EQ(back.entries_.size(), out_.entries_.size());
    EXPECT_EQ(back.dropped_slack_count_, out_.dropped_slack_count_);
    EXPECT_EQ(back.cd_flag_c_count_, out_.cd_flag_c_count_);
    const TMNameTiming* merged = back.find_entry("clk1");
    ASSERT_NE(merged, nullptr);
    EXPECT_TRUE(merged->is_multi_source());
    EXPECT_EQ(merged->clock_id_, 1u);
    expect_range(merged->fall_arrival_, 5.0, 5.2);
}

TEST_F(TmParserTest, DelimitersHeader) {
    // DELIMITERS 头声明收录（plan §7.4 实施注记）：原文存储 + 层级分隔符
    // = 首字符；未声明时空串、hier_delim() 缺省 '/' 语义（手册缺省）
    EXPECT_EQ(out_.delimiters_, "/[]");
    EXPECT_EQ(out_.hier_delim(), '/');
    const TMTimingFile bare = tm_parse_twf_text(
        "(TIMING_WINDOWS (CAUSED_BY NULL (NET \"n\" 1:1 1:1 * * 1:1 1:1 * *)))",
        "t");
    EXPECT_EQ(bare.delimiters_, "");
    EXPECT_EQ(bare.hier_delim(), '/');
    // 非缺省分隔符文件：首字符生效（strip_prefix 段级拆分依据）
    const TMTimingFile dot = tm_parse_twf_text(
        "(TIMING_WINDOWS (HEADER (DELIMITERS \".[]\")) (CAUSED_BY NULL "
        "(NET \"n\" 1:1 1:1 * * 1:1 1:1 * *)))", "t");
    EXPECT_EQ(dot.delimiters_, ".[]");
    EXPECT_EQ(dot.hier_delim(), '.');
}

TEST(TmParserErrorTest, StreamLevelErrors) {
    // 顶层结构破坏
    EXPECT_THROW(tm_parse_twf_text("(NET \"x\" 1:1 1:1 * * 1:1 1:1 * *)", "t"),
                 std::runtime_error);
    EXPECT_THROW(tm_parse_twf_text("garbage", "t"), std::runtime_error);
    // 顶层闭合后杂散内容
    EXPECT_THROW(
        tm_parse_twf_text("(TIMING_WINDOWS) junk", "t"), std::runtime_error);
    // 未闭合引号（词法层抛）
    EXPECT_THROW(
        tm_parse_twf_text("(TIMING_WINDOWS (HEADER (VERSION \"abc", "t"),
        std::runtime_error);
    // 顶层未闭合（EOF）
    EXPECT_THROW(tm_parse_twf_file(data_path("bad_syntax.twf").string()),
                 std::runtime_error);
    // 文件不可读
    EXPECT_THROW(tm_parse_twf_file("/nonexistent/sample.twf"),
                 std::runtime_error);
}

// 途径二真实生成文件（Nangate45 + OpenSTA，见 qa/emir/data/timing/README.md）：
// 真实静态时序引擎数值 + 名字与 tm_design.def 对齐；文件已提交为确定性产物
TEST_F(TmParserTest, GeneratedRealTimingFile) {
    const TMTimingFile r =
        tm_parse_twf_file(data_path("tm_design.twf").string());
    EXPECT_EQ(r.design_, "tm_design");
    EXPECT_DOUBLE_EQ(r.time_scale_sec_, 1e-9);
    ASSERT_EQ(r.clocks_.size(), 1u);
    EXPECT_EQ(r.clocks_[0].name_, "clk");
    EXPECT_NEAR(r.clocks_[0].period_, 1.0, 1e-9);
    EXPECT_NEAR(r.clocks_[0].negedge_, 0.5, 1e-9);
    ASSERT_EQ(r.entries_.size(), 7u);

    // 时钟网（时钟源引脚驱动，clk 组）：上升沿 0、下降沿半周期
    const TMNameTiming* clk = r.find_entry("clk");
    ASSERT_NE(clk, nullptr);
    EXPECT_EQ(clk->clock_id_, 0u);
    expect_range(clk->rise_arrival_, 0.0, 0.0);
    expect_range(clk->fall_arrival_, 0.5, 0.5);

    // 时钟缓冲输出（clk 组）：缓冲延迟 0.026 / 半周期 + 延迟 0.525
    const TMNameTiming* nclk = r.find_entry("nclk");
    ASSERT_NE(nclk, nullptr);
    EXPECT_EQ(nclk->clock_id_, 0u);
    EXPECT_NEAR(nclk->rise_arrival_.max_, 0.026037, 1e-6);
    EXPECT_NEAR(nclk->fall_arrival_.max_, 0.525140, 1e-6);

    // 输入端口网：窗口缺省（*）、翻转时间存在（生成器已知局限形态）
    const TMNameTiming* d = r.find_entry("d");
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(d->is_rise_arrival());
    EXPECT_FALSE(d->is_fall_arrival());
    EXPECT_TRUE(d->is_rise_slew());
    EXPECT_FALSE(d->clock_id_.is_valid());

    // 组合网（NULL 组）：双路径真实 min:max 窗口
    const TMNameTiming* n2 = r.find_entry("n2");
    ASSERT_NE(n2, nullptr);
    EXPECT_FALSE(n2->clock_id_.is_valid());
    EXPECT_LT(n2->rise_arrival_.min_, n2->rise_arrival_.max_);
    EXPECT_NEAR(n2->rise_arrival_.min_, 0.064366, 1e-6);
    EXPECT_NEAR(n2->rise_arrival_.max_, 0.092367, 1e-6);

    // 寄存器输出网：CLK→Q 真实时序
    const TMNameTiming* q1 = r.find_entry("q1");
    ASSERT_NE(q1, nullptr);
    EXPECT_NEAR(q1->rise_arrival_.max_, 0.085276, 1e-6);

    // 计数：富余量逐条有值弃收（clk 1 + nclk 1 + d 2 + n1/n2/q/q1 各 2 = 12）
    EXPECT_EQ(r.dropped_slack_count_, 12u);
    EXPECT_EQ(r.dropped_source_res_count_, 0u);
    EXPECT_EQ(r.bad_record_count_, 0u);
    EXPECT_EQ(r.unknown_construct_count_, 0u);
    EXPECT_EQ(r.missing_clock_count_, 0u);
    EXPECT_EQ(r.cd_flag_c_count_, 1u);
    EXPECT_EQ(r.cd_flag_d_count_, 6u);
}

// 途径二引脚维度版本（-pin 风味，键 = 实例/引脚名）：19 条实例引脚条目；
// 未连接引脚（IQ/IQN）窗口 * 缺省形态；输入延迟在数据端点可见
TEST_F(TmParserTest, GeneratedPinDimensionFile) {
    const TMTimingFile r =
        tm_parse_twf_file(data_path("tm_design_pins.twf").string());
    ASSERT_EQ(r.entries_.size(), 19u);
    size_t pin_kind_count = 0;
    for (const TMNameTiming& e : r.entries_) {
        if (e.is_pin_kind()) ++pin_kind_count;
    }
    EXPECT_EQ(pin_kind_count, 19u);

    // 时钟网输入引脚（C 类，clk 组）：时钟两沿
    const TMNameTiming* cba = r.find_entry("u_cb/A");
    ASSERT_NE(cba, nullptr);
    EXPECT_EQ(cba->clock_id_, 0u);
    expect_range(cba->rise_arrival_, 0.0, 0.0);
    expect_range(cba->fall_arrival_, 0.5, 0.5);

    // 时钟缓冲输出：与网络维度 nclk 同值（值取自同一驱动引脚）
    const TMNameTiming* cbz = r.find_entry("u_cb/Z");
    ASSERT_NE(cbz, nullptr);
    EXPECT_EQ(cbz->clock_id_, 0u);
    EXPECT_NEAR(cbz->rise_arrival_.max_, 0.026037, 1e-6);
    EXPECT_NEAR(cbz->fall_arrival_.max_, 0.525140, 1e-6);

    // 数据端点：输入延迟 0.05 作为到达（网络维度 d 网发 * 的互补形态）
    const TMNameTiming* d1d = r.find_entry("u_d1/D");
    ASSERT_NE(d1d, nullptr);
    expect_range(d1d->rise_arrival_, 0.05, 0.05);
    EXPECT_FALSE(d1d->clock_id_.is_valid());

    // 寄存器数据端点 = 驱动输出（零线负载）：u_d2/D ≡ 网络维度 n2 窗口
    const TMNameTiming* d2d = r.find_entry("u_d2/D");
    ASSERT_NE(d2d, nullptr);
    EXPECT_NEAR(d2d->rise_arrival_.min_, 0.064366, 1e-6);
    EXPECT_NEAR(d2d->rise_arrival_.max_, 0.092367, 1e-6);

    // 未连接引脚：窗口缺省、翻转时间零值对
    const TMNameTiming* iq = r.find_entry("u_d1/IQ");
    ASSERT_NE(iq, nullptr);
    EXPECT_FALSE(iq->is_rise_arrival());
    EXPECT_FALSE(iq->is_fall_arrival());
    EXPECT_TRUE(iq->is_rise_slew());

    EXPECT_EQ(r.dropped_slack_count_, 22u);
    EXPECT_EQ(r.dropped_source_res_count_, 0u);
    EXPECT_EQ(r.bad_record_count_, 0u);
    EXPECT_EQ(r.cd_flag_c_count_, 1u);
    EXPECT_EQ(r.cd_flag_d_count_, 18u);
}

// 途径二混合维度版本（同一 CAUSED_BY 分组内 NET 与 PIN 条目并存——解析
// 必须支持的形态，2026-09-15 裁定）：26 条 = 7 网络 + 19 引脚；跨维度条目
// 名互不冲突、数值一致
TEST_F(TmParserTest, GeneratedMixedDimensionFile) {
    const TMTimingFile r =
        tm_parse_twf_file(data_path("tm_design_mixed.twf").string());
    ASSERT_EQ(r.entries_.size(), 26u);
    size_t pin_kind_count = 0;
    for (const TMNameTiming& e : r.entries_) {
        if (e.is_pin_kind()) ++pin_kind_count;
    }
    EXPECT_EQ(pin_kind_count, 19u);

    // 同一时钟分组含两种维度条目
    const TMNameTiming* net_clk = r.find_entry("clk");
    const TMNameTiming* pin_cba = r.find_entry("u_cb/A");
    ASSERT_NE(net_clk, nullptr);
    ASSERT_NE(pin_cba, nullptr);
    EXPECT_EQ(net_clk->clock_id_, 0u);
    EXPECT_EQ(pin_cba->clock_id_, 0u);
    EXPECT_FALSE(net_clk->is_pin_kind());
    EXPECT_TRUE(pin_cba->is_pin_kind());

    // 跨维度数值一致：网条目值 = 其驱动引脚条目值（零线负载）
    const TMNameTiming* n2 = r.find_entry("n2");
    const TMNameTiming* nand_zn = r.find_entry("u_nand/ZN");
    ASSERT_NE(n2, nullptr);
    ASSERT_NE(nand_zn, nullptr);
    EXPECT_EQ(n2->rise_arrival_.min_, nand_zn->rise_arrival_.min_);
    EXPECT_EQ(n2->rise_arrival_.max_, nand_zn->rise_arrival_.max_);
    EXPECT_EQ(n2->rise_slew_.max_, nand_zn->rise_slew_.max_);
    const TMNameTiming* q1 = r.find_entry("q1");
    const TMNameTiming* d1q = r.find_entry("u_d1/Q");
    ASSERT_NE(q1, nullptr);
    ASSERT_NE(d1q, nullptr);
    EXPECT_EQ(q1->rise_arrival_.max_, d1q->rise_arrival_.max_);

    // 维度互补：网络维度 d 窗口 *、引脚维度端点 u_inv/A 到达 = 输入延迟
    const TMNameTiming* d = r.find_entry("d");
    const TMNameTiming* inv_a = r.find_entry("u_inv/A");
    ASSERT_NE(d, nullptr);
    ASSERT_NE(inv_a, nullptr);
    EXPECT_FALSE(d->is_rise_arrival());
    expect_range(inv_a->rise_arrival_, 0.05, 0.05);

    EXPECT_EQ(r.dropped_slack_count_, 34u);
    EXPECT_EQ(r.bad_record_count_, 0u);
    EXPECT_EQ(r.cd_flag_c_count_, 2u);
    EXPECT_EQ(r.cd_flag_d_count_, 24u);
}

// 途径二放大设计（pg_grid：64×64 阵列 + 时钟缓冲树，12943 条网络维度条目；
// 生成链 gen_pg_grid.py → OpenROAD 流程（布图/PDN/布局/CTS/布线，DEF 双跑
// 逐字节一致）→ 独立 OpenSTA 3.1.0 读布线后网表产出，双跑逐字节一致——
// 见 qa/emir/data/timing/pg_grid_twf.tcl 文件头）：真实静态时序数值在
// 大规模设计上的覆盖——时钟树传播、双路径窗口、缺省形态、弃收计数
TEST_F(TmParserTest, GeneratedPgGridFile) {
    const TMTimingFile r =
        tm_parse_twf_file(data_path("pg_grid.twf").string());
    EXPECT_EQ(r.design_, "pg_grid");
    EXPECT_DOUBLE_EQ(r.time_scale_sec_, 1e-9);
    ASSERT_EQ(r.clocks_.size(), 1u);
    EXPECT_EQ(r.clocks_[0].name_, "clk");
    EXPECT_NEAR(r.clocks_[0].period_, 1.0, 1e-9);
    EXPECT_NEAR(r.clocks_[0].posedge_, 0.0, 1e-9);
    EXPECT_NEAR(r.clocks_[0].negedge_, 0.5, 1e-9);
    ASSERT_EQ(r.entries_.size(), 12943u);
    size_t pin_kind_count = 0;
    for (const TMNameTiming& e : r.entries_) {
        if (e.is_pin_kind()) ++pin_kind_count;
    }
    EXPECT_EQ(pin_kind_count, 0u);   // 网络维度单产出

    // 有值窗口条目数（任务验收硬标准下限 >10000）+ 精确回归锚：全文件仅
    // 顶层输入端口网 d 窗口缺省（生成器已知局限形态），其余 12942 条
    // RTW/FTW 至少一侧有值
    size_t winval_count = 0;
    for (const TMNameTiming& e : r.entries_) {
        if (e.is_rise_arrival() || e.is_fall_arrival()) ++winval_count;
    }
    EXPECT_GT(winval_count, 10000u);
    EXPECT_EQ(winval_count, 12942u);

    // 时钟源网（C 类，clk 组）：源端口到达 = 沿时刻（上升 0 / 下降半周期）
    const TMNameTiming* clk = r.find_entry("clk");
    ASSERT_NE(clk, nullptr);
    EXPECT_EQ(clk->clock_id_, 0u);
    EXPECT_FALSE(clk->is_pin_kind());
    expect_range(clk->rise_arrival_, 0.0, 0.0);
    expect_range(clk->fall_arrival_, 0.5, 0.5);

    // 时钟树网（clk 组）：缓冲级联传播延迟单调递增（根 0.033 → 行网 0.097）
    const TMNameTiming* root = r.find_entry("nclk_root");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->clock_id_, 0u);
    EXPECT_NEAR(root->rise_arrival_.max_, 0.033001, 1e-6);
    const TMNameTiming* rowclk = r.find_entry("nclk_r_0");
    ASSERT_NE(rowclk, nullptr);
    EXPECT_EQ(rowclk->clock_id_, 0u);
    EXPECT_NEAR(rowclk->rise_arrival_.max_, 0.096505, 1e-6);
    EXPECT_LT(root->rise_arrival_.max_, rowclk->rise_arrival_.max_);

    // 顶层输入端口网：窗口缺省（*）、翻转时间存在——生成器已知局限形态
    const TMNameTiming* d = r.find_entry("d");
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(d->is_rise_arrival());
    EXPECT_FALSE(d->is_fall_arrival());
    EXPECT_TRUE(d->is_rise_slew());
    EXPECT_FALSE(d->clock_id_.is_valid());

    // 组合网：双路径真实 min:max 窗口（DFF→INV→NAND 行内链）
    const TMNameTiming* in1 = r.find_entry("in_0_1");
    ASSERT_NE(in1, nullptr);
    EXPECT_FALSE(in1->clock_id_.is_valid());
    EXPECT_LT(in1->rise_arrival_.min_, in1->rise_arrival_.max_);
    EXPECT_NEAR(in1->rise_arrival_.min_, 0.126102, 1e-6);
    EXPECT_NEAR(in1->rise_arrival_.max_, 0.279098, 1e-6);

    // 计数：富余量逐条逐沿有值弃收（RSlk 12754 + FSlk 12104）；
    // 源电阻列恒 *；破损/未知构造/缺时钟零
    EXPECT_EQ(r.dropped_slack_count_, 24858u);
    EXPECT_EQ(r.dropped_source_res_count_, 0u);
    EXPECT_EQ(r.bad_record_count_, 0u);
    EXPECT_EQ(r.unknown_construct_count_, 0u);
    EXPECT_EQ(r.missing_clock_count_, 0u);
    // C/D 结尾标记：C = 时钟源网仅 clk 一条；D = 其余全部
    EXPECT_EQ(r.cd_flag_c_count_, 1u);
    EXPECT_EQ(r.cd_flag_d_count_, 12942u);
}

TEST(TmParserErrorTest, EntryLevelRecoveryKeepsStream) {
    // 单条破损记录跳过后，后续记录照常入库
    const TMTimingFile r = tm_parse_twf_text(
        "(TIMING_WINDOWS\n"
        "(CAUSED_BY NULL\n"
        "(NET \"bad\" 1:2 x:y * * 1:2 1:1 * *)\n"
        "(NET \"good\" 1:2 3:3 * * 4:5 6:6 * *)\n"
        ")\n"
        ")\n",
        "t");
    EXPECT_EQ(r.bad_record_count_, 1u);
    ASSERT_EQ(r.entries_.size(), 1u);
    const TMNameTiming* good = r.find_entry("good");
    ASSERT_NE(good, nullptr);
    EXPECT_NEAR(good->fall_arrival_.max_, 5.0, 1e-9);
}

}  // namespace
