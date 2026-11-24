// SPEF（Standard Parasitic Exchange Format，标准寄生交换格式，
// IEEE 1481）解析器单测（spef-db-plan §9.2 解析矩阵全条目）：
// 基准样张期望值逐项钉死、单位换算、多值取值规则（裁定 6）、名字
// 映射与未登记处置（裁定 11）、节点分类与合计口径、方言容忍（§2.5
// 清单）、跳过项（裁定 5/12）、注释、文件级与段级错误边界（dev-rules
// §7.2 两类）、切块计划（网段边界 + 单网不拆分）、整文件与分段解析
// 等价、真实工具样张回归（GPL 许可样张不入仓库——唯一开关是环境变量
// FLY_SPEF_REAL_SAMPLE，未设置即跳过；设置了但文件不存在是显式失败）。
//
// 手写样张 data/*.spef 的期望值全部按文件逐行手工推导（C_UNIT 1 FF
// 时数值直读；1 PF 时 x1000）；参数化变体（分隔符四种、多值下标组合、
// 文件级负例微形态）用测试内合成文本，与样张行为互为印证。
#include <emir/spef/cpp/sp_parser.h>
#include <emir/spef/cpp/sp_types.h>

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace fly;

namespace fs = std::filesystem;

fs::path data_path(const char* name) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace / "src/emir/spef/tests/data" / name;
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

CMString read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return CMString((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
}

// 相对误差 1e-12 的浮点断言（期望值 0 时要求精确 0）
void expect_near(double actual, double expected) {
    EXPECT_NEAR(actual, expected, std::fabs(expected) * 1e-12) << "expected "
                                                               << expected;
}

// 最小合法文件头（单位 1 FF / 1 OHM——数值直读 fF）
CMString std_header(const CMString& body) {
    return CMString("*SPEF \"IEEE 1481-1998\"\n*DIVIDER /\n*DELIMITER :\n"
                    "*C_UNIT 1 FF\n*R_UNIT 1 OHM\n") + body;
}

// 矩阵条目 13：真实工具样张——默认跳过，环境变量 FLY_SPEF_REAL_SAMPLE
// 是唯一开关（用例本体见文件末尾 RealSampleTest.RealToolSample，注释
// 写明不设默认路径与不入库的理由、以及运行方法）

// SPChunkResult 全字段比较（整文件与分段等价断言用）
void expect_same(const SPChunkResult& a, const SPChunkResult& b) {
    ASSERT_EQ(a.nets_.size(), b.nets_.size());
    for (size_t i = 0; i < a.nets_.size(); ++i) {
        EXPECT_EQ(a.nets_[i].name_, b.nets_[i].name_) << i;
        EXPECT_EQ(a.nets_[i].kind_, b.nets_[i].kind_) << i;
        expect_near(a.nets_[i].total_cap_reported_ff_,
                    b.nets_[i].total_cap_reported_ff_);
        expect_near(a.nets_[i].ground_cap_ff_, b.nets_[i].ground_cap_ff_);
        expect_near(a.nets_[i].coupling_cap_ff_,
                    b.nets_[i].coupling_cap_ff_);
        EXPECT_EQ(a.nets_[i].value_count_, b.nets_[i].value_count_) << i;
        EXPECT_EQ(a.nets_[i].cap_count_, b.nets_[i].cap_count_) << i;
        EXPECT_EQ(a.nets_[i].coupling_cap_count_,
                  b.nets_[i].coupling_cap_count_) << i;
        EXPECT_EQ(a.nets_[i].res_count_, b.nets_[i].res_count_) << i;
    }
    EXPECT_EQ(a.net_count_, b.net_count_);
    EXPECT_EQ(a.dnet_count_, b.dnet_count_);
    EXPECT_EQ(a.rnet_count_, b.rnet_count_);
    EXPECT_EQ(a.pnet_skip_count_, b.pnet_skip_count_);
    EXPECT_EQ(a.broken_net_count_, b.broken_net_count_);
    EXPECT_EQ(a.name_map_miss_count_, b.name_map_miss_count_);
    EXPECT_EQ(a.dropped_record_count_, b.dropped_record_count_);
    EXPECT_EQ(a.multi_value_net_count_, b.multi_value_net_count_);
    EXPECT_EQ(a.value_count_max_, b.value_count_max_);
    EXPECT_EQ(a.value_index_fallback_count_, b.value_index_fallback_count_);
    EXPECT_EQ(a.cap_count_, b.cap_count_);
    EXPECT_EQ(a.coupling_cap_count_, b.coupling_cap_count_);
    EXPECT_EQ(a.res_count_, b.res_count_);
    EXPECT_EQ(a.external_ground_cap_count_, b.external_ground_cap_count_);
    EXPECT_EQ(a.external_coupling_cap_count_,
              b.external_coupling_cap_count_);
    EXPECT_EQ(a.conn_count_, b.conn_count_);
    expect_near(a.conn_pin_cap_obs_sum_ff_, b.conn_pin_cap_obs_sum_ff_);
    EXPECT_EQ(a.unknown_direction_count_, b.unknown_direction_count_);
    EXPECT_EQ(a.induc_count_, b.induc_count_);
    EXPECT_EQ(a.stray_content_count_, b.stray_content_count_);
}

// 逐块解析拼接（计数相加 / value_count_max 取最大 / nets 顺序拼接）
SPChunkResult merge_chunks(const CMVector<SPChunkResult>& parts) {
    SPChunkResult all;
    for (const SPChunkResult& r : parts) {
        for (const SPNet& net : r.nets_) {
            all.nets_.push_back(net);
        }
        all.net_count_ += r.net_count_;
        all.dnet_count_ += r.dnet_count_;
        all.rnet_count_ += r.rnet_count_;
        all.pnet_skip_count_ += r.pnet_skip_count_;
        all.broken_net_count_ += r.broken_net_count_;
        all.name_map_miss_count_ += r.name_map_miss_count_;
        all.dropped_record_count_ += r.dropped_record_count_;
        all.multi_value_net_count_ += r.multi_value_net_count_;
        all.value_count_max_ = std::max(all.value_count_max_,
                                        r.value_count_max_);
        all.value_index_fallback_count_ += r.value_index_fallback_count_;
        all.cap_count_ += r.cap_count_;
        all.coupling_cap_count_ += r.coupling_cap_count_;
        all.res_count_ += r.res_count_;
        all.external_ground_cap_count_ += r.external_ground_cap_count_;
        all.external_coupling_cap_count_ += r.external_coupling_cap_count_;
        all.conn_count_ += r.conn_count_;
        all.conn_pin_cap_obs_sum_ff_ += r.conn_pin_cap_obs_sum_ff_;
        all.unknown_direction_count_ += r.unknown_direction_count_;
        all.induc_count_ += r.induc_count_;
        all.stray_content_count_ += r.stray_content_count_;
    }
    return all;
}

// ── 矩阵条目 1：基准样张（期望值逐项钉死）─────────────────────────

class BasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        result_ = sp_parse_spef_file(data_path("basic.spef").string());
    }

    SPChunkResult result_;
    SPFileHeader header_ = sp_parse_header_file(
        data_path("basic.spef").string());
};

TEST_F(BasicTest, NetAggregates) {
    ASSERT_EQ(result_.nets_.size(), 1u);
    const SPNet& net = result_.nets_[0];
    EXPECT_EQ(net.name_, "n_a");
    EXPECT_EQ(net.kind_, SPNetKind::D_NET);
    expect_near(net.total_cap_reported_ff_, 10.0);   // 0.010 PF x 1000
    expect_near(net.ground_cap_ff_, 7.0);            // 0.004 + 0.003
    expect_near(net.coupling_cap_ff_, 3.0);
    EXPECT_EQ(net.value_count_, 1u);
    EXPECT_EQ(net.cap_count_, 3u);
    EXPECT_EQ(net.coupling_cap_count_, 1u);
    EXPECT_EQ(net.res_count_, 2u);
    EXPECT_EQ(result_.net_count_, 1u);
    EXPECT_EQ(result_.dnet_count_, 1u);
    EXPECT_EQ(result_.rnet_count_, 0u);
    EXPECT_EQ(result_.broken_net_count_, 0u);
    // *CONN 观测：2 条 *I 行；*L 0.001 PF -> 1 fF
    EXPECT_EQ(result_.conn_count_, 2u);
    expect_near(result_.conn_pin_cap_obs_sum_ff_, 1.0);
}

