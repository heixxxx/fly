// VCD 解析器单测（plan §11.2 wf_parser_test 矩阵，裁定 V 重构后口径）：
// 头部全构造（timescale 各档位/scope 嵌套/var 类型/位区间/id 码复用）、
// 事件段全形态（标量/向量最高位在前/real 跳过计数/x/z/$dumpoff 全 x/
// $dumpon 恢复/$dumpall）、时间戳单调/重复、流级破损 raise、标量速记全
// 位判定（裁定 IV-7）、切块正确性（纯字节切块区间 + 首块起点含
// $dumpvars 段——裁定 IV-6，快照相关断言随快照删除移除）、真实 VCD 样
// 张回归（small.vcd 头部与统计窗口 + xz.vcd 的 x/z 事件）。
//
// 手写样张 data/basic.vcd 的期望值全部按文件逐行手工推导（100ps 档，
// 时间 × 1e5 = fs）；真实样张 small.vcd/xz.vcd 断言依据
// qa/emir/data/waveform/README.md 推导表。
#include <emir/waveform/cpp/wf_parser.h>
#include <emir/waveform/cpp/wf_types.h>

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

using namespace fly;

namespace fs = std::filesystem;

fs::path data_path(const char* name) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace /
               "src/emir/waveform/tests/data" / name;
    }
    return fs::path("data") / name;
}

// 测试内合成文件（切块布局 / gzip 魔数）——bazel 提供的 TEST_TMPDIR
fs::path write_temp(const char* name, const std::string& content) {
    const char* tmp = std::getenv("TEST_TMPDIR");
    fs::path dir = tmp != nullptr ? fs::path(tmp) : fs::path(".work");
    fs::create_directories(dir);
    const fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out << content;
    return p;
}

// 最小合法 VCD 骨架（timescale 档位扫描用）
CMString wrap_vcd(const CMString& timescale_body) {
    return "$timescale " + timescale_body +
           " $end\n"
           "$scope module t $end\n"
           "$var wire 1 ! a $end\n"
           "$upscope $end\n"
           "$enddefinitions $end\n"
           "#0\n"
           "0!\n";
}

void expect_track_bits(const WFTrackEvents& track, uint32_t bit,
                       const CMVector<uint64_t>& times,
                       const CMVector<WFValue>& values) {
    ASSERT_LT(bit, track.bits_.size());
    ASSERT_EQ(track.bits_[bit].size(), times.size());
    ASSERT_EQ(times.size(), values.size());
    for (size_t i = 0; i < times.size(); ++i) {
        EXPECT_EQ(track.bits_[bit][i].time_fs_, times[i]);
        EXPECT_EQ(track.bits_[bit][i].value_, values[i]);
    }
}

// ── 头部全构造（plan §11.2：timescale 各档位/scope 嵌套/var 类型/
// 位区间/id 码复用）─────────────────────────────────────────────────

TEST(WfTimescaleTest, AllGradesToFs) {
    // 档位 = 1|10|100 × s|ms|us|ns|ps|fs 全表（裁定 9：int64 无损承载）
    const struct {
        const char* body;
        uint64_t fs;
    } grades[] = {
        {"1s", 1000000000000000ULL},  {"10ms", 10000000000000ULL},
        {"100us", 100000000000ULL},   {"1us", 1000000000ULL},
        {"100ns", 100000000ULL},      {"10ns", 10000000ULL},
        {"1ns", 1000000ULL},          {"100ps", 100000ULL},
        {"10ps", 10000ULL},           {"1ps", 1000ULL},
        {"100fs", 100ULL},            {"10fs", 10ULL},
        {"1fs", 1ULL},
    };
    for (const auto& g : grades) {
        const WFVcdFile r = wf_parse_vcd_text(wrap_vcd(g.body), "t");
        EXPECT_EQ(r.timescale_fs_, g.fs) << g.body;
    }
    // 数字与单位分属两 token 形态（拼接解析）
    const WFVcdFile split = wf_parse_vcd_text(wrap_vcd("1 ns"), "t");
    EXPECT_EQ(split.timescale_fs_, 1000000ULL);
    // 原值换算秒口径（WFFileStats.timescale_sec_ 同源）
    const WFVcdFile ns = wf_parse_vcd_text(wrap_vcd("1ns"), "t");
    EXPECT_DOUBLE_EQ(ns.timescale_sec_, 1e-9);
}

TEST(WfTimescaleTest, InvalidGradesRaise) {
    // 档位非法 = 头部形态错（流级 raise）
    EXPECT_THROW(wf_parse_vcd_text(wrap_vcd("3ns"), "t"),
                 std::runtime_error);
    EXPECT_THROW(wf_parse_vcd_text(wrap_vcd("1zs"), "t"),
                 std::runtime_error);
    EXPECT_THROW(wf_parse_vcd_text(wrap_vcd("ns"), "t"), std::runtime_error);
    // $timescale 缺失 = 头部缺失（流级 raise）
    const CMString no_ts =
        "$date\nx\n$end\n$scope module t $end\n$var wire 1 ! a $end\n"
        "$upscope $end\n$enddefinitions $end\n#0\n0!\n";
    EXPECT_THROW(wf_parse_vcd_text(no_ts, "t"), std::runtime_error);
}

class WfBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        out_ = wf_parse_vcd_file(data_path("basic.vcd").string());
    }

    WFVcdFile out_;
};

// 头部：date/version/scope 嵌套/var 类型/位区间
TEST_F(WfBasicTest, HeaderConstructs) {
    EXPECT_EQ(out_.date_, "handwritten fixture");
    EXPECT_EQ(out_.version_, "wf basic fixture 1.0");
    EXPECT_EQ(out_.timescale_fs_, 100000ULL);   // 100ps 档
    EXPECT_DOUBLE_EQ(out_.timescale_sec_, 1e-10);
    EXPECT_EQ(out_.t_start_fs_, 0u);
    EXPECT_EQ(out_.t_end_fs_, 6000000u);        // #60 × 1e5
    EXPECT_EQ(out_.duration_fs_, 6000000u);
    ASSERT_EQ(out_.vars_.vars_.size(), 5u);

    // scope 嵌套两层：全路径名 + 分隔符归一 '/'（根 scope 保留）
    const WFVar* clk = out_.vars_.find_by_name("top/clk");
    ASSERT_NE(clk, nullptr);
    EXPECT_EQ(clk->id_code_, "!");
    EXPECT_EQ(clk->kind_, WFVarKind::WIRE);
    EXPECT_EQ(clk->width_, 1u);
    EXPECT_FALSE(clk->has_range_);

    // 位区间声明（[3:0] 左右值原样保留）
    const WFVar* count = out_.vars_.find_by_name("top/count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->id_code_, "*");
    EXPECT_EQ(count->kind_, WFVarKind::REG);
    EXPECT_EQ(count->width_, 4u);
    EXPECT_TRUE(count->has_range_);
    EXPECT_EQ(count->range_left_, 3);
    EXPECT_EQ(count->range_right_, 0);

    // tri 类型归并 + 子 scope 全路径
    const WFVar* data = out_.vars_.find_by_name("top/data");
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->kind_, WFVarKind::TRI);
    const WFVar* flag = out_.vars_.find_by_name("top/u_sub/flag");
    ASSERT_NE(flag, nullptr);
    EXPECT_EQ(flag->id_code_, "#");
}

// id 码复用（README 特征 3）：clk 与 echo 共享值轨道——同 id 多名同值
// 序列由共享结构恒真保证
TEST_F(WfBasicTest, IdReuseAliases) {
    out_.rebuild_indexes();
    EXPECT_EQ(out_.vars_.track_count(), 4u);   // ! * " #（! 两名共用）
    EXPECT_EQ(out_.vars_.track_ids_,
              (CMVector<CMString>{"!", "*", "\"", "#"}));
    EXPECT_EQ(out_.vars_.track_widths_,
              (CMVector<uint32_t>{1, 4, 1, 1}));
    EXPECT_EQ(out_.vars_.find_track("!"), 0u);
    EXPECT_EQ(out_.vars_.find_track("nope"), WFVarTable::kNoTrack);
    const WFVar* echo = out_.vars_.find_by_name("top/u_sub/echo");
    ASSERT_NE(echo, nullptr);
    EXPECT_EQ(echo->id_code_, "!");
    // 两名指向同一轨道 = 同一事件序列
    const size_t clk_track = out_.vars_.find_track(
        out_.vars_.find_by_name("top/clk")->id_code_);
    const size_t echo_track = out_.vars_.find_track(echo->id_code_);
    EXPECT_EQ(clk_track, echo_track);
    EXPECT_EQ(out_.tracks_[clk_track].bits_[0].size(), 7u);
}

// 事件段全形态：标量 0/1 + x/z 大小写、向量最高位在前/短值补 0/含 x/z
// 位（plan §11.2 事件段矩阵；期望值按 basic.vcd 逐行手工推导）
TEST_F(WfBasicTest, ScalarAndVectorEvents) {
    // clk（轨道 0）：#0 置 1 → 每 10ps 翻转（#10 重复时间戳两段）；
    // $dumpoff 段未列 clk → 合成 x 事件 @3e6（全轨道置 x 语义）
    expect_track_bits(out_.tracks_[0], 0,
                      {0, 1000000, 2000000, 3000000, 4000000, 5000000,
                       6000000},
                      {WFValue::ONE, WFValue::ZERO, WFValue::ONE,
                       WFValue::X, WFValue::ZERO, WFValue::ONE,
                       WFValue::ZERO});
    // count（轨道 1，4 位）：b0101 / b1（短值左侧补 0——LSB 右对齐）/
    // x*（标量速记 = 全位 x）/b00x1（x 位）/b1z0（z 位）/b101
    expect_track_bits(out_.tracks_[1], 0,                       // 位权 1
                      {1000000, 3000000, 4000000, 5000000, 6000000},
                      {WFValue::ONE, WFValue::X, WFValue::ONE,
                       WFValue::ZERO, WFValue::ONE});
    expect_track_bits(out_.tracks_[1], 1,                       // 位权 2
                      {1000000, 3000000, 5000000, 6000000},
                      {WFValue::ZERO, WFValue::X, WFValue::Z,
                       WFValue::ZERO});
    expect_track_bits(out_.tracks_[1], 2,                       // 位权 4
                      {1000000, 2000000, 3000000, 4000000, 5000000},
                      {WFValue::ONE, WFValue::ZERO, WFValue::X,
                       WFValue::ZERO, WFValue::ONE});
    expect_track_bits(out_.tracks_[1], 3,                       // 位权 8
                      {1000000, 3000000, 4000000},
                      {WFValue::ZERO, WFValue::X, WFValue::ZERO});
    // data（轨道 2）：0 → z → x（$dumpoff）→ 0
    expect_track_bits(out_.tracks_[2], 0, {0, 1000000, 3000000, 4000000},
                      {WFValue::ZERO, WFValue::Z, WFValue::X,
                       WFValue::ZERO});
    // flag（轨道 3）：1 → x（$dumpoff）→ 0
    expect_track_bits(out_.tracks_[3], 0, {0, 3000000, 4000000},
                      {WFValue::ONE, WFValue::X, WFValue::ZERO});
}

// $dumpoff 全 x（显式行 + 段内未现轨道合成）/ $dumpall / $dumpon 恢复
TEST_F(WfBasicTest, DumpControls) {
    // basic.vcd 的 $dumpoff 段显式列出全部轨道——此处验证合成路径：空段
    // $dumpoff 令全部轨道（含未列出的）产生 x 事件
    const CMString text =
        "$timescale 1ns $end\n"
        "$scope module t $end\n"
        "$var wire 1 ! a $end\n"
        "$var wire 4 * bus [3:0] $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "1!\n"
        "b0011 *\n"
        "$end\n"
        "#10\n"
        "$dumpoff\n"          // 空段：全轨道合成 x
        "$end\n"
        "#20\n"
        "$dumpon\n"           // 恢复 = 全量重 dump（段内行照常解析）
        "b0101 *\n"
        "$end\n";
    const WFVcdFile r = wf_parse_vcd_text(text, "t");
    ASSERT_EQ(r.tracks_.size(), 2u);
    expect_track_bits(r.tracks_[0], 0, {0, 10000000},
                      {WFValue::ONE, WFValue::X});
    // 总线 4 位全部合成 x（b0011 → 全 x → b0101）
    expect_track_bits(r.tracks_[1], 0, {0, 10000000, 20000000},
                      {WFValue::ONE, WFValue::X, WFValue::ONE});
    expect_track_bits(r.tracks_[1], 1, {0, 10000000, 20000000},
                      {WFValue::ONE, WFValue::X, WFValue::ZERO});
    expect_track_bits(r.tracks_[1], 2, {0, 10000000, 20000000},
                      {WFValue::ZERO, WFValue::X, WFValue::ONE});
    expect_track_bits(r.tracks_[1], 3, {0, 10000000, 20000000},
                      {WFValue::ZERO, WFValue::X, WFValue::ZERO});
    // $dumpall 全量重 dump（basic.vcd #40 段已入 ScalarAndVectorEvents）
    // $dumpon 恢复语义同上（basic.vcd #50 段同覆盖）
}

// 标量速记全位判定（裁定 IV-7）：多位向量「全位同值」简写行的等价判定
// 必须覆盖全位——构造位 0 已等于简写值而高位不同的状态（[x,1,0,0] 施 x
// 简写行），旧口径仅查位 0 会误判等价重复并漏置高位
TEST(WfShorthandTest, AllBitsEquivalence) {
    const CMString text =
        "$timescale 1ns $end\n"
        "$scope module t $end\n"
        "$var wire 4 * b [3:0] $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "b01x *\n"    // 状态 [0,0,1,x]（位权序）：位 0 = x，高位非全 x
        "$end\n"
        "#10\n"
        "x*\n"        // 全位同值速记行：位 0 相同但高位必须施值
        "$end\n";
    const WFVcdFile r = wf_parse_vcd_text(text, "t");
    ASSERT_EQ(r.tracks_.size(), 1u);
    const WFTrackEvents& track = r.tracks_[0];
    // 位 0：0 时刻 x 简写（初值 x）无变化，#10 再施 x = 全位等价过滤无
    // 事件——判定依据是全位而非位 0
    EXPECT_EQ(track.bits_[0].size(), 0u);
    // 位 1/2/3：#10 全部置 x（漏置 = 旧缺陷复现）
    for (uint32_t b = 1; b < 4; ++b) {
        ASSERT_EQ(track.bits_[b].size(), 2u) << "bit " << b;
        EXPECT_EQ(track.bits_[b][0].time_fs_, 0u);
        EXPECT_EQ(track.bits_[b][1].time_fs_, 10000000u);
        EXPECT_EQ(track.bits_[b][1].value_, WFValue::X);
    }
}

// 重复时间戳合并（同刻多行记录全部产出）+ 值等价过滤 + 计数器
TEST_F(WfBasicTest, DuplicateTimestampsAndCounters) {
    // basic.vcd 两个 #10：clk 事件与 data z 事件同刻（重复时间戳合法
    // 合并）；重复值记录行 x*（#0）与 0"（第二个 #10）不产事件
    EXPECT_EQ(out_.duplicate_value_count_, 2u);
    // 未知 id 码值行 1&（#60）= 条目级破损跳过计数
    EXPECT_EQ(out_.bad_record_count_, 1u);
    EXPECT_EQ(out_.unsupported_var_count_, 0u);
    EXPECT_EQ(out_.unsupported_record_count_, 0u);
    EXPECT_EQ(out_.unknown_construct_count_, 0u);
    // 同刻事件合并验证：轨道 0 与轨道 2 在 1e6 各有一条事件
    ASSERT_GE(out_.tracks_[0].bits_[0].size(), 2u);
    EXPECT_EQ(out_.tracks_[0].bits_[0][1].time_fs_, 1000000u);
    ASSERT_GE(out_.tracks_[2].bits_[0].size(), 2u);
    EXPECT_EQ(out_.tracks_[2].bits_[0][1].time_fs_, 1000000u);
}