TEST_F(BasicTest, HeaderFields) {
    EXPECT_EQ(header_.source_file_, data_path("basic.spef").string());
    EXPECT_EQ(header_.design_name_, "wf_top");
    EXPECT_EQ(header_.version_, "IEEE 1481-1998");
    EXPECT_EQ(header_.divider_, '/');
    EXPECT_EQ(header_.delimiter_, ':');
    EXPECT_EQ(header_.bus_left_, '[');
    EXPECT_EQ(header_.bus_right_, ']');
    expect_near(header_.cap_scale_ff_, 1000.0);   // 1 PF = 1e3 fF
    expect_near(header_.res_scale_ohm_, 1.0);
    EXPECT_EQ(header_.cap_unit_decl_, "1 PF");
    EXPECT_EQ(header_.res_unit_decl_, "1 OHM");
    EXPECT_EQ(header_.time_unit_decl_, "1 NS");
    EXPECT_EQ(header_.ind_unit_decl_, "1 HENRY");
}

// ── 矩阵条目 2：单位换算 ─────────────────────────────────────────

TEST(UnitsTest, ScaleFactors) {
    const SPFileHeader mix =
        sp_parse_header_file(data_path("units_mix.spef").string());
    expect_near(mix.cap_scale_ff_, 10000.0);    // 10 PF = 1e4 fF
    expect_near(mix.res_scale_ohm_, 1000.0);    // 1 KOHM = 1e3 ohm
    EXPECT_EQ(mix.cap_unit_decl_, "10 PF");
    EXPECT_EQ(mix.res_unit_decl_, "1 KOHM");
    EXPECT_EQ(mix.time_unit_decl_, "10 NS");
    EXPECT_EQ(mix.ind_unit_decl_, "100 UH");

    const SPFileHeader ff =
        sp_parse_header_file(data_path("units_ff.spef").string());
    expect_near(ff.cap_scale_ff_, 1.0);         // 1.0 FF（小数刻度）
    expect_near(ff.res_scale_ohm_, 10.0);       // 10 OHM

    const SPFileHeader ff100 =
        sp_parse_header_file(data_path("units_ff100.spef").string());
    expect_near(ff100.cap_scale_ff_, 100.0);    // 100 FF
    // 无 *DIVIDER / *DELIMITER 的缺省分隔符
    EXPECT_EQ(ff100.divider_, '/');
    EXPECT_EQ(ff100.delimiter_, ':');
}

TEST(UnitsTest, ConvertedNetValues) {
    // 10 PF 刻度：0.005 x 10 x 1000 = 50 fF
    const SPChunkResult mix =
        sp_parse_spef_file(data_path("units_mix.spef").string());
    ASSERT_EQ(mix.nets_.size(), 1u);
    expect_near(mix.nets_[0].total_cap_reported_ff_, 50.0);
    expect_near(mix.nets_[0].ground_cap_ff_, 50.0);

    // 1 FF 刻度：数值直读 fF
    const SPChunkResult ff =
        sp_parse_spef_file(data_path("units_ff.spef").string());
    ASSERT_EQ(ff.nets_.size(), 1u);
    expect_near(ff.nets_[0].ground_cap_ff_, 2.0);

    // 100 FF 刻度：0.001 x 100 = 0.1 fF
    const SPChunkResult ff100 =
        sp_parse_spef_file(data_path("units_ff100.spef").string());
    ASSERT_EQ(ff100.nets_.size(), 1u);
    expect_near(ff100.nets_[0].ground_cap_ff_, 0.1);
}

TEST(UnitsTest, UnitWordCaseInsensitive) {
    // 单位词大小写不敏感（1 Kohm = 1e3 ohm）
    const SPFileHeader h = sp_parse_header_text(
        std_header("*C_UNIT 1 pf\n*R_UNIT 1 Kohm\n"), "t");
    expect_near(h.cap_scale_ff_, 1000.0);
    expect_near(h.res_scale_ohm_, 1000.0);
}

TEST(UnitsTest, InvalidUnitsRaise) {
    // 单位词 UF 不在合法集合
    EXPECT_THROW(sp_parse_header_text(std_header("*C_UNIT 1 UF\n"), "t"),
                 std::runtime_error);
    // 刻度 0 与负数
    EXPECT_THROW(sp_parse_header_text(std_header("*C_UNIT 0 PF\n"), "t"),
                 std::runtime_error);
    EXPECT_THROW(sp_parse_header_text(std_header("*R_UNIT -1 OHM\n"), "t"),
                 std::runtime_error);
    // 头部缺 *C_UNIT / *R_UNIT
    EXPECT_THROW(sp_parse_header_text(
                     "*SPEF \"IEEE 1481-1998\"\n*C_UNIT 1 PF\n", "t"),
                 std::runtime_error);
    // 非法时间单位词
    EXPECT_THROW(sp_parse_header_text(std_header("*T_UNIT 1 US\n"), "t"),
                 std::runtime_error);
}

TEST(UnitsTest, TimeInductanceUnitCaseVariants) {
    // 时间 / 电感单位词大小写变体同样合法（词表大小写不敏感），
    // 原声明按原文记录（可追溯）
    const SPFileHeader h = sp_parse_header_text(
        std_header("*T_UNIT 1 Ns\n*L_UNIT 1 Henry\n"), "t");
    EXPECT_EQ(h.time_unit_decl_, "1 Ns");
    EXPECT_EQ(h.ind_unit_decl_, "1 Henry");
}

TEST(UnitsTest, UnderflowTreatedAsZero) {
    // 下溢按 0 收录：1e-400 超 double 下界，strtod 报 ERANGE 但结果为
    // 0——量级小到下溢的电容物理上就是零，不是非法值（仅溢出判非法）
    const CMString text = std_header(
        "*D_NET n 1e-400\n*CAP\n1 n:1 2.5e-400\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    expect_near(r.nets_[0].total_cap_reported_ff_, 0.0);
    expect_near(r.nets_[0].ground_cap_ff_, 0.0);
    EXPECT_EQ(r.broken_net_count_, 0u);
    // 对照：溢出仍是非法值（1e999 > double 上界 → 段级破损）
    const CMString overflow = std_header("*D_NET n 1e999\n*END\n");
    SPFileHeader h2;
    const SPChunkResult r2 = sp_parse_spef_text(overflow, h2, 0, false, "t");
    EXPECT_EQ(r2.nets_.size(), 0u);
    EXPECT_EQ(r2.broken_net_count_, 1u);
}

// ── 矩阵条目 3：多值形态与取值规则（裁定 6）──────────────────────

class MultiValueTest : public ::testing::Test {
protected:
    // 四网样张：单值 / 三值 / 两值 / 四值（1 FF 直读）
    void SetUp() override {
        text_ = std_header(
            "*D_NET n1 5.0\n*END\n"
            "*D_NET n2 1.0:2.0:3.0\n*END\n"
            "*D_NET n3 1.0:2.0\n*END\n"
            "*D_NET n4 1.0:2.0:3.0:4.0\n*END\n");
    }

    const SPNet& net(const SPChunkResult& r, size_t i) const {
        return r.nets_[i];
    }

    CMString text_;
};

TEST_F(MultiValueTest, DefaultRule) {
    // 缺省规则：n == 3 取下标 1；其它取下标 0
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text_, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 4u);
    expect_near(net(r, 0).total_cap_reported_ff_, 5.0);
    expect_near(net(r, 1).total_cap_reported_ff_, 2.0);
    expect_near(net(r, 2).total_cap_reported_ff_, 1.0);
    expect_near(net(r, 3).total_cap_reported_ff_, 1.0);
    EXPECT_EQ(net(r, 0).value_count_, 1u);
    EXPECT_EQ(net(r, 1).value_count_, 3u);
    EXPECT_EQ(net(r, 2).value_count_, 2u);
    EXPECT_EQ(net(r, 3).value_count_, 4u);
    // 多值网数与最大值个数
    EXPECT_EQ(r.multi_value_net_count_, 3u);
    EXPECT_EQ(r.value_count_max_, 4u);
    EXPECT_EQ(r.value_index_fallback_count_, 0u);
}

TEST_F(MultiValueTest, ExplicitIndex) {
    // 显式下标 2：三值 1:2:3 取 3、四值 1:2:3:4 取 3（均为下标 2 的值）
    SPFileHeader h;
    const SPChunkResult r =
        sp_parse_spef_text(text_, h, 2, true, "t");
    ASSERT_EQ(r.nets_.size(), 4u);
    expect_near(net(r, 1).total_cap_reported_ff_, 3.0);
    expect_near(net(r, 3).total_cap_reported_ff_, 3.0);
    // 显式下标越界退回缺省规则并计数：单值（n1）与两值（n3）
    EXPECT_EQ(r.value_index_fallback_count_, 2u);
    expect_near(net(r, 0).total_cap_reported_ff_, 5.0);
    expect_near(net(r, 2).total_cap_reported_ff_, 1.0);
    EXPECT_EQ(r.multi_value_net_count_, 3u);
}