// 确定性：同输入双跑结果逐项一致（runqa 口径——禁 flaky）
TEST_F(WfBasicTest, DeterministicDoubleRun) {
    const WFVcdFile again = wf_parse_vcd_file(data_path("basic.vcd").string());
    EXPECT_EQ(again.vars_.vars_.size(), out_.vars_.vars_.size());
    ASSERT_EQ(again.tracks_.size(), out_.tracks_.size());
    for (size_t t = 0; t < out_.tracks_.size(); ++t) {
        ASSERT_EQ(again.tracks_[t].bits_.size(), out_.tracks_[t].bits_.size());
        for (size_t b = 0; b < out_.tracks_[t].bits_.size(); ++b) {
            ASSERT_EQ(again.tracks_[t].bits_[b].size(),
                      out_.tracks_[t].bits_[b].size());
            for (size_t e = 0; e < out_.tracks_[t].bits_[b].size(); ++e) {
                EXPECT_EQ(again.tracks_[t].bits_[b][e].time_fs_,
                          out_.tracks_[t].bits_[b][e].time_fs_);
                EXPECT_EQ(again.tracks_[t].bits_[b][e].value_,
                          out_.tracks_[t].bits_[b][e].value_);
            }
        }
    }
}

// 不支持构造：real/event var 声明、real 值行、EVCD 强度（S）/port（p）
// 记录——跳过 + 计数（WFMG::0012 家族；样张 data/unsupported.vcd）
TEST(WfUnsupportedTest, SkipsAndCounts) {
    const WFVcdFile r = wf_parse_vcd_file(data_path("unsupported.vcd").string());
    EXPECT_EQ(r.timescale_fs_, 1ULL);   // 1fs 档（单行 $timescale 形态）
    EXPECT_EQ(r.unsupported_var_count_, 2u);
    EXPECT_EQ(r.unsupported_record_count_, 3u);   // r 行 + S 行 + p 行
    // 坏 $var（宽度非数字）与同 id 宽度冲突声明 = 条目级破损计数，跳过
    // 后续声明照常入库（恢复语义）
    EXPECT_EQ(r.bad_record_count_, 2u);
    ASSERT_EQ(r.vars_.vars_.size(), 1u);
    EXPECT_EQ(r.vars_.vars_[0].name_, "top/sig");
    EXPECT_EQ(r.vars_.track_count(), 1u);
    // sig 值序列正常（0 @0 → 1 @10fs）
    ASSERT_EQ(r.tracks_.size(), 1u);
    ASSERT_EQ(r.tracks_[0].bits_[0].size(), 2u);
    EXPECT_EQ(r.tracks_[0].bits_[0][0].time_fs_, 0u);
    EXPECT_EQ(r.tracks_[0].bits_[0][0].value_, WFValue::ZERO);
    EXPECT_EQ(r.tracks_[0].bits_[0][1].time_fs_, 10u);
    EXPECT_EQ(r.tracks_[0].bits_[0][1].value_, WFValue::ONE);
}

// 时间戳单调 + 回退 = 流级破损 raise；档位 × 时间戳 uint64 溢出 =
// 乘前上界校验流级 raise（防静默回绕）
TEST(WfTimestampTest, RegressRaises) {
    EXPECT_THROW(
        wf_parse_vcd_file(data_path("bad_ts_regress.vcd").string()),
        std::runtime_error);
    const CMString regress =
        "$timescale 1ns $end\n$scope module t $end\n$var wire 1 ! a $end\n"
        "$upscope $end\n$enddefinitions $end\n#10\n1!\n#5\n0!\n";
    EXPECT_THROW(wf_parse_vcd_text(regress, "t"), std::runtime_error);
    // 时间戳 token 非法
    const CMString bad_tok = wrap_vcd("1ns");
    EXPECT_THROW(wf_parse_vcd_text(
                     "$timescale 1ns $end\n$scope module t $end\n"
                     "$var wire 1 ! a $end\n$upscope $end\n"
                     "$enddefinitions $end\n#xx\n",
                     "t"),
                 std::runtime_error);
    // 溢出：uint64 最大时间戳 × 1ns 档（×1e6）必然回绕 → raise；
    // 同时间戳 1fs 档（×1）无损承载 → 不 raise
    const CMString huge_ts =
        "$timescale 1ns $end\n$scope module t $end\n$var wire 1 ! a $end\n"
        "$upscope $end\n$enddefinitions $end\n"
        "#18446744073709551615\n0!\n";
    EXPECT_THROW(wf_parse_vcd_text(huge_ts, "t"), std::runtime_error);
    const CMString huge_ts_fs =
        "$timescale 1fs $end\n$scope module t $end\n$var wire 1 ! a $end\n"
        "$upscope $end\n$enddefinitions $end\n"
        "#18446744073709551615\n0!\n";
    const WFVcdFile max_ok = wf_parse_vcd_text(huge_ts_fs, "t");
    EXPECT_EQ(max_ok.t_end_fs_, UINT64_MAX);
}