TEST_F(MultiValueTest, MultiValueInRecords) {
    // 记录内数值同样多值：CAP 值 6.0:7.0:8.0 取 7.0；显式下标覆盖
    const CMString cap_text = std_header(
        "*D_NET n_a 1.0\n"
        "*CAP\n1 n_a:1 6.0:7.0:8.0\n"
        "2 n_a:2 n_a:3 0.1:0.2\n"
        "*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(cap_text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    expect_near(r.nets_[0].ground_cap_ff_, 7.0);
    expect_near(r.nets_[0].coupling_cap_ff_, 0.1);
}

TEST_F(MultiValueTest, SignCheckedOnSelectedValueOnly) {
    // 多值形态的符号校验只看选中参与合计的值（裁定）：未选中的负值不
    // 触发破损。缺省规则下：三值 1:-2:3 取下标 1 = -2 → 进入合计为负
    // → 破损；两值 1:-2 取下标 0 = 1 → 合法收录。显式下标 2 下同一三
    // 值序列选中 3 → 合法收录（是否破损只由选中值决定）
    const CMString text = std_header(
        "*D_NET n_head_neg_sel 1.0:-2.0:3.0\n*END\n"
        "*D_NET n_head_ok 1.0:-2.0\n*END\n"
        "*D_NET n_cap 3.0\n*CAP\n"
        "1 n_cap:1 5.0:-1.0\n"          // 缺省取 5.0 → 合法
        "2 n_cap:2 -1.0:5.0\n"          // 缺省取 -1.0 → 该网破损
        "*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_head_ok");
    expect_near(r.nets_[0].total_cap_reported_ff_, 1.0);
    EXPECT_EQ(r.broken_net_count_, 2u);   // 选中负值的头与网

    // 显式下标 2：三值头选中 3.0 → 收录；两值头与两条 CAP 记录下标越
    // 界 → 退回缺省规则（fallback），n_cap 的记录 2 选中 -1.0 → 该网
    // 破损、其余收录
    SPFileHeader h2;
    const SPChunkResult r2 = sp_parse_spef_text(text, h2, 2, true, "t");
    ASSERT_EQ(r2.nets_.size(), 2u);
    EXPECT_EQ(r2.nets_[0].name_, "n_head_neg_sel");
    expect_near(r2.nets_[0].total_cap_reported_ff_, 3.0);
    EXPECT_EQ(r2.nets_[1].name_, "n_head_ok");
    EXPECT_EQ(r2.broken_net_count_, 1u);   // n_cap（记录 2 选中负值）
}

// ── 矩阵条目 4：名字映射 ─────────────────────────────────────────

class NameMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        result_ = sp_parse_spef_file(data_path("namemap.spef").string());
        header_ = sp_parse_header_file(data_path("namemap.spef").string());
    }

    SPChunkResult result_;
    SPFileHeader header_;
};

TEST_F(NameMapTest, HeaderMapTable) {
    EXPECT_EQ(header_.name_map_.size(), 4u);
    ASSERT_TRUE(header_.name_map_.count(5));
    EXPECT_EQ(header_.name_map_.at(5), "n_top");
    // 索引重复定义保留首份：*6 的第二个定义被丢弃
    EXPECT_EQ(header_.name_map_.at(6), "u_and1");
    EXPECT_EQ(header_.name_map_.at(7), "n_other");
    EXPECT_EQ(header_.name_map_.at(2453), "u_clkbuf");
    EXPECT_EQ(header_.name_map_dup_count_, 1u);
}

TEST_F(NameMapTest, NetNameExpansion) {
    // *D_NET *5 -> 网名展开为原名 n_top；未登记 *888 -> 整网跳过
    ASSERT_EQ(result_.nets_.size(), 2u);
    EXPECT_EQ(result_.nets_[0].name_, "n_top");
    EXPECT_EQ(result_.nets_[1].name_, "n_other");
    EXPECT_EQ(result_.net_count_, 2u);
    EXPECT_EQ(result_.name_map_miss_count_, 2u);   // 网名 1 + 节点 1
}

TEST_F(NameMapTest, NodeExpansionAndMiss) {
    const SPNet& net = result_.nets_[0];
    // kept：*6:ZN（实例引脚）、n_top:3（本网内部——原名引用）、
    // *2453:CLK（实例部分查表、引脚名 CLK 保持原样——耦合计入）
    expect_near(net.ground_cap_ff_, 7.0);   // 0.004 + 0.003（PF->fF）
    expect_near(net.coupling_cap_ff_, 2.0);
    EXPECT_EQ(net.cap_count_, 3u);
    EXPECT_EQ(net.coupling_cap_count_, 1u);
    // 第 4 条耦合 *6:ZN *999:X 0.001：*999 未登记 -> 记录丢弃
    EXPECT_EQ(result_.dropped_record_count_, 1u);
    // CONN *I *6:ZN O：映射展开后保留
    EXPECT_EQ(result_.conn_count_, 1u);
}

// ── 矩阵条目 5：节点分类与合计口径 ───────────────────────────────

TEST(NodeClassTest, AggregatesByClass) {
    const SPChunkResult r =
        sp_parse_spef_file(data_path("nodes_self_other.spef").string());
    ASSERT_EQ(r.nets_.size(), 1u);
    const SPNet& net = r.nets_[0];
    // ground：本网内部 1.0 + 实例引脚 3.0 + 端口 4.0；别的网内部 2.0
    // 不计入（external_ground_cap_count）
    expect_near(net.ground_cap_ff_, 8.0);
    // coupling：一端属于本网两条计入（1.0 + 1.0）；两端都是别的网内部
    // 不计入（external_coupling_cap_count）
    expect_near(net.coupling_cap_ff_, 2.0);
    EXPECT_EQ(net.cap_count_, 7u);
    EXPECT_EQ(net.coupling_cap_count_, 2u);
    EXPECT_EQ(net.res_count_, 1u);
    EXPECT_EQ(r.external_ground_cap_count_, 1u);
    EXPECT_EQ(r.external_coupling_cap_count_, 1u);
}

TEST(NodeClassTest, AllFourDelimiters) {
    // 四种分隔符各一例：本网内部 + 别的网内部的对地口径（头部的
    // *DELIMITER 与节点写法一致）
    for (const char delim : {'.', '/', '|', ':'}) {
        const CMString text =
            CMString("*SPEF \"IEEE 1481-1998\"\n*DIVIDER /\n*DELIMITER ") +
            CMString(1, delim) +
            "\n*C_UNIT 1 FF\n*R_UNIT 1 OHM\n"
            "*D_NET n_a 3.0\n*CAP\n1 n_a" + CMString(1, delim) +
            "5 1.0\n2 n_b" + CMString(1, delim) + "7 2.0\n*END\n";
        SPFileHeader h;
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u) << static_cast<char>(delim);
        expect_near(r.nets_[0].ground_cap_ff_, 1.0);
        EXPECT_EQ(r.external_ground_cap_count_, 1u);
        EXPECT_EQ(r.nets_[0].cap_count_, 2u);
    }
}

TEST(NodeClassTest, SplitAtLastDelimiter) {
    // 按最后一个分隔符切分：实例路径含层次分隔符 '.'、引脚分隔符 ':'
    const CMString text = std_header(
        "*D_NET n_a 4.0\n*CAP\n"
        "1 top.u1.inst:CLK 1.0\n"          // 实例引脚（前半含 '.'）
        "2 n_a:7 2.0\n"                    // 本网内部
        "3 top.u1.inst:CLK n_a:8 1.0\n"    // 耦合：实例引脚 + 本网内部
        "*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    expect_near(r.nets_[0].ground_cap_ff_, 3.0);
    expect_near(r.nets_[0].coupling_cap_ff_, 1.0);
    EXPECT_EQ(r.external_ground_cap_count_, 0u);
}

TEST(NodeClassTest, RefFormInternalNode) {
    // 本网内部节点的映射写法：*5:5（序号不从 1 起）经映射展开后等于
    // 当前网名 -> 本网内部；别的网映射写法 -> external
    const CMString text = std_header(
        "*NAME_MAP\n*5 n_a\n*6 n_b\n"
        "*D_NET *5 4.0\n*CAP\n1 *5:5 1.0\n2 *6:6 3.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_a");
    expect_near(r.nets_[0].ground_cap_ff_, 1.0);
    EXPECT_EQ(r.external_ground_cap_count_, 1u);
}

TEST(NodeClassTest, BusBracketNamesKeptVerbatim) {
    // 总线位方括号不转义（§2.5：OpenROAD 的写出器对网名不转义方括号，
    // 总线位直接写 count[0]）：网名与节点全部原样保留；节点切分只看
    // 引脚分隔符，方括号不影响分类
    const CMString text = std_header(
        "*D_NET n_a[0] 5.0\n*CAP\n"
        "1 n_a[0]:1 1.0\n"          // 本网内部节点（前半 == 网名）
        "2 u1:A[0] 3.0\n"           // 实例引脚节点（后半非纯数字）
        "3 u1:A[0] n_a[0]:2 1.0\n"  // 耦合：实例引脚 + 本网内部
        "*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_a[0]");
    expect_near(r.nets_[0].ground_cap_ff_, 4.0);
    expect_near(r.nets_[0].coupling_cap_ff_, 1.0);
    EXPECT_EQ(r.nets_[0].cap_count_, 3u);
    EXPECT_EQ(r.nets_[0].coupling_cap_count_, 1u);
    EXPECT_EQ(r.external_ground_cap_count_, 0u);
}

// ── 矩阵条目 6：方言容忍 ─────────────────────────────────────────

TEST(DialectTest, ConnForms) {
    const SPChunkResult r =
        sp_parse_spef_file(data_path("dialect_conn.spef").string());
    ASSERT_EQ(r.nets_.size(), 1u);
    // 4 条 *I + 2 条 *P；*N 坐标记录不计
    EXPECT_EQ(r.conn_count_, 6u);
    // 未知方向值 X：计数不报错
    EXPECT_EQ(r.unknown_direction_count_, 1u);
    // *L 观测和：0.001 + 4.0（2:4:6 取下标 1）+ 0.002
    expect_near(r.conn_pin_cap_obs_sum_ff_, 4.003);
    expect_near(r.nets_[0].ground_cap_ff_, 1.0);
}

TEST(DialectTest, MiscForms) {
    const SPChunkResult r =
        sp_parse_spef_file(data_path("dialect_misc.spef").string());
    ASSERT_EQ(r.nets_.size(), 2u);
    // *D_NET n 0 紧跟 *END：零值合法
    EXPECT_EQ(r.nets_[0].name_, "n_zero");
    expect_near(r.nets_[0].total_cap_reported_ff_, 0.0);
    EXPECT_EQ(r.nets_[0].cap_count_, 0u);
    // 关键字后零条记录：*CONN / *RES / *INDUC 空段
    EXPECT_EQ(r.conn_count_, 0u);
    EXPECT_EQ(r.res_count_, 0u);
    EXPECT_EQ(r.induc_count_, 0u);
    // 无编号与带编号同行混排 + 跨行记录：ground 0.5、coupling 1.0 + 1.5
    expect_near(r.nets_[1].ground_cap_ff_, 0.5);
    expect_near(r.nets_[1].coupling_cap_ff_, 2.5);
    EXPECT_EQ(r.nets_[1].cap_count_, 3u);
    EXPECT_EQ(r.nets_[1].coupling_cap_count_, 2u);
    // 头部：小写版本串 + DESIGN_FLOW 多串
    const SPFileHeader h =
        sp_parse_header_file(data_path("dialect_misc.spef").string());
    EXPECT_EQ(h.version_, "ieee 1481-1999");
    ASSERT_EQ(h.design_flow_.size(), 2u);
    EXPECT_EQ(h.design_flow_[0], "NAME_SCOPE LOCAL");
    EXPECT_EQ(h.design_flow_[1], "PIN_CAP NONE");
}

TEST(DialectTest, DesignFlowSingleString) {
    const SPFileHeader h = sp_parse_header_text(
        std_header("*DESIGN_FLOW \"PIN_CAP NONE\"\n*D_NET n 1.0\n*END\n"),
        "t");
    ASSERT_EQ(h.design_flow_.size(), 1u);
    EXPECT_EQ(h.design_flow_[0], "PIN_CAP NONE");
}

TEST(DialectTest, NumberedAndBareCapEquivalent) {
    // *CAP 带编号与不带编号解析结果逐项相同
    const CMString numbered = std_header(
        "*D_NET n 5.0\n*CAP\n1 n:1 1.0\n2 n:1 n:2 2.0\n*END\n");
    const CMString bare = std_header(
        "*D_NET n 5.0\n*CAP\nn:1 1.0\nn:1 n:2 2.0\n*END\n");
    SPFileHeader h1;
    SPFileHeader h2;
    const SPChunkResult r1 = sp_parse_spef_text(numbered, h1, 0, false, "t");
    const SPChunkResult r2 = sp_parse_spef_text(bare, h2, 0, false, "t");
    expect_same(r1, r2);
    expect_near(r1.nets_[0].ground_cap_ff_, 1.0);
    expect_near(r1.nets_[0].coupling_cap_ff_, 2.0);
}

TEST(DialectTest, ScientificNotationAndIntegers) {
    // 科学计数法与整数写法（1 FF 直读）
    const CMString text = std_header(
        "*D_NET n 5\n*CAP\n1 n:1 2.5e-01\n2 n:2 1E-01\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    expect_near(r.nets_[0].total_cap_reported_ff_, 5.0);
    expect_near(r.nets_[0].ground_cap_ff_, 0.25 + 0.1);
}

TEST(DialectTest, ConfidenceSuffixAccepted) {
    // 行尾 *V 置信度（裁定 12）：语法消费，网照常收录
    const CMString text = std_header("*D_NET n 1.0 *V 0.7\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n");
    expect_near(r.nets_[0].total_cap_reported_ff_, 1.0);
    EXPECT_EQ(r.broken_net_count_, 0u);
}

TEST(DialectTest, ConfidenceSuffixBroken) {
    // *V 置信度数值残缺（非数值）= 段级破损；后续网不受影响
    const CMString text = std_header(
        "*D_NET n_bad 1.0 *V abc\n*END\n"
        "*D_NET n_ok 2.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_ok");
    expect_near(r.nets_[0].total_cap_reported_ff_, 2.0);
    EXPECT_EQ(r.broken_net_count_, 1u);
}

TEST(DialectTest, NonConsumedNegativeValuesAccepted) {
    // 符号校验只对进入合计的值做（裁定）：不消费的数值字段负值接受、
    // 不报错——*L 负值照加观测和、*S 负值、*RES / *INDUC 负值（只计
    // 数）、缩减网 *RC 负延迟（只收录行首总电容）——全部收录、零破损
    const CMString text = std_header(
        "*D_NET n_d 2.0\n"
        "*CONN\n"
        "*I u_x:P O *L -0.5\n"
        "*I u_y:A I *S -0.01 -0.02\n"
        "*CAP\n1 n_d:1 1.0\n"
        "*RES\n1 n_d:1 n_d:2 -3.0\n"
        "*INDUC\n1 n_d:1 n_d:2 -0.001\n"
        "*END\n"
        "*R_NET n_r 5.0\n*RC n_r:1 -0.002\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 2u);
    EXPECT_EQ(r.nets_[0].name_, "n_d");
    EXPECT_EQ(r.nets_[0].kind_, SPNetKind::D_NET);
    expect_near(r.nets_[0].ground_cap_ff_, 1.0);
    EXPECT_EQ(r.nets_[1].name_, "n_r");
    EXPECT_EQ(r.nets_[1].kind_, SPNetKind::R_NET);
    expect_near(r.nets_[1].total_cap_reported_ff_, 5.0);
    EXPECT_EQ(r.broken_net_count_, 0u);
    // *L 负值计入观测和（照原样累加）
    EXPECT_EQ(r.conn_count_, 2u);
    expect_near(r.conn_pin_cap_obs_sum_ff_, -0.5);
    EXPECT_EQ(r.res_count_, 1u);
    EXPECT_EQ(r.induc_count_, 1u);
}

// ── 矩阵条目 7：跳过项 ───────────────────────────────────────────

TEST(SkipTest, RNetAndPnetAndInductance) {
    const SPChunkResult r =
        sp_parse_spef_file(data_path("rnet_pnet.spef").string());
    // 物理网两段整段跳过
    EXPECT_EQ(r.pnet_skip_count_, 2u);
    // R_NET 行首总电容收录、kind = R_NET（π 模型关键字全部被消费）
    ASSERT_EQ(r.nets_.size(), 2u);
    EXPECT_EQ(r.nets_[0].name_, "n_r");
    EXPECT_EQ(r.nets_[0].kind_, SPNetKind::R_NET);
    expect_near(r.nets_[0].total_cap_reported_ff_, 7.5);
    EXPECT_EQ(r.nets_[0].cap_count_, 0u);
    EXPECT_EQ(r.rnet_count_, 1u);
    // D_NET 照常解析；*INDUC 记录忽略并计数
    EXPECT_EQ(r.nets_[1].name_, "n_d");
    EXPECT_EQ(r.nets_[1].kind_, SPNetKind::D_NET);
    EXPECT_EQ(r.induc_count_, 2u);
    expect_near(r.nets_[1].ground_cap_ff_, 1.0);
    EXPECT_EQ(r.dnet_count_, 1u);
}

TEST(SkipTest, HeaderMinorConstructs) {
    const SPFileHeader h =
        sp_parse_header_file(data_path("rnet_pnet.spef").string());
    // 电源地网名被记录
    ASSERT_EQ(h.power_nets_.size(), 1u);
    EXPECT_EQ(h.power_nets_[0], "VDD");
    ASSERT_EQ(h.ground_nets_.size(), 1u);
    EXPECT_EQ(h.ground_nets_[0], "VSS");
}

TEST(SkipTest, RNetWithConnSectionTolerated) {
    // 缩减网带可选连接段（标准允许，商业工具文件里可能出现）：*CONN 按
    // 分布式网同一口径只统计不取值（*I/*P 行数、*L 观测和），整网不丢
    // 弃；对照：缩减网段内出现 *CAP 仍是段级破损
    const CMString text = std_header(
        "*R_NET n_r 7.5\n"
        "*CONN\n"
        "*I u_x:P O *D DRVC *L 0.001\n"
        "*P p_clk I\n"
        "*DRV n_r:1 *CELL DRVC *C2_R1_C1 2.0 3.0 1.5\n"
        "*END\n"
        "*R_NET n_bad 1.0\n*CAP\n1 n_bad:1 1.0\n*END\n"
        "*D_NET n_d 1.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 2u);
    EXPECT_EQ(r.nets_[0].name_, "n_r");
    EXPECT_EQ(r.nets_[0].kind_, SPNetKind::R_NET);
    expect_near(r.nets_[0].total_cap_reported_ff_, 7.5);
    // *CONN 统计照常生效：2 条连接行、*L 0.001 FF 直读
    EXPECT_EQ(r.conn_count_, 2u);
    expect_near(r.conn_pin_cap_obs_sum_ff_, 0.001);
    // 缩减网内的 *CAP 仍按段级破损；收尾 D_NET 不受影响
    EXPECT_EQ(r.broken_net_count_, 1u);
    EXPECT_EQ(r.nets_[1].name_, "n_d");
    EXPECT_EQ(r.nets_[1].kind_, SPNetKind::D_NET);
    EXPECT_EQ(r.rnet_count_, 1u);
    EXPECT_EQ(r.dnet_count_, 1u);
}

TEST(SkipTest, RNetNegativeModelValuesAccepted) {
    // 缩减网不消费的数值（π 模型 c2/r1/c1、埃尔莫尔延迟）只要求是合法
    // 数值，负值接受（裁定）：缩减网唯一收录的行首总电容不因它们丢弃
    const CMString text = std_header(
        "*R_NET n_neg1 1.0\n*DRV n_neg1:1 *CELL D *C2_R1_C1 -2.0 3.0 1.5\n"
        "*END\n"
        "*R_NET n_neg2 1.0\n*RC n_neg2:2 -0.001\n*END\n"
        "*D_NET n_ok 2.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 3u);
    EXPECT_EQ(r.nets_[0].name_, "n_neg1");
    EXPECT_EQ(r.nets_[0].kind_, SPNetKind::R_NET);
    expect_near(r.nets_[0].total_cap_reported_ff_, 1.0);
    EXPECT_EQ(r.nets_[1].name_, "n_neg2");
    expect_near(r.nets_[1].total_cap_reported_ff_, 1.0);
    EXPECT_EQ(r.nets_[2].name_, "n_ok");
    expect_near(r.nets_[2].total_cap_reported_ff_, 2.0);
    EXPECT_EQ(r.broken_net_count_, 0u);
    EXPECT_EQ(r.rnet_count_, 2u);
}

// ── 矩阵条目 8：注释 ─────────────────────────────────────────────

TEST(CommentTest, CommentsEverywhere) {
    const SPChunkResult r =
        sp_parse_spef_file(data_path("comments.spef").string());
    // 注释里的 fake / fake2 / fake3 不生效；真实网只有一个
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_a");
    expect_near(r.nets_[0].total_cap_reported_ff_, 1.0);
    expect_near(r.nets_[0].ground_cap_ff_, 2.0);
    EXPECT_EQ(r.net_count_, 1u);
}

TEST(CommentTest, UnclosedBlockCommentRaises) {
    // 块注释未闭合 = 文件级错误
    SPFileHeader h;
    EXPECT_THROW(sp_parse_spef_text(
                     std_header("/* 未闭合\n*D_NET n 1.0\n*END\n"), h, 0,
                     false, "t"),
                 std::runtime_error);
}

TEST(CommentTest, NetKeywordAfterSameLineCommentRecognized) {
    // 网段识别不依赖行首（评审改向）：网与网之间、同行注释之后的网段
    // 关键字照常开新网——`*END /* c */ *D_NET m` 的 m 正常收录、零破损
    // 零杂料；上一网的合法记录（1 n_a:1 1.0）不因同行内容受牵连。
    // 对照保留：网段内部（记录后未遇 *END）出现网段关键字仍是上一网
    // 段级破损（规则不变），新网从关键字处照常开始收录
    const CMString text = std_header(
        "*D_NET n_a 1.0\n*CAP\n"
        "1 n_a:1 1.0\n"
        "*END /* c */ *D_NET m 2.0\n"          // 网间：同行注释后开新网
        "*END\n"
        "*D_NET n_c 1.0\n*CAP\n"
        "1 n_c:1 1.0 /* c */ *D_NET n_d 3.0\n"  // 网内：n_c 缺 *END 破损
        "*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 3u);
    EXPECT_EQ(r.nets_[0].name_, "n_a");
    expect_near(r.nets_[0].ground_cap_ff_, 1.0);   // 记录合法归属上一网
    EXPECT_EQ(r.nets_[1].name_, "m");
    expect_near(r.nets_[1].total_cap_reported_ff_, 2.0);
    EXPECT_EQ(r.nets_[2].name_, "n_d");            // n_c 破损丢弃，n_d 收录
    expect_near(r.nets_[2].total_cap_reported_ff_, 3.0);
    EXPECT_EQ(r.broken_net_count_, 1u);   // 仅 n_c（网内缺 *END）
    EXPECT_EQ(r.stray_content_count_, 0u);
}

// ── 矩阵条目 9：文件级错误 ───────────────────────────────────────

TEST(FileLevelErrorTest, NotSpefHeader) {
    EXPECT_THROW(sp_parse_spef_file(data_path("bad_first_line.spef").string()),
                 std::runtime_error);
    SPFileHeader h;
    EXPECT_THROW(
        sp_parse_spef_text("*DESIGN \"t\"\n*C_UNIT 1 FF\n", h, 0, false, "t"),
        std::runtime_error);
    // 空文本同样不是 *SPEF 头
    EXPECT_THROW(sp_parse_header_text("", "t"), std::runtime_error);
}

TEST(FileLevelErrorTest, PathNotReadable) {
    EXPECT_THROW(sp_parse_spef_file(
                     data_path("no_such_file_anywhere.spef").string()),
                 std::runtime_error);
    EXPECT_THROW(sp_parse_header_file(
                     data_path("no_such_file_anywhere.spef").string()),
                 std::runtime_error);
    SPFileHeader h;
    EXPECT_THROW(sp_plan_file_chunks(
                     data_path("no_such_file_anywhere.spef").string(), 1024, h),
                 std::runtime_error);
}

TEST(FileLevelErrorTest, GzipMagic) {
    // gzip 魔数（0x1f 0x8b）开头：C++ 直连接口明确报错指明需先解压
    const std::string gz = std::string("\x1f\x8b", 2) + "compressed body";
    const fs::path p = write_temp("gzip_magic.spef", gz);
    try {
        sp_parse_spef_file(p.string());
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(CMString(e.what()).find("gzip"), CMString::npos);
    }
    SPFileHeader h;
    EXPECT_THROW(sp_plan_file_chunks(p.string(), 1024, h), std::runtime_error);
}

TEST(FileLevelErrorTest, HeaderDefects) {
    SPFileHeader h;
    // 头部缺 *C_UNIT（*R_UNIT 在）
    EXPECT_THROW(sp_parse_spef_text(
                     "*SPEF \"IEEE 1481-1998\"\n*R_UNIT 1 OHM\n"
                     "*D_NET n 1.0\n*END\n",
                     h, 0, false, "t"),
                 std::runtime_error);
    // 分隔符取值非法（# 不在 . / | : 集合）
    EXPECT_THROW(sp_parse_header_text(std_header("*DELIMITER #\n"), "t"),
                 std::runtime_error);
    EXPECT_THROW(sp_parse_header_text(std_header("*DIVIDER #\n"), "t"),
                 std::runtime_error);
    // 名字映射条目残缺：只有索引没有名字
    EXPECT_THROW(sp_parse_header_text(
                     std_header("*NAME_MAP\n*5\n*PORTS\np1 I\n"), "t"),
                 std::runtime_error);
    // 总线分隔符取值非法（非括号字符 / 不配对）
    EXPECT_THROW(sp_parse_header_text(std_header("*BUS_DELIMITER 1 }\n"), "t"),
                 std::runtime_error);
    EXPECT_THROW(sp_parse_header_text(std_header("*BUS_DELIMITER [}\n"), "t"),
                 std::runtime_error);
}

// ── 矩阵条目 10：段级错误（其它网不受影响）───────────────────────

TEST(SegmentErrorTest, MissingEndOnLastNet) {
    const SPChunkResult r =
        sp_parse_spef_file(data_path("bad_missing_end.spef").string());
    // 最后一个网缺 *END：整网丢弃、计数 1；之前的网完好
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_ok");
    expect_near(r.nets_[0].ground_cap_ff_, 1.0);
    EXPECT_EQ(r.broken_net_count_, 1u);
    EXPECT_EQ(r.net_count_, 1u);
}

TEST(SegmentErrorTest, BrokenNetsIsolated) {
    // 四种段级破损各一网，前后完好网不受影响
    const CMString text = std_header(
        // 1) *CAP 值不是数字
        "*D_NET n_first 1.0\n*CAP\n1 n_first:1 abc\n*END\n"
        // 2) 网段头缺总电容值
        "*D_NET n_headless\n*CAP\n1 n_headless:1 1.0\n*END\n"
        // 3) 段内出现不认识的 * 关键字
        "*D_NET n_mid 1.0\n*XYZ 1 2\n*END\n"
        // 4) 进入合计的对地电容记录值为负（负值不能进入合计）
        "*D_NET n_negcap 1.0\n*CAP\n1 n_negcap:1 -1.0\n*END\n"
        // 完好收尾网
        "*D_NET n_last 3.0\n*CAP\n1 n_last:1 3.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_last");
    expect_near(r.nets_[0].ground_cap_ff_, 3.0);
    EXPECT_EQ(r.broken_net_count_, 4u);
    EXPECT_EQ(r.net_count_, 1u);
}

TEST(SegmentErrorTest, MissingEndBeforeNextNet) {
    // *END 缺失但后面还有网段关键字：破损网到下一网关键字收口
    const CMString text = std_header(
        "*D_NET n_a 1.0\n*CAP\n1 n_a:1 1.0\n"
        "*D_NET n_b 2.0\n*CAP\n1 n_b:1 2.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_b");
    expect_near(r.nets_[0].ground_cap_ff_, 2.0);
    EXPECT_EQ(r.broken_net_count_, 1u);
}

TEST(SegmentErrorTest, NegativeReportedCapIsBroken) {
    // 进入合计的行首总电容为负（单值即选中值）→ 段级破损——负值不能
    // 进入合计
    const CMString text = std_header(
        "*D_NET n_a -1.0\n*END\n*D_NET n_b 2.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_b");
    EXPECT_EQ(r.broken_net_count_, 1u);
}

TEST(SegmentErrorTest, TruncatedRecordKeepsFollowingNet) {
    // 记录残缺且紧跟网段关键字：残缺记录所在网按段级破损计数，但被读
    // 到的网段关键字必须留在流里——紧随的网照常收录、不无声丢失（跳
    // 过必须计数：关键字属于网边界而非杂料，stray 为 0）。四个残缺口
    // 径逐个覆盖；对照面（记录完整后段边界出现网关键字）由
    // MissingEndBeforeNextNet 钉住
    SPFileHeader h;
    // 1) *CAP 记录缺值后紧跟网段关键字
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0\n*CAP\n1 n_a:1\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        expect_near(r.nets_[0].total_cap_reported_ff_, 2.0);
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 2) 网段头缺总电容值
    {
        const CMString text = std_header(
            "*D_NET n_a\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 3) *I 缺名字
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0\n*CONN\n*I\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 4) *V 缺置信度
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0 *V\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
}

// ── 网与网之间的杂料（跳过但计数——无声丢内容不可接受）─────────────

TEST(StrayContentTest, StrayBetweenNetsCounted) {
    // 杂料口径 = 连续杂料 token 序列记 1 次（token 间仅空白/注释相连算
    // 同一序列）：孤立 token 与三 token 残缺片段各计 1，不随个数膨胀；
    // 两个网照常收录
    const CMString text = std_header(
        "*D_NET n_a 1.0\n*END\n"
        "garbage\n"                       // 孤立杂料 token：计 1
        "*D_NET n_b 2.0\n*END\n"
        "leftover 1.5 tokens\n"           // 连续三 token：同一序列计 1
        "*D_NET n_c 3.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 3u);
    EXPECT_EQ(r.nets_[0].name_, "n_a");
    EXPECT_EQ(r.nets_[1].name_, "n_b");
    EXPECT_EQ(r.nets_[2].name_, "n_c");
    EXPECT_EQ(r.stray_content_count_, 2u);
    EXPECT_EQ(r.broken_net_count_, 0u);
}

TEST(StrayContentTest, HeaderRegionStrayNotCounted) {
    // 口径边界：stray_content_count_ 只统计头段结束后的网段区——头部区
    // 域的散料（版本串周边本就容忍，且无丢网风险：网关键字一律被识别
    // 为头段终点）由头部解析静默跳过，不计入网段区杂料
    const CMString text = std_header(
        "stray_before_first_net\n"
        "*D_NET n_a 1.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].name_, "n_a");
    EXPECT_EQ(r.stray_content_count_, 0u);
}

// ── 矩阵条目 11 + 12：切块计划与整文件/分段等价 ──────────────────

// 程序化生成含 30 个网段、段长已知的文件文本；顺带生成一个含巨型网段
// 的变体（单网超过 chunk_size 不拆分）。header 含 *PORTS 块。
struct GeneratedFile {
    CMString header_text_;
    CMString net_text_;           // 30 个常规网
    CMString big_net_text_;       // 巨型网（仅 big 变体使用）
    fs::path path_;
};

constexpr size_t kNetCount = 30;

CMString make_regular_nets() {
    CMString nets;
    for (size_t i = 0; i < kNetCount; ++i) {
        nets += "*D_NET n_" + std::to_string(i) + " 3.0\n"
                "*CONN\n*I u:P I\n"
                "*CAP\n1 n_" + std::to_string(i) + ":1 1.0\n2 n_" +
                std::to_string(i) + ":2 2.0\n"
                "*RES\n1 n_" + std::to_string(i) + ":1 n_" +
                std::to_string(i) + ":2 3.0\n*END\n";
    }
    return nets;
}

GeneratedFile write_generated(const char* name, bool with_big_net) {
    GeneratedFile g;
    g.header_text_ =
        "*SPEF \"IEEE 1481-1998\"\n*DIVIDER /\n*DELIMITER :\n"
        "*C_UNIT 1 FF\n*R_UNIT 1 OHM\n*PORTS\np1 I\n";
    g.net_text_ = make_regular_nets();
    // 巨型网紧随头部（首个网段）——保证它是某块的起点，块长超过
    // chunk_size 即证明单网段不被拆分
    g.big_net_text_ = "*D_NET n_big 1.0\n*CAP\n";
    for (int i = 0; i < 300; ++i) {
        g.big_net_text_ += "100000 n_big:1 0.001\n";
    }
    g.big_net_text_ += "*END\n";
    CMString text = g.header_text_;
    if (with_big_net) {
        text += g.big_net_text_;   // 巨型网段（约 8 KB > chunk_size）
    }
    text += g.net_text_;
    g.path_ = write_temp(name, text);
    return g;
}

TEST(ChunkPlanTest, BoundariesAndCoverage) {
    const GeneratedFile g = write_generated("spef_plan_30.spef", false);
    const uint64_t chunk_size = 128;   // 每网约 90 字节 -> 多块
    SPFileHeader plan_header;
    const SPFileChunkPlan plan = sp_plan_file_chunks(g.path_.string(),
                                                     chunk_size, plan_header);
    // 一次扫描同时产出文件头（流程层免二次整读）：单位换算可用
    expect_near(plan_header.cap_scale_ff_, 1.0);
    EXPECT_EQ(plan_header.delimiter_, ':');
    EXPECT_EQ(plan.file_name_, g.path_.string());
    const uint64_t file_size = read_all(g.path_).size();
    EXPECT_EQ(plan.file_size_, file_size);

    // header_end 恰是首个网段关键字的偏移（头部含 *PORTS 块）
    const CMString whole = read_all(g.path_);
    const uint64_t first_net = whole.find("*D_NET n_0");
    ASSERT_NE(first_net, CMString::npos);
    EXPECT_EQ(plan.header_end_, first_net);
    ASSERT_FALSE(plan.chunks_.empty());

    uint64_t covered = 0;
    for (size_t i = 0; i < plan.chunks_.size(); ++i) {
        const SPFileChunk& c = plan.chunks_[i];
        // 每块起点恰好是某个网段关键字的行首（前一字符为换行且块首
        // 文本以 *D_NET 打头）
        ASSERT_EQ(whole.substr(c.start_, 6), "*D_NET");
        ASSERT_GE(c.start_, 1u);
        EXPECT_EQ(whole[c.start_ - 1], '\n');
        // 相邻块首尾相接；最后一块到文件尾
        if (i + 1 < plan.chunks_.size()) {
            EXPECT_EQ(c.end_, plan.chunks_[i + 1].start_);
        } else {
            EXPECT_EQ(c.end_, file_size);
        }
        ASSERT_LT(c.start_, c.end_);
        covered += c.end_ - c.start_;
    }
    // 从 header_end 到文件尾每个字节恰好属于一块
    EXPECT_EQ(covered, file_size - plan.header_end_);
    EXPECT_EQ(plan.chunks_.front().start_, plan.header_end_);
}

TEST(ChunkPlanTest, OversizedNetNotSplit) {
    const GeneratedFile g = write_generated("spef_plan_big.spef", true);
    const uint64_t chunk_size = 1024;
    SPFileHeader plan_header;
    const SPFileChunkPlan plan = sp_plan_file_chunks(g.path_.string(),
                                                     chunk_size, plan_header);
    ASSERT_FALSE(plan.chunks_.empty());
    // 巨型网是首块（头部终点即其关键字行首），整段独占一块不拆分
    //（块长超过 chunk_size）
    const CMString whole = read_all(g.path_);
    const uint64_t big_start = whole.find("*D_NET n_big");
    ASSERT_NE(big_start, CMString::npos);
    ASSERT_EQ(plan.chunks_.front().start_, big_start);
    EXPECT_EQ(plan.header_end_, big_start);
    EXPECT_GT(plan.chunks_.front().end_ - plan.chunks_.front().start_,
              chunk_size);
    // 巨型网块之后的常规网块照常收口
    EXPECT_GT(plan.chunks_.size(), 1u);
}

TEST(ChunkEquivalenceTest, ChunkedParseEqualsWholeFile) {
    const GeneratedFile g = write_generated("spef_equiv_30.spef", false);
    // 切块扫描产出的文件头直接供块解析使用（与整文件解析的头部等价面）
    SPFileHeader header;
    const SPFileChunkPlan plan =
        sp_plan_file_chunks(g.path_.string(), 200, header);
    ASSERT_FALSE(plan.chunks_.empty());

    const CMString whole = read_all(g.path_);
    CMVector<SPChunkResult> parts;
    for (const SPFileChunk& c : plan.chunks_) {
        parts.push_back(sp_parse_chunk_text(
            whole.substr(c.start_, c.end_ - c.start_), header, 0, false,
            g.path_.string()));
    }
    const SPChunkResult merged = merge_chunks(parts);
    const SPChunkResult all = sp_parse_spef_file(g.path_.string());
    expect_same(merged, all);

    // 30 网的合计期望（1 FF 直读）
    EXPECT_EQ(all.net_count_, kNetCount);
    EXPECT_EQ(all.cap_count_, 2 * kNetCount);
    EXPECT_EQ(all.res_count_, kNetCount);
    EXPECT_EQ(all.conn_count_, kNetCount);
    expect_near(all.nets_.back().ground_cap_ff_, 3.0);
}

TEST(ChunkEquivalenceTest, NamemapFixtureEquivalent) {
    // 名字映射样张同样满足分段等价（含映射展开与丢弃计数）
    const fs::path p = data_path("namemap.spef");
    SPFileHeader header;
    const SPFileChunkPlan plan = sp_plan_file_chunks(p.string(), 60, header);
    ASSERT_FALSE(plan.chunks_.empty());
    const CMString whole = read_all(p);
    CMVector<SPChunkResult> parts;
    for (const SPFileChunk& c : plan.chunks_) {
        parts.push_back(sp_parse_chunk_text(
            whole.substr(c.start_, c.end_ - c.start_), header, 0, false,
            p.string()));
    }
    expect_same(merge_chunks(parts), sp_parse_spef_file(p.string()));
}

TEST(ChunkPlanTest, MidLineNetKeywordStaysInChunk) {
    // 「行首」判定只服务切块扫描：行中的网段关键字（同行注释后）不切
    // 出新块，所在行留在原块——块解析按 token 识别网段（位置无关），
    // 网不丢；逐块解析与整文件解析逐项一致
    const CMString text = std_header(
        "*D_NET n_a 1.0\n*CAP\n"
        "1 n_a:1 1.0\n"
        "*END /* c */ *D_NET n_b 2.0\n"   // n_b 的关键字在行中间
        "*END\n"
        "*D_NET n_c 3.0\n*END\n");
    const fs::path p = write_temp("spef_midline.spef", text);
    SPFileHeader header;
    const SPFileChunkPlan plan = sp_plan_file_chunks(p.string(), 64, header);
    const CMString whole = read_all(p);
    // n_b 的关键字在行中 → 不是块边界：只有 n_a 与 n_c 两个边界
    const uint64_t n_c_start = whole.find("*D_NET n_c");
    ASSERT_NE(n_c_start, CMString::npos);
    ASSERT_EQ(plan.chunks_.size(), 2u);
    EXPECT_EQ(plan.chunks_[0].start_, plan.header_end_);
    EXPECT_EQ(plan.chunks_[0].end_, n_c_start);      // n_b 留在第一块
    EXPECT_EQ(plan.chunks_[1].start_, n_c_start);
    EXPECT_EQ(plan.chunks_[1].end_, plan.file_size_);
    // 逐块解析与整文件解析一致：三个网都收录（n_b 不因行中关键字丢失）
    CMVector<SPChunkResult> parts;
    for (const SPFileChunk& c : plan.chunks_) {
        parts.push_back(sp_parse_chunk_text(
            whole.substr(c.start_, c.end_ - c.start_), header, 0, false,
            p.string()));
    }
    expect_same(merge_chunks(parts), sp_parse_spef_file(p.string()));
    const SPChunkResult all = sp_parse_spef_file(p.string());
    ASSERT_EQ(all.nets_.size(), 3u);
    EXPECT_EQ(all.nets_[1].name_, "n_b");
    expect_near(all.nets_[1].total_cap_reported_ff_, 2.0);
    EXPECT_EQ(all.broken_net_count_, 0u);
    EXPECT_EQ(all.stray_content_count_, 0u);
}

// ── 矩阵条目 13：真实工具样张（环境变量开关，默认跳过）─────────────
//
// 为什么不设默认路径：真实样张是仓库之外、不受版本管理的本地文件——
// 样张在就真跑、不在就跳过，会让同一份提交在不同机器上实际执行的断
// 言不一样；样张路径本指向临时调研目录（任务结束即清理），项目规则
// 也禁止在测试里硬编码本机数据路径。因此本用例默认一律跳过。
//
// 为什么不把真实样张入库：OpenSTA 自带样张是 GPL-3.0 许可；布局布线
// 工具产出的设计数据许可状态待裁定——都不拷入仓库。
//
// 怎么用：设置环境变量指向任意一份真实工具产出的 SPEF 绝对路径（用例
// 里读环境变量，bazel 需经 --test_env 透传），例如：
//   ./fly.sh test //src/emir/spef/tests:sp_parser_test \
//       --cache_test_results=no \
//       --test_arg=--gtest_filter='*RealToolSample*' \
//       --test_env=FLY_SPEF_REAL_SAMPLE=/abs/path/to/sample.spef
TEST(RealSampleTest, RealToolSample) {
    const char* env = std::getenv("FLY_SPEF_REAL_SAMPLE");
    if (env == nullptr || *env == '\0') {
        GTEST_SKIP() << "未指定真实样张（设置环境变量 FLY_SPEF_REAL_SAMPLE "
                        "指向真实工具产出的 SPEF 绝对路径后运行）";
    }
    const CMString sample = env;
    ASSERT_TRUE(fs::exists(sample)) << "指定的真实样张不存在: " << sample;
    SPFileHeader header;
    const SPChunkResult r = sp_parse_spef_file(sample, header, 0, false);
    // 真实文件解析必须成功：无破损网、网数大于 0
    EXPECT_EQ(r.broken_net_count_, 0u);
    EXPECT_GT(r.net_count_, 0u);
    // 通用不变量（对任何真实样张都应成立）：收录计数与网表一致；逐网
    // 电容合计均为非负的有限值（不出现 NaN/inf/负值）
    EXPECT_EQ(r.net_count_, r.nets_.size());
    for (const SPNet& net : r.nets_) {
        EXPECT_TRUE(std::isfinite(net.ground_cap_ff_)) << net.name_;
        EXPECT_TRUE(std::isfinite(net.coupling_cap_ff_)) << net.name_;
        EXPECT_GE(net.ground_cap_ff_, 0.0) << net.name_;
        EXPECT_GE(net.coupling_cap_ff_, 0.0) << net.name_;
    }
    std::cout << "[ 真实样张 ] nets=" << r.net_count_
              << " dnet=" << r.dnet_count_ << " rnet=" << r.rnet_count_
              << " cap=" << r.cap_count_
              << " coupling=" << r.coupling_cap_count_
              << " res=" << r.res_count_
              << " dropped=" << r.dropped_record_count_
              << " map_miss=" << r.name_map_miss_count_ << std::endl;
}

// ── 矩阵条目 10 补充（第 H 节第 1 条）：进入合计的值判文件数值 ──────

TEST(SegmentErrorTest, NegativeSubnormalCapRecordIsBroken) {
    // 对地/耦合电容的符号校验判文件数值（第 H 节第 1 条修正）：原判乘
    // 过换算系数后的飞法值，「负的次正规数 × 小于 1 的换算系数」下溢成
    // -0.0（-1e-323 × 0.001 = -0.0），-0.0 < 0.0 为假，负值漏判。修正后
    // 三处（行首/对地/耦合）统一判文件数值再乘系数——本复现输入应为段
    // 级破损
    const CMString text =
        "*SPEF \"IEEE 1481-1998\"\n*C_UNIT 0.001 FF\n*R_UNIT 1 OHM\n"
        "*D_NET n_a 2.0\n*CAP\n1 n_a:1 -1e-323\n*END\n";
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    EXPECT_EQ(r.nets_.size(), 0u);
    EXPECT_EQ(r.broken_net_count_, 1u);
    // 耦合电容同口径：负的次正规数（0.001 FF 档换算后为 -0.0）→ 破损
    const CMString coupling_text =
        "*SPEF \"IEEE 1481-1998\"\n*C_UNIT 0.001 FF\n*R_UNIT 1 OHM\n"
        "*D_NET n_a 2.0\n*CAP\n1 n_a:1 n_a:2 -1e-323\n*END\n";
    SPFileHeader h2;
    const SPChunkResult r2 =
        sp_parse_spef_text(coupling_text, h2, 0, false, "t");
    EXPECT_EQ(r2.nets_.size(), 0u);
    EXPECT_EQ(r2.broken_net_count_, 1u);
}

TEST(SegmentErrorTest, NegativeZeroStoredAsPositiveZero) {
    // 存储值不得出现负零（第 H 节第 1 条顺带修正）：-0.0 字面量是合法
    // 数值（-0.0 < 0.0 为假，正常收录），乘换算系数后结果 -0.0 归 0.0
    const CMString text = std_header(
        "*D_NET n_a -0.0\n*CAP\n1 n_a:1 -0.0\n2 n_a:2 n_a:3 -0.0\n*END\n");
    SPFileHeader h;
    const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
    ASSERT_EQ(r.nets_.size(), 1u);
    EXPECT_EQ(r.nets_[0].total_cap_reported_ff_, 0.0);
    EXPECT_EQ(r.nets_[0].ground_cap_ff_, 0.0);
    EXPECT_EQ(r.nets_[0].coupling_cap_ff_, 0.0);
    EXPECT_FALSE(std::signbit(r.nets_[0].total_cap_reported_ff_));
    EXPECT_FALSE(std::signbit(r.nets_[0].ground_cap_ff_));
    EXPECT_FALSE(std::signbit(r.nets_[0].coupling_cap_ff_));
}

// ── 第 H 节第 3 条：本轮放宽位置的值位残缺回归（关键字放回语义不被
//    改回去）────────────────────────────────────────────────────────

TEST(SegmentErrorTest, TruncatedNonConsumedValueKeepsFollowingNet) {
    // 本轮放宽的六处（只要求合法数值、值不消费：*L、*S、*RES / *INDUC
    // 值、缩减网 *C2_R1_C1、*RC）的值位缺失且紧跟网段关键字——残缺记
    // 录所在网按段级破损计数，但被读到的网段关键字必须留在流里，紧随
    // 的网照常收录、不无声丢失（stray 为 0——关键字属于网边界而非杂料）
    SPFileHeader h;
    // 1) *L 值位缺失（CONN 属性）
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0\n*CONN\n*I u_x:P O *L\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 2) *S 值位缺失（转换时间两值）
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0\n*CONN\n*I u_x:P O *S\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 3) *RES 值位缺失
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0\n*RES\n1 n_a:1 n_a:2\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 4) *INDUC 值位缺失
    {
        const CMString text = std_header(
            "*D_NET n_a 1.0\n*INDUC\n1 n_a:1 n_a:2\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 5) 缩减网 *C2_R1_C1 值位缺失
    {
        const CMString text = std_header(
            "*R_NET n_a 1.0\n*DRV n_a:1 *CELL D *C2_R1_C1\n"
            "*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
    // 6) 缩减网 *RC 值位缺失
    {
        const CMString text = std_header(
            "*R_NET n_a 1.0\n*RC\n*D_NET n_b 2.0\n*END\n");
        const SPChunkResult r = sp_parse_spef_text(text, h, 0, false, "t");
        ASSERT_EQ(r.nets_.size(), 1u);
        EXPECT_EQ(r.nets_[0].name_, "n_b");
        EXPECT_EQ(r.broken_net_count_, 1u);
        EXPECT_EQ(r.stray_content_count_, 0u);
    }
}

// ── 字节区间读取（第 H 节 C1：块任务把切块计划的字节区间读成文本）──

TEST(ByteRangeTest, ReadsMiddleRange) {
    const fs::path p = write_temp("spef_range.spef",
                                  std_header("*D_NET n_a 1.0\n*END\n"));
    const CMString whole = read_all(p);
    const CMString part = sp_read_byte_range(p.string(), 5, 20);
    ASSERT_EQ(part.size(), 15u);
    EXPECT_EQ(whole.substr(5, 15), part);
}

TEST(ByteRangeTest, OutOfRangeOrUnreadableThrows) {
    const fs::path p = write_temp("spef_range_oob.spef", "0123456789");
    // 区间终点越出文件大小
    EXPECT_THROW(sp_read_byte_range(p.string(), 0, 11), std::runtime_error);
    EXPECT_THROW(sp_read_byte_range(p.string(), 100, 200),
                 std::runtime_error);
    // 文件不可读
    EXPECT_THROW(sp_read_byte_range("no_such_file_anywhere.spef", 0, 1),
                 std::runtime_error);
    // 空区间合法（start == end）
    EXPECT_EQ(sp_read_byte_range(p.string(), 4, 4).size(), 0u);
}

}  // namespace