// 流级破损 raise（对齐 tm_parser 语义；plan §11.2）
TEST(WfStreamErrorTest, Raises) {
    // $date 段未闭合
    EXPECT_THROW(
        wf_parse_vcd_file(data_path("bad_unclosed_version.vcd").string()),
        std::runtime_error);
    // $enddefinitions 缺失（头部缺失家族）
    EXPECT_THROW(
        wf_parse_vcd_file(data_path("bad_missing_enddefs.vcd").string()),
        std::runtime_error);
    // $scope 未闭合（$enddefinitions 时栈非空）
    EXPECT_THROW(wf_parse_vcd_file(data_path("bad_scope_leak.vcd").string()),
                 std::runtime_error);
    // $dumpvars 段未闭合
    EXPECT_THROW(
        wf_parse_vcd_file(data_path("bad_dumpvars_unclosed.vcd").string()),
        std::runtime_error);
    // $upscope 下溢
    EXPECT_THROW(wf_parse_vcd_text("$upscope $end\n", "t"),
                 std::runtime_error);
    // 无 $var 声明
    EXPECT_THROW(wf_parse_vcd_text(
                     "$timescale 1ns $end\n$enddefinitions $end\n", "t"),
                 std::runtime_error);
    // 文件不可读
    EXPECT_THROW(wf_parse_vcd_file("/nonexistent/x.vcd"),
                 std::runtime_error);
    WFVarTable vars_sink;
    EXPECT_THROW(wf_plan_file_chunks("/nonexistent/x.vcd", 1024, vars_sink),
                 std::runtime_error);
    // $timescale 缺失（头部缺失家族——文件样张形态）
    EXPECT_THROW(wf_parse_vcd_file(data_path("no_timescale.vcd").string()),
                 std::runtime_error);
}

// gzip 魔数（0x1f 0x8b）嗅探：明确报错指引不支持压缩 VCD（plan §2/§12）
TEST(WfGzipTest, MagicRejectWithGuidance) {
    const std::string gz = std::string("\x1f\x8b", 2) + "fake gzip payload";
    const fs::path p = write_temp("gzip_magic.vcd", gz);
    bool caught = false;
    try {
        wf_parse_vcd_file(p.string());
    } catch (const std::runtime_error& e) {
        caught = true;
        EXPECT_NE(std::string(e.what()).find("gzip"), std::string::npos);
    }
    EXPECT_TRUE(caught);
    caught = false;
    WFVarTable vars_sink;
    try {
        wf_plan_file_chunks(p.string(), 1024, vars_sink);
    } catch (const std::runtime_error& e) {
        caught = true;
        EXPECT_NE(std::string(e.what()).find("gzip"), std::string::npos);
    }
    EXPECT_TRUE(caught);
}

// ── 切块正确性（plan §11.2：纯字节切块区间——裁定 V-3 无快照；合成
//    173 字节文件，时间戳行/值行字节布局手工核算）────────────────────

class WfChunkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 事件段布局：#0(3) $dumpvars(10) 0!(3) 1"(3) $end(5) #10(4)
        // 1!(3) #20(4) 0!(3) 1"(3) #30(4) 0"(3) #40(4) 1!(3)
        synth_ =
            "$timescale 1ns $end\n"
            "$scope module t $end\n"
            "$var wire 1 ! a $end\n"
            "$var wire 1 \" b $end\n"
            "$upscope $end\n"
            "$enddefinitions $end\n"
            "#0\n$dumpvars\n0!\n1\"\n$end\n"
            "#10\n1!\n#20\n0!\n1\"\n#30\n0\"\n#40\n1!\n";
        path_ = write_temp("chunk_synth.vcd", synth_);
        full_ = wf_parse_vcd_text(synth_, "synth");
        full_.rebuild_indexes();
    }

    CMString synth_;
    fs::path path_;
    WFVcdFile full_;
};

TEST_F(WfChunkTest, MultiBlockByteSpans) {
    WFVarTable vars;
    const WFFileChunkPlan plan =
        wf_plan_file_chunks(path_.string(), 30, vars);
    EXPECT_EQ(plan.file_name_, path_.string());
    EXPECT_EQ(plan.file_size_, synth_.size());
    EXPECT_EQ(plan.header_start_, 0u);
    EXPECT_EQ(plan.header_end_, synth_.find("$enddefinitions"));
    EXPECT_EQ(plan.timescale_fs_, 1000000u);
    // 30 字节界：#0 开首块（偏移差 24 < 30），#20 处收口开新块（31 ≥ 30）
    const size_t ts0 = synth_.find("#0");
    const size_t ts20 = synth_.find("#20");
    ASSERT_EQ(plan.chunks_.size(), 2u);

    const WFFileChunk& b0 = plan.chunks_[0];
    const WFFileChunk& b1 = plan.chunks_[1];
    EXPECT_EQ(b0.start_, ts0);
    EXPECT_EQ(b0.end_, ts20);
    EXPECT_EQ(b1.start_, ts20);
    EXPECT_EQ(b1.end_, synth_.size());
    // 块间首尾衔接
    EXPECT_EQ(b0.end_, b1.start_);
    // 时间范围（块内首末时间戳口径——B0 含 #0/#10 两时间戳行）
    EXPECT_EQ(b0.t_start_fs_, 0u);
    EXPECT_EQ(b0.t_end_fs_, 10000000u);
    EXPECT_EQ(b1.t_start_fs_, 20000000u);
    EXPECT_EQ(b1.t_end_fs_, 40000000u);
    // var 表出参与整读解析一致（块任务不重解析头部文本的共享件）
    vars.rebuild_indexes();
    ASSERT_EQ(vars.vars_.size(), full_.vars_.vars_.size());
    EXPECT_EQ(vars.track_ids_, full_.vars_.track_ids_);
    EXPECT_EQ(vars.track_widths_, full_.vars_.track_widths_);
}

TEST_F(WfChunkTest, SingleBlockCoversWholeEventSection) {
    WFVarTable vars;
    const WFFileChunkPlan plan =
        wf_plan_file_chunks(path_.string(), 1ULL << 30, vars);
    ASSERT_EQ(plan.chunks_.size(), 1u);
    EXPECT_EQ(plan.chunks_[0].start_, synth_.find("#0"));
    EXPECT_EQ(plan.chunks_[0].end_, synth_.size());
    EXPECT_EQ(plan.chunks_[0].t_start_fs_, 0u);
    EXPECT_EQ(plan.chunks_[0].t_end_fs_, 40000000u);
}

// 首块起点 = min($dumpvars 段起点, 首个 # 行起点)（裁定 IV-6）：$dumpvars
// 先于首时间戳的形态下首块从 $dumpvars 关键字起步——初值事件段不可丢
// 失（事件级断言见 wf_partition_test.DumpvarsFirstChunkInitialEvents）
TEST_F(WfChunkTest, DumpvarsBeforeFirstTimestampStartsFirstChunk) {
    const CMString text =
        "$timescale 1ns $end\n"
        "$scope module t $end\n"
        "$var wire 1 ! a $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "$dumpvars\n1!\n$end\n"
        "#0\n0!\n"
        "#10\n1!\n";
    const fs::path p = write_temp("chunk_dumpvars_first.vcd", text);
    WFVarTable vars;
    const WFFileChunkPlan plan =
        wf_plan_file_chunks(p.string(), 1ULL << 30, vars);
    ASSERT_EQ(plan.chunks_.size(), 1u);
    // 首块起点 = $dumpvars 关键字字节偏移（早于首个 # 行）
    EXPECT_EQ(plan.chunks_[0].start_, text.find("$dumpvars"));
    EXPECT_EQ(plan.chunks_[0].t_start_fs_, 0u);
    EXPECT_EQ(plan.chunks_[0].t_end_fs_, 10000000u);
    // $dumpvars 在首个 # 之后（本文件主体形态）→ 首块起点 = 首个 # 行
    const WFFileChunkPlan plan2 =
        wf_plan_file_chunks(path_.string(), 1ULL << 30, vars);
    ASSERT_EQ(plan2.chunks_.size(), 1u);
    EXPECT_EQ(plan2.chunks_[0].start_, synth_.find("#0"));
}

// 无 # 时间戳行的纯 $dumpvars 形态：单块兜底（块区间 = $dumpvars 段起
// 至文件尾，时间范围 [0,0]）——不退化为 0 块任务（初值事件级断言见
// wf_partition_test.DumpvarsOnlyFileSingleChunkInitialEvents）
TEST_F(WfChunkTest, DumpvarsOnlyNoTimestampSingleChunk) {
    const CMString text =
        "$timescale 1ns $end\n"
        "$scope module t $end\n"
        "$var wire 1 ! a $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "$dumpvars\n1!\n$end\n";
    const fs::path p = write_temp("chunk_dumpvars_only.vcd", text);
    WFVarTable vars;
    const WFFileChunkPlan plan =
        wf_plan_file_chunks(p.string(), 1ULL << 30, vars);
    ASSERT_EQ(plan.chunks_.size(), 1u);
    EXPECT_EQ(plan.chunks_[0].start_, text.find("$dumpvars"));
    EXPECT_EQ(plan.chunks_[0].end_, text.size());
    EXPECT_EQ(plan.chunks_[0].t_start_fs_, 0u);
    EXPECT_EQ(plan.chunks_[0].t_end_fs_, 0u);
}

TEST_F(WfChunkTest, SerializationRoundTrip) {
    WFVarTable vars;
    const WFFileChunkPlan plan =
        wf_plan_file_chunks(path_.string(), 30, vars);
    CMString blob;
    FLY_ENCODE(plan, blob);
    WFFileChunkPlan back;
    FLY_DECODE(blob, WFFileChunkPlan, back);
    EXPECT_EQ(back.file_name_, plan.file_name_);
    EXPECT_EQ(back.file_size_, plan.file_size_);
    EXPECT_EQ(back.header_end_, plan.header_end_);
    ASSERT_EQ(back.chunks_.size(), plan.chunks_.size());
    for (size_t i = 0; i < plan.chunks_.size(); ++i) {
        EXPECT_EQ(back.chunks_[i].start_, plan.chunks_[i].start_);
        EXPECT_EQ(back.chunks_[i].end_, plan.chunks_[i].end_);
        EXPECT_EQ(back.chunks_[i].t_start_fs_, plan.chunks_[i].t_start_fs_);
        EXPECT_EQ(back.chunks_[i].t_end_fs_, plan.chunks_[i].t_end_fs_);
    }
    // var 表序列化往返 + 索引重建
    CMString vars_blob;
    FLY_ENCODE(vars, vars_blob);
    WFVarTable vars_back;
    FLY_DECODE(vars_blob, WFVarTable, vars_back);
    vars_back.rebuild_indexes();
    EXPECT_EQ(vars_back.track_ids_, vars.track_ids_);
    ASSERT_EQ(vars_back.vars_.size(), vars.vars_.size());
    EXPECT_EQ(vars_back.find_by_name("top/clk"), nullptr);   // 合成文件
    ASSERT_NE(vars_back.find_by_name("t/a"), nullptr);
    EXPECT_EQ(vars_back.find_track("!"), 0u);
}

// ── 真实样张回归（Icarus Verilog 确定性产物；断言依据
// qa/emir/data/waveform/README.md 推导表）────────────────────────────

class WfRealSampleTest : public ::testing::Test {
protected:
    void SetUp() override {
        small_ = wf_parse_vcd_file(data_path("small.vcd").string());
        xz_ = wf_parse_vcd_file(data_path("xz.vcd").string());
    }

    WFVcdFile small_;
    WFVcdFile xz_;
};

TEST_F(WfRealSampleTest, SmallVcdHeaderAndWindow) {
    // 头部解析 + 统计窗口（README：$timescale 1ns、时间范围 [0, 201]）
    EXPECT_EQ(small_.timescale_fs_, 1000000ULL);
    EXPECT_DOUBLE_EQ(small_.timescale_sec_, 1e-9);
    EXPECT_EQ(small_.t_start_fs_, 0u);
    EXPECT_EQ(small_.t_end_fs_, 201000000u);
    EXPECT_EQ(small_.duration_fs_, 201000000u);
    EXPECT_EQ(small_.vars_.vars_.size(), 68u);
    // id 码复用后值轨道数（68 声明 − 29 重复 id = 39；README 特征 3）
    EXPECT_EQ(small_.vars_.track_count(), 39u);
    ASSERT_NE(small_.vars_.find_by_name("wf_small/clk"), nullptr);
    // 根 scope = 设计名（README 特征 1——解析层保留原文，剥除属换算层）
    EXPECT_EQ(small_.vars_.vars_[0].name_, "wf_small/n_xor");
    // 向量 count [3:0] 位区间
    const WFVar* count = small_.vars_.find_by_name("wf_small/count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->width_, 4u);
    EXPECT_TRUE(count->has_range_);
    EXPECT_EQ(count->range_left_, 3);
    EXPECT_EQ(count->range_right_, 0);
    EXPECT_EQ(small_.bad_record_count_, 0u);
    EXPECT_EQ(small_.unsupported_var_count_, 0u);
    EXPECT_EQ(small_.unsupported_record_count_, 0u);
}

TEST_F(WfRealSampleTest, SmallVcdStatisticsWindow) {
    small_.rebuild_indexes();
    // clk：dumpvars 初值 0（X→0）+ 20 rise + 20 fall = 41 事件
    const size_t clk_track =
        small_.vars_.find_track(small_.vars_.find_by_name("wf_small/clk")
                                     ->id_code_);
    ASSERT_EQ(small_.tracks_[clk_track].bits_[0].size(), 41u);
    EXPECT_EQ(small_.tracks_[clk_track].bits_[0][0].time_fs_, 0u);
    EXPECT_EQ(small_.tracks_[clk_track].bits_[0][0].value_, WFValue::ZERO);
    EXPECT_EQ(small_.tracks_[clk_track].bits_[0][1].time_fs_, 5000000u);
    EXPECT_EQ(small_.tracks_[clk_track].bits_[0][1].value_, WFValue::ONE);
    // count[0]（位权 2^0）：初值 + 18 次翻转 = 19 事件（README 推导 4）
    const size_t cnt_track =
        small_.vars_.find_track(small_.vars_.find_by_name("wf_small/count")
                                     ->id_code_);
    ASSERT_EQ(small_.tracks_[cnt_track].width_, 4u);
    ASSERT_EQ(small_.tracks_[cnt_track].bits_[0].size(), 19u);
    // count[3]（位权 2^3）：初值 + 2 次翻转（@95/@175）= 3 事件
    ASSERT_EQ(small_.tracks_[cnt_track].bits_[3].size(), 3u);
    EXPECT_EQ(small_.tracks_[cnt_track].bits_[3][1].time_fs_, 95000000u);
    EXPECT_EQ(small_.tracks_[cnt_track].bits_[3][1].value_, WFValue::ONE);
    EXPECT_EQ(small_.tracks_[cnt_track].bits_[3][2].time_fs_, 175000000u);
    // 重复值事件行（README 特征 4）：等价行被过滤并计数（组合毛刺同刻
    // 聚合形态——n_cnt2_d 在 t=55 出现与前值相同的记录）
    EXPECT_GT(small_.duplicate_value_count_, 0u);
    // 别名一致断言：n_and 与 u_and1/ZN 共 id ")"——同轨道同事件序列
    const WFVar* n_and = small_.vars_.find_by_name("wf_small/n_and");
    const WFVar* zn = small_.vars_.find_by_name("wf_small/u_and1/ZN");
    ASSERT_NE(n_and, nullptr);
    ASSERT_NE(zn, nullptr);
    EXPECT_EQ(n_and->id_code_, ")");
    EXPECT_EQ(zn->id_code_, ")");
    EXPECT_EQ(small_.vars_.find_track(n_and->id_code_),
              small_.vars_.find_track(zn->id_code_));
}

TEST_F(WfRealSampleTest, XzVcdFourState) {
    xz_.rebuild_indexes();
    EXPECT_EQ(xz_.vars_.vars_.size(), 19u);
    EXPECT_EQ(xz_.vars_.track_count(), 9u);
    EXPECT_EQ(xz_.duration_fs_, 101000000u);
    // n_tri（TZ=71ns）：z→0@30 → 0→1@45 → 1→z@60；初值 z@0
    const size_t tri_track =
        xz_.vars_.find_track(xz_.vars_.find_by_name("wf_xz/n_tri")->id_code_);
    ASSERT_EQ(xz_.tracks_[tri_track].bits_[0].size(), 4u);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][0].time_fs_, 0u);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][0].value_, WFValue::Z);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][1].time_fs_, 30000000u);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][1].value_, WFValue::ZERO);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][2].time_fs_, 45000000u);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][2].value_, WFValue::ONE);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][3].time_fs_, 60000000u);
    EXPECT_EQ(xz_.tracks_[tri_track].bits_[0][3].value_, WFValue::Z);
    // n_q（TX=15ns）：x 态持续到 15ns 才有首事件（x→0@15，README：
    // x 参与转换不计口径的解析级证据）
    const size_t q_track =
        xz_.vars_.find_track(xz_.vars_.find_by_name("wf_xz/n_q")->id_code_);
    ASSERT_EQ(xz_.tracks_[q_track].bits_[0].size(), 4u);
    EXPECT_EQ(xz_.tracks_[q_track].bits_[0][0].time_fs_, 15000000u);
    EXPECT_EQ(xz_.tracks_[q_track].bits_[0][0].value_, WFValue::ZERO);
    EXPECT_EQ(xz_.tracks_[q_track].bits_[0][1].time_fs_, 25000000u);
    EXPECT_EQ(xz_.tracks_[q_track].bits_[0][1].value_, WFValue::ONE);
    // n_f（force 极端构造）：x→1@38 → 1→z@58 → z→0@78——净 rise/fall
    // 为 0，各态时长非平凡（README 注意事项 6）；15ns 前为 1
    //（u_buf 直通初值段——force x@18 前的值段，README 表 T1=23 含
    // [15,18) 段）
    const size_t f_track =
        xz_.vars_.find_track(xz_.vars_.find_by_name("wf_xz/n_f")->id_code_);
    ASSERT_EQ(xz_.tracks_[f_track].bits_[0].size(), 5u);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][0].time_fs_, 15000000u);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][0].value_, WFValue::ONE);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][1].time_fs_, 18000000u);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][1].value_, WFValue::X);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][2].time_fs_, 38000000u);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][2].value_, WFValue::ONE);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][3].time_fs_, 58000000u);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][3].value_, WFValue::Z);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][4].time_fs_, 78000000u);
    EXPECT_EQ(xz_.tracks_[f_track].bits_[0][4].value_, WFValue::ZERO);
    // $dumpvars 段的 x 初值行（x" 等）为等价记录——不产事件
    EXPECT_GT(xz_.duplicate_value_count_, 0u);
}

// 解析产物容器序列化往返（FLY_SERIALIZE；对齐 TmParserTest 先例）
TEST_F(WfRealSampleTest, ParsedFileSerializationRoundTrip) {
    CMString blob;
    FLY_ENCODE(small_, blob);
    WFVcdFile back;
    FLY_DECODE(blob, WFVcdFile, back);
    back.rebuild_indexes();
    EXPECT_EQ(back.version_, small_.version_);
    EXPECT_EQ(back.timescale_fs_, small_.timescale_fs_);
    EXPECT_EQ(back.duration_fs_, small_.duration_fs_);
    ASSERT_EQ(back.vars_.vars_.size(), small_.vars_.vars_.size());
    EXPECT_EQ(back.vars_.track_ids_, small_.vars_.track_ids_);
    ASSERT_EQ(back.tracks_.size(), small_.tracks_.size());
    EXPECT_EQ(back.tracks_[0].bits_[0].size(),
              small_.tracks_[0].bits_[0].size());
    EXPECT_EQ(back.duplicate_value_count_, small_.duplicate_value_count_);
    EXPECT_EQ(back.bad_record_count_, small_.bad_record_count_);
    const WFVar* clk = back.vars_.find_by_name("wf_small/clk");
    ASSERT_NE(clk, nullptr);
    EXPECT_EQ(clk->id_code_, ",");
    const size_t clk_track = back.vars_.find_track(clk->id_code_);
    ASSERT_EQ(back.tracks_[clk_track].bits_[0].size(), 41u);
}

}  // namespace
