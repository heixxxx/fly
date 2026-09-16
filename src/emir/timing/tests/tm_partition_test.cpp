// timing db 落库形态 + 名字换算 + 分区合并单测（plan §11：名字换算 +
// 路由 + 合并语义，用合成 design db 数据——同 ds_hier_test 先例）：
//   1. T1 切块扫描：块表 + 块协议等价性（分块逐块换算合并 ≡ 整文件）；
//   2. T2 名字换算：纯路径（网/引脚/常量兜底）+ block_inst 绑定 +
//      block_cell 定义级复制 + strip_prefix 段级剥离（plan §2/§7）；
//   3. T3 分区合并：跨文件冲突保留首份（TIMG::0006）与同文件跨块同源
//      形态静默合并的区分；
//   4. T4 汇总：summary 归并 + 时钟表顶层优先跨文件合并（TIMG::0007）；
//   5. 落库形态序列化 round-trip。
// 合成场景（两层树 + block cell 双实例化；net 区间含 local 0 空洞位——
// 2026-09-14 裁定 count = 真网数 + 1、global = start + local）：
//   cells: INV(0) + B1(1, block_cell；port pin PIN_IN + VDD[POWER])
//   pin hasher: A=0 Z=1 PIN_IN=2 VDD=3（全局 pin 名字空间，裁定 3）
//   defs: top{top1=INV, c1=B1, c2=B1; nt,npg_top} / b1{u1=INV; na,nb,npg}
//   树（DFS 前序）：
//     #0 top (self 0, inst [0,4), net [0,3))
//      ├ #1 B1#1 (self 2, inst [4,6), net [3,7))
//      └ #2 B1#2 (self 3, inst [6,8), net [7,11))
//   global: top1=1 c1=2 c2=3 u1#1=5 u1#2=7；nt=1 npg_top=2
//           na=4 nb=5 npg=6（B1#1）；na=8 nb=9 npg=10（B1#2）
//   分区：单分区 pid=0（id map 段表/段对象合成）；pg 网 2/6/10 入 power 集
#include <emir/timing/cpp/tm_parser.h>
#include <emir/timing/cpp/tm_partition.h>

#include <common/testing/cpp/test_helpers.h>
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_types.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

namespace {

using namespace fly;
using namespace fly::test;

// —— 合成 design db 快照（两层树 + block cell 双实例化）——————

constexpr CMPinId kPinA{0};
constexpr CMPinId kPinZ{1};
constexpr CMPinId kPinIn{2};
constexpr CMPartitionId kPid{0};

struct SynthEnv {
    std::shared_ptr<const DSDesign> design;
    std::shared_ptr<const DSBlockNames> top_names;
    std::shared_ptr<const DSBlockNames> b1_names;

    SynthEnv() {
        auto d = std::make_shared<DSDesign>();
        // INV_X1：A(INPUT) / Z(OUTPUT)
        DSCell inv;
        inv.set_name("INV_X1");
        inv.set_lef_cell();
        inv.add_pin(DSPin{});
        inv.add_pin(DSPin{});
        d->add_cell(std::move(inv));
        d->get_cell(CMCellId{0}).pins_[0].direction_ = DSPinDirection::INPUT;
        d->get_cell(CMCellId{0}).pins_[1].direction_ = DSPinDirection::OUTPUT;
        d->get_cell(CMCellId{0}).pins_[0].pin_id_ = kPinA;
        d->get_cell(CMCellId{0}).pins_[1].pin_id_ = kPinZ;
        // top：root block cell（真实建库中 S4 头扫描合成入表——DSBlockNames
        // 注入按 cell 名解析，缺失则 root hasher 不注入、纯路径换算全挂）
        DSCell top_cell;
        top_cell.set_name("top");
        top_cell.set_block_cell();
        d->add_cell(std::move(top_cell));
        // B1：block cell + port pin PIN_IN(INPUT) / VDD(POWER)
        DSCell b1;
        b1.set_name("B1");
        b1.set_block_cell();
        b1.add_pin(DSPin{});
        b1.add_pin(DSPin{});
        d->add_cell(std::move(b1));
        DSCell& b1_ref = d->get_cell(CMCellId{2});
        b1_ref.pins_[0].set_port();
        b1_ref.pins_[0].direction_ = DSPinDirection::INPUT;
        b1_ref.pins_[0].pin_id_ = kPinIn;
        b1_ref.pins_[1].set_port();
        b1_ref.pins_[1].type_ = DSPinType::POWER;
        b1_ref.pins_[1].direction_ = DSPinDirection::INPUT;
        b1_ref.pins_[1].pin_id_ = CMPinId{3};
        // 全局 pin 名字空间（键 = 裸 pin 名，裁定 3）
        d->register_pin("A");
        d->register_pin("Z");
        d->register_pin("PIN_IN");
        d->register_pin("VDD");

        // per-DEF 产物（建树输入；实例名 = 树节点名——DSBlockNames 同名）
        auto make_block = [](const char* name,
                             const CMVector<std::pair<CMString, CMCellId>>&
                                 insts,
                             size_t net_count) {
            DSBlockBuildData b;
            b.init_placeholder(name, CMCellId{});
            for (const auto& inst : insts) {
                DSInstance inst_obj;
                inst_obj.set_cell_id(inst.second);
                b.add_instance(std::move(inst_obj), inst.first);
            }
            for (size_t i = 0; i < net_count; ++i) {
                (void)b.register_net("n" + std::to_string(i));
            }
            return b;
        };
        CMVector<DSBlockBuildData> blocks;
        blocks.push_back(make_block("B1", {{"u1", CMCellId{0}}}, 3));
        blocks.push_back(make_block(
            "top",
            {{"top1", CMCellId{0}}, {"c1", CMCellId{2}}, {"c2", CMCellId{2}}},
            2));
        CMVector<DSNetBuildData> nets(2);
        CMVector<const DSBlockBuildData*> block_ptrs;
        CMVector<const DSNetBuildData*> net_ptrs;
        for (const auto& b : blocks) {
            block_ptrs.push_back(&b);
        }
        for (const auto& n : nets) {
            net_ptrs.push_back(&n);
        }
        d->set_hier_tree(ds_build_hier_tree(block_ptrs, net_ptrs, *d));

        // 块 local 名空间伴生对象（与 per-DEF 产物同名对齐）
        auto names = [](const char* block,
                        const CMVector<std::pair<CMString, uint64_t>>& insts,
                        const CMVector<std::pair<CMString, uint64_t>>& nets_) {
            auto n = std::make_shared<DSBlockNames>();
            n->block_name_ = block;
            for (const auto& item : insts) {
                n->instance_names_->assign(item.first, item.second);
            }
            for (const auto& item : nets_) {
                n->net_names_->assign(item.first, item.second);
            }
            return n;
        };
        top_names = names("top", {{"top1", 1}, {"c1", 2}, {"c2", 3}},
                          {{"nt", 1}, {"npg_top", 2}});
        b1_names = names("B1", {{"u1", 1}},
                         {{"na", 1}, {"nb", 2}, {"npg", 3}});
        design = std::move(d);
    }
};

// T2 换算上下文组装（快照全量注入；单分区 pid 0）
std::shared_ptr<TMDesignContext> make_context(const SynthEnv& env,
                                              const DSPartitionNets& nets) {
    auto ctx = std::make_shared<TMDesignContext>();
    ctx->set_design(env.design);
    ctx->add_block_names(env.top_names);
    ctx->add_block_names(env.b1_names);
    auto index = std::make_shared<DSIdPartitionIndex>();
    index->id_starts_ = {0};
    ctx->set_inst_id_map(index);
    ctx->set_net_id_map(index);
    auto seg = std::make_shared<DSIdPartitionSegment>();
    seg->id_start_ = 0;
    seg->pids_.resize(16, kPid);
    ctx->add_inst_segment(seg);
    ctx->add_net_segment(seg);
    auto pg = std::make_shared<DSPgNetSet>();
    pg->power_.insert(CMNetId{2});   // npg_top（顶层）
    pg->power_.insert(CMNetId{6});   // npg（B1#1）
    pg->power_.insert(CMNetId{10});  // npg（B1#2）
    ctx->set_pg_nets(pg);
    auto nets_shared = std::make_shared<DSPartitionNets>(nets);
    ctx->add_partition_nets(nets_shared);
    return ctx;
}

// 分区 NETS 合成：nt=1 驱动网 / na=4 无驱动 / nb=5 双端 / na=8 无驱动 /
// nb=9 双端（B1#2 侧）；pg 网 2/6/10 不入库（pg 判定先行）
DSPartitionNets make_partition_nets() {
    DSPartitionNets nets;
    nets.part_id_ = kPid;
    auto add_net = [&nets](uint64_t net_id) {
        auto n = std::make_shared<DSNet>();
        n->net_id_ = CMNetId{net_id};
        nets.nets_[CMNetId{net_id}] = std::move(n);
        return nets.nets_[CMNetId{net_id}];
    };
    auto add_conn = [](const CMSharedPtr<DSNet>& n, uint64_t inst,
                       CMPinId pin, bool driver, bool receiver) {
        DSNetConnEntry c;
        c.inst_id_ = CMInstanceId{inst};
        c.pin_id_ = pin;
        if (driver) {
            c.set_driver();
        }
        if (receiver) {
            c.set_receiver();
        }
        n->connections_.push_back(std::move(c));
    };
    const CMSharedPtr<DSNet> nt = add_net(1);
    add_conn(nt, 1, kPinZ, true, false);     // top1/Z 驱动
    const CMSharedPtr<DSNet> na1 = add_net(4);
    add_conn(na1, 5, kPinA, false, true);    // 仅接收 → 无驱动
    const CMSharedPtr<DSNet> nb1 = add_net(5);
    add_conn(nb1, 5, kPinZ, true, false);    // u1#1/Z 驱动
    add_conn(nb1, 1, kPinA, false, true);
    const CMSharedPtr<DSNet> na2 = add_net(8);
    add_conn(na2, 7, kPinA, false, true);    // B1#2 侧无驱动
    const CMSharedPtr<DSNet> nb2 = add_net(9);
    add_conn(nb2, 7, kPinZ, true, false);    // u1#2/Z 驱动
    add_conn(nb2, 1, kPinA, false, true);
    return nets;
}

// —— 测试辅助 ——————————————————————————————

class TmPartitionTest : public ::testing::Test {
protected:
    // 测试工作目录（qa_tmp_dir 惯例：TEST_TMPDIR 沙箱优先，bazel 自动
    // 回收；手动直跑 fallback .work/gtest_tmp）
    CMString tmp_;

    void SetUp() override {
        tmp_ = fly::test::qa_tmp_dir("tm_partition_test");
        std::filesystem::create_directories(tmp_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmp_, ec);  // 清理失败不掩盖测试结果
    }

    void write_file(const char* name, const CMString& text) {
        std::ofstream out(std::filesystem::path(tmp_) / name,
                          std::ios::binary);
        out << text;
    }

    std::string file_path(const char* name) const {
        return (std::filesystem::path(tmp_) / name).string();
    }

    // 整文件转换（生产路径口径：先 T1 规划——块起点跳过 (TIMING_WINDOWS
    // 外皮，再 T2 处理全部块合并；默认大块界 = 单块）
    TMEntrySlice convert_whole(const TMDesignContext& ctx,
                               const std::string& path,
                               const TMFileBinding& binding,
                               uint32_t file_index) const {
        const TMFileChunkPlan plan = tm_plan_file_chunks(path, 1u << 30);
        // 大块界恒单块（测试文件规模 << 1 GiB）
        return tm_convert_chunk(ctx, path, plan.chunk_starts_[0],
                                plan.chunk_ends_[0], binding, file_index);
    }

    static const TMInstanceTiming* find_inst(const TMPartitionTiming& part,
                                             uint64_t inst_id) {
        const auto it = part.items_.find(CMInstanceId{inst_id});
        return it == part.items_.end() ? nullptr : &it->second;
    }
};

// 标准顶层纯路径 TWF（nt 驱动 / npg_top pg 网 / na 无驱动 / 未知网 /
// 顶层引脚条目 + 未命中族）
const char* kTopTwf =
    "(TIMING_WINDOWS\n"
    "(HEADER (VERSION \"t\") (DESIGN \"top\") (DELIMITERS \"/\")\n"
    " (TIME_SCALE 1.000E-09))\n"
    "(WAVEFORM \"clk\" 1.0 (POSEDGE 0.0) (NEGEDGE 0.5))\n"
    "(CAUSED_BY \"clk\"\n"
    "(NET \"nt\" 0.1:0.2 0.01 0.5 0.001 0.3:0.4 0.02 0.5 0.001)\n"
    "(NET \"npg_top\" 0.1:0.2 0.01 * * 0.3:0.4 0.02 * *)\n"
    ")\n"
    "(CAUSED_BY NULL\n"
    "(NET \"c1/na\" 0.5:0.5 0.03 * * 0.6:0.6 0.04 * *)\n"
    "(NET \"ghost\" 0.5:0.5 0.03 * * 0.6:0.6 0.04 * *)\n"
    "(PIN \"top1/Z\" 0.1:0.2 0.01 * * 0.3:0.4 0.02 * *)\n"
    "(PIN \"ghost_inst/Z\" 0.1:0.2 0.01 * * 0.3:0.4 0.02 * *)\n"
    "(PIN \"top1/QQ\" 0.1:0.2 0.01 * * 0.3:0.4 0.02 * *)\n"
    "(PIN \"c2\" 0.1:0.2 0.01 * * 0.3:0.4 0.02 * *)\n"
    ")\n"
    ")\n";

// ── 1. T1 切块扫描 ──────────────────────────────────────────────────

TEST_F(TmPartitionTest, PlanFileChunksSingleBlock) {
    write_file("top.twf", kTopTwf);
    const TMFileChunkPlan plan =
        tm_plan_file_chunks(file_path("top.twf"), 1u << 20);
    EXPECT_EQ(plan.file_name_, file_path("top.twf"));
    EXPECT_EQ(plan.chunk_starts_.size(), 1u);   // 大块界 = 单块
    // 首块起于首个顶层子构造（跳过 (TIMING_WINDOWS 外皮）
    const size_t header_pos = CMString(kTopTwf).find("(HEADER");
    EXPECT_EQ(plan.chunk_starts_[0], header_pos);
    EXPECT_EQ(plan.file_size_, CMString(kTopTwf).size());
}

TEST_F(TmPartitionTest, PlanFileChunksRespectsBoundary) {
    // 引号内括号不计深度（字符串含 '(' ——切块边界不落入引号内子构造）
    write_file("quoted.twf",
               "(TIMING_WINDOWS\n"
               "(HEADER (VERSION \"a(b\")\n"
               "(CAUSED_BY NULL (NET \"n1\" 1:1 1:1 * * 1:1 1:1 * *))\n"
               "(CAUSED_BY NULL (NET \"n2\" 1:1 1:1 * * 1:1 1:1 * *))\n"
               ")\n");
    // 小块界 → 顶层子构造间切分（HEADER/两个 CAUSED_BY 三段 ≥ 2 块）
    const TMFileChunkPlan plan =
        tm_plan_file_chunks(file_path("quoted.twf"), 64);
    ASSERT_GE(plan.chunk_starts_.size(), 2u);
    // 每块包装外皮后均可独立解析（安全切点的本质语义——切点恒落在顶层
    // 构造边界，不落入引号/字段内容）
    for (size_t i = 0; i < plan.chunk_starts_.size(); ++i) {
        const uint64_t end = plan.chunk_ends_[i];
        std::ifstream in(file_path("quoted.twf"), std::ios::binary);
        in.seekg(static_cast<std::streamoff>(plan.chunk_starts_[i]));
        std::ostringstream ss;
        ss << in.rdbuf();
        const CMString text = ss.str().substr(0, end - plan.chunk_starts_[i]);
        const TMTimingFile chunk = tm_parse_twf_text(
            "(TIMING_WINDOWS\n" + text + "\n)", "chunk");
        EXPECT_EQ(chunk.bad_record_count_, 0u);
        EXPECT_EQ(chunk.unknown_construct_count_, 0u);
    }
}

// 块协议等价性：小块界逐块换算合并 ≡ 整文件单块换算（分块 TWF 读取的
// 正确性基准——plan §2 文件列表 + §6 T1/T2 形态）
TEST_F(TmPartitionTest, ChunkedConversionEqualsWholeFile) {
    write_file("top.twf", kTopTwf);
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());

    const TMEntrySlice whole = convert_whole(*ctx, file_path("top.twf"),
                                             TMFileBinding{}, 0);

    const TMFileChunkPlan plan =
        tm_plan_file_chunks(file_path("top.twf"), 64);
    ASSERT_GE(plan.chunk_starts_.size(), 2u);
    // reserve 后指针稳定（vector 扩容搬移会使 &slices.back() 悬垂）
    const size_t chunk_count = plan.chunk_starts_.size();
    CMVector<TMEntrySlice> slices;
    slices.reserve(chunk_count);
    CMVector<const TMEntrySlice*> slice_ptrs;
    slice_ptrs.reserve(chunk_count);
    for (size_t i = 0; i < chunk_count; ++i) {
        slices.push_back(tm_convert_chunk(*ctx, file_path("top.twf"),
                                          plan.chunk_starts_[i],
                                          plan.chunk_ends_[i],
                                          TMFileBinding{}, 0));
        slice_ptrs.push_back(&slices.back());
    }
    uint64_t conflicts = 0;
    const TMPartitionTiming merged =
        tm_merge_partition(slice_ptrs, kPid, conflicts);

    // 条目级等价：合并分区对象 vs 整文件分区的条目集一致（同文件跨块
    // 的 NET 条目与其驱动 PIN 条目同指 (1, Z)——同源静默合并不计冲突）
    const TMInstanceTiming* nt_whole = find_inst(whole.partitions_.at(kPid), 1);
    const TMInstanceTiming* nt_merged = find_inst(merged, 1);
    ASSERT_NE(nt_whole, nullptr);
    ASSERT_NE(nt_merged, nullptr);
    ASSERT_EQ(nt_merged->pins_.size(), nt_whole->pins_.size());
    EXPECT_EQ(conflicts, 0u);
    EXPECT_EQ(nt_merged->pins_[0].pin_id_, kPinZ);
}

// ── 2. T2 名字换算：纯路径（网/引脚/常量）───────────────────────────

TEST_F(TmPartitionTest, ConvertNetEntriesTopLevelFile) {
    write_file("top.twf", kTopTwf);
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    const TMEntrySlice slice = convert_whole(*ctx, file_path("top.twf"),
                                             TMFileBinding{}, 0);

    // 逐文件统计
    ASSERT_EQ(slice.stats_.files_.size(), 1u);
    const TMFileStats& fs = slice.stats_.files_[0];
    EXPECT_EQ(fs.source_file_, file_path("top.twf"));
    EXPECT_EQ(fs.entry_count_, 8u);
    EXPECT_EQ(fs.hit_count_, 2u);               // nt + top1/Z（同 (1,Z) 合一）
    // nt → driver top1(1)/Z(1)
    const TMPartitionTiming& part = slice.partitions_.at(kPid);
    const TMInstanceTiming* top1 = find_inst(part, 1);
    ASSERT_NE(top1, nullptr);
    EXPECT_EQ(top1->clock_id_, CMClockId{0});   // clk 组
    const TMPinTiming* z = top1->find_pin(kPinZ);
    ASSERT_NE(z, nullptr);
    EXPECT_NEAR(z->rise_arrival_.min_, 0.1, 1e-9);
    EXPECT_NEAR(z->rise_arrival_.max_, 0.2, 1e-9);
    EXPECT_TRUE(z->is_rise_slew());
    EXPECT_NEAR(z->fall_arrival_.max_, 0.4, 1e-9);
    EXPECT_EQ(part.size(), 1u);                 // 仅 top1
    // 计数：na 悬空 1 + ghost 网名 1 + pg 1；引脚条目 top1/Z 命中、
    // ghost_inst/Z 实例未命中、top1/QQ pin 未命中、c2 形态错
    EXPECT_EQ(fs.dangling_net_count_, 1u);
    EXPECT_EQ(fs.net_name_miss_count_, 1u);
    EXPECT_EQ(slice.stats_.pg_net_skip_count_, 1u);
    EXPECT_EQ(fs.skipped_instance_count_, 1u);
    EXPECT_EQ(fs.skipped_pin_count_, 2u);
    // 弃收计数（RDr/FDr 有值：nt 两沿各 1 次 = 2）
    EXPECT_EQ(slice.stats_.dropped_source_res_count_, 2u);
    // 时钟表随块产出（T4 输入）
    ASSERT_EQ(slice.stats_.clocks_.size(), 1u);
    EXPECT_EQ(slice.stats_.clocks_[0].name_, "clk");
}

TEST_F(TmPartitionTest, ConvertPinEntriesAndConstantFallback) {
    // 独立 CONSTANT 条目：网形态命中 + 引脚形态兜底（§7 表「上述任一」）
    write_file("const.twf",
               "(TIMING_WINDOWS\n"
               "(HEADER (DELIMITERS \"/\") (TIME_SCALE 1.000E-09))\n"
               "(CAUSED_BY NULL\n"
               "(NET CONSTANT \"c1/nb\")\n"
               "(NET CONSTANT \"top1/Z\")\n"
               "(NET CONSTANT \"ghost\")\n"
               ")\n"
               ")\n");
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    const TMEntrySlice slice = convert_whole(*ctx, file_path("const.twf"),
                                             TMFileBinding{}, 0);
    const TMPartitionTiming& part = slice.partitions_.at(kPid);
    // nb 网形态 → driver u1#1(5)/Z constant 位
    const TMInstanceTiming* u1 = find_inst(part, 5);
    ASSERT_NE(u1, nullptr);
    const TMPinTiming* z = u1->find_pin(kPinZ);
    ASSERT_NE(z, nullptr);
    EXPECT_TRUE(z->is_constant());
    EXPECT_FALSE(z->is_rise_arrival());
    // top1/Z 引脚形态兜底 → top1(1)/Z
    const TMInstanceTiming* top1 = find_inst(part, 1);
    ASSERT_NE(top1, nullptr);
    EXPECT_NE(top1->find_pin(kPinZ), nullptr);
    // ghost 双形态未命中 → 网名未命中计数
    const TMFileStats& fs = slice.stats_.files_[0];
    EXPECT_EQ(fs.net_name_miss_count_, 1u);
    EXPECT_EQ(slice.stats_.const_entry_count_, 2u);
    EXPECT_EQ(fs.hit_count_, 2u);
}

// ── 3. T2 块实例绑定（block_inst 局部名偏移换算，plan §7.1）─────────

TEST_F(TmPartitionTest, ConvertBlockInstBinding) {
    write_file("b1.twf",
               "(TIMING_WINDOWS\n"
               "(HEADER (DELIMITERS \"/\") (TIME_SCALE 1.000E-09))\n"
               "(CAUSED_BY NULL\n"
               "(NET \"nb\" 0.2:0.3 0.01 * * 0.4:0.5 0.02 * *)\n"
               "(PIN \"u1/Z\" 0.2:0.3 0.01 * * 0.4:0.5 0.02 * *)\n"
               "(NET \"PIN_IN\" 0.1:0.1 0.01 * * 0.2:0.2 0.02 * *)\n"
               "(NET \"VDD\" 0.1:0.1 0.01 * * 0.2:0.2 0.02 * *)\n"
               "(NET \"ghost_local\" 0.1:0.1 0.01 * * 0.2:0.2 0.02 * *)\n"
               "(PIN \"u1/QQ\" 0.1:0.1 0.01 * * 0.2:0.2 0.02 * *)\n"
               ")\n"
               ")\n");
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    TMFileBinding binding;
    binding.kind = 1;
    binding.block_inst = "c1";  // B1#1（self 2；块内 u1 → global 5）
    const TMEntrySlice slice =
        convert_whole(*ctx, file_path("b1.twf"), binding, 0);

    const TMPartitionTiming& part = slice.partitions_.at(kPid);
    // nb → u1#1(5)/Z；u1/Z pin 条目 → 同一 (5, Z)（同块重复防御保留首份）
    const TMInstanceTiming* u1 = find_inst(part, 5);
    ASSERT_NE(u1, nullptr);
    ASSERT_EQ(u1->pins_.size(), 1u);   // NET 与 PIN 两路同 (inst,pin) 合一
    // 块端口 PIN_IN → 归属块实例自身 c1(2)/PIN_IN(2)——§7.1 归属键语义
    const TMInstanceTiming* c1 = find_inst(part, 2);
    ASSERT_NE(c1, nullptr);
    EXPECT_NE(c1->find_pin(kPinIn), nullptr);
    // VDD = pg 端口 → §7.5 跳过计数
    EXPECT_EQ(slice.stats_.pg_net_skip_count_, 1u);
    // ghost_local 块内未命中 / u1/QQ pin 未命中
    const TMFileStats& fs = slice.stats_.files_[0];
    EXPECT_EQ(fs.net_name_miss_count_, 1u);
    EXPECT_EQ(fs.skipped_pin_count_, 1u);
    EXPECT_EQ(fs.hit_count_, 3u);  // nb + u1/Z + PIN_IN
}

// ── 4. T2 块定义绑定（block_cell 定义级全实例复制，plan §7.2）───────

TEST_F(TmPartitionTest, ConvertBlockCellBindingReplicatesAllInstances) {
    write_file("b1cell.twf",
               "(TIMING_WINDOWS\n"
               "(HEADER (DELIMITERS \"/\") (TIME_SCALE 1.000E-09))\n"
               "(CAUSED_BY NULL\n"
               "(PIN \"u1/Z\" 0.2:0.3 0.01 * * 0.4:0.5 0.02 * *)\n"
               "(NET \"nb\" 0.2:0.3 0.01 * * 0.4:0.5 0.02 * *)\n"
               ")\n"
               ")\n");
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    TMFileBinding binding;
    binding.kind = 2;
    binding.block_cell = "B1";  // 两个实例化 c1/c2 → u1#1(5) 与 u1#2(7)
    const TMEntrySlice slice =
        convert_whole(*ctx, file_path("b1cell.twf"), binding, 0);
    const TMPartitionTiming& part = slice.partitions_.at(kPid);
    const TMInstanceTiming* u1_a = find_inst(part, 5);
    const TMInstanceTiming* u1_b = find_inst(part, 7);
    ASSERT_NE(u1_a, nullptr);
    ASSERT_NE(u1_b, nullptr);
    EXPECT_NE(u1_a->find_pin(kPinZ), nullptr);   // 定义级复制 → 两实例同值
    EXPECT_NE(u1_b->find_pin(kPinZ), nullptr);
    const TMPinTiming* pa = u1_a->find_pin(kPinZ);
    const TMPinTiming* pb = u1_b->find_pin(kPinZ);
    EXPECT_EQ(pa->rise_arrival_.min_, pb->rise_arrival_.min_);
    EXPECT_EQ(slice.stats_.files_[0].hit_count_, 2u);  // 两条件目全命中
}

// ── 5. T2 strip_prefix 段级剥离（§7.4，块绑定组合）──────────────────

TEST_F(TmPartitionTest, ConvertStripPrefix) {
    write_file("wrapped.twf",
               "(TIMING_WINDOWS\n"
               "(HEADER (DELIMITERS \"/\") (TIME_SCALE 1.000E-09))\n"
               "(CAUSED_BY NULL\n"
               "(PIN \"tb_top/u_dut/u1/Z\" 0.2:0.3 0.01 * * 0.4:0.5 0.02 "
               "* *)\n"
               "(NET \"tb_top/u_dutx/nb\" 0.1:0.1 0.01 * * 0.2:0.2 0.02 "
               "* *)\n"
               "(NET \"tb_top/u_dut\" 0.1:0.1 0.01 * * 0.2:0.2 0.02 * *)\n"
               ")\n"
               ")\n");
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    TMFileBinding binding;
    binding.kind = 1;
    binding.block_inst = "c1";
    binding.strip_prefix = "tb_top/u_dut";  // 段级精确匹配
    const TMEntrySlice slice =
        convert_whole(*ctx, file_path("wrapped.twf"), binding, 0);

    // 剥后 u1/Z → u1#1(5)/Z 命中
    const TMPartitionTiming& part = slice.partitions_.at(kPid);
    const TMInstanceTiming* u1 = find_inst(part, 5);
    ASSERT_NE(u1, nullptr);
    EXPECT_NE(u1->find_pin(kPinZ), nullptr);
    // 未命中前缀（u_dutx 段不匹配防误配）+ 剥后余空（条目名恰为前缀）
    const TMFileStats& fs = slice.stats_.files_[0];
    EXPECT_EQ(fs.strip_miss_count_, 2u);   // TIMG::0010
    EXPECT_EQ(fs.hit_count_, 1u);
    EXPECT_EQ(fs.entry_count_, 3u);
}

// strip_prefix 整文件零命中：空结果放行 + 计数（不 fatal）
TEST_F(TmPartitionTest, StripPrefixAllMissPassesEmpty) {
    write_file("allmiss.twf",
               "(TIMING_WINDOWS\n"
               "(HEADER (DELIMITERS \"/\") (TIME_SCALE 1.000E-09))\n"
               "(CAUSED_BY NULL (NET \"other/n1\" 0.1:0.1 0.01 * * 0.2:0.2 "
               "0.02 * *))\n"
               ")\n");
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    TMFileBinding binding;
    binding.kind = 1;
    binding.block_inst = "c1";
    binding.strip_prefix = "tb_top/u_dut";
    const TMEntrySlice slice =
        convert_whole(*ctx, file_path("allmiss.twf"), binding, 0);
    EXPECT_TRUE(slice.partitions_.empty());
    const TMFileStats& fs = slice.stats_.files_[0];
    EXPECT_EQ(fs.strip_miss_count_, 1u);
    EXPECT_EQ(fs.hit_count_, 0u);
}

// ── 6. 单块语法破损兜底（plan §6 范式 (b)）：空分片照常产出 ─────────

TEST_F(TmPartitionTest, BrokenChunkYieldsEmptySliceWithCounter) {
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    write_file("bad.twf", "(TIMING_WINDOWS (CAUSED_BY NULL (NET \"n\"");
    const TMEntrySlice slice =
        tm_convert_chunk(*ctx, file_path("bad.twf"), 0, UINT64_MAX,
                         TMFileBinding{}, 0);
    EXPECT_TRUE(slice.partitions_.empty());
    ASSERT_EQ(slice.stats_.files_.size(), 1u);
    EXPECT_EQ(slice.stats_.files_[0].failed_chunk_count_, 1u);
    EXPECT_EQ(slice.stats_.files_[0].entry_count_, 0u);
}

// 绑定目标不存在（入口校验后环境漂移防御）：块失败兜底计数，不 raise
TEST_F(TmPartitionTest, MissingBindingTargetFallsBackToChunkFailure) {
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    write_file("ok.twf", kTopTwf);
    TMFileBinding binding;
    binding.kind = 1;
    binding.block_inst = "no_such_inst";
    const TMEntrySlice slice =
        tm_convert_chunk(*ctx, file_path("ok.twf"), 0, UINT64_MAX, binding, 0);
    EXPECT_TRUE(slice.partitions_.empty());
    EXPECT_EQ(slice.stats_.files_[0].failed_chunk_count_, 1u);
}

// ── 7. T3 分区合并：跨文件冲突保留首份（TIMG::0006）────────────────

TEST_F(TmPartitionTest, MergePartitionConflictsKeepFirst) {
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    // 两个「文件」（file_index 0/1）：同实例同 pin 不同值——跨文件同名
    // 条目冲突场景
    write_file("a.twf",
               "(TIMING_WINDOWS (CAUSED_BY NULL (PIN \"top1/Z\" 0.1:0.2 "
               "0.01 * * 0.3:0.4 0.02 * *)) )\n");
    write_file("b.twf",
               "(TIMING_WINDOWS (CAUSED_BY NULL (PIN \"top1/Z\" 0.9:1.0 "
               "0.09 * * 0.11:0.12 0.08 * *)) )\n");
    const TMEntrySlice sa = convert_whole(*ctx, file_path("a.twf"),
                                          TMFileBinding{}, 0);
    const TMEntrySlice sb = convert_whole(*ctx, file_path("b.twf"),
                                          TMFileBinding{}, 1);
    CMVector<const TMEntrySlice*> slices = {&sa, &sb};
    uint64_t conflicts = 0;
    const TMPartitionTiming merged =
        tm_merge_partition(slices, kPid, conflicts);
    EXPECT_EQ(conflicts, 1u);
    const TMInstanceTiming* top1 = find_inst(merged, 1);
    ASSERT_NE(top1, nullptr);
    ASSERT_EQ(top1->pins_.size(), 1u);   // 保留首份
    EXPECT_NEAR(top1->pins_[0].rise_arrival_.min_, 0.1, 1e-9);
}

// ── 8. T4 汇总：summary 归并 + 时钟表顶层优先（TIMG::0007）──────────

TEST_F(TmPartitionTest, MergeSummaryAggregatesPerFile) {
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    write_file("a.twf",
               "(TIMING_WINDOWS (CAUSED_BY NULL (PIN \"top1/Z\" 0.1:0.2 "
               "0.01 * * 0.3:0.4 0.02 * *)) )\n");
    write_file("b.twf",
               "(TIMING_WINDOWS (CAUSED_BY NULL (NET \"ghost\" 0.1:0.1 "
               "0.01 * * 0.2:0.2 0.02 * *)) )\n");
    const TMStatsDelta da =
        convert_whole(*ctx, file_path("a.twf"), TMFileBinding{}, 0).stats_;
    const TMStatsDelta db =
        convert_whole(*ctx, file_path("b.twf"), TMFileBinding{}, 1).stats_;
    CMVector<const TMStatsDelta*> deltas = {&da, &db};
    const TMSummary summary = tm_merge_summary(deltas, 7);
    ASSERT_EQ(summary.files_.size(), 2u);
    EXPECT_EQ(summary.total_entry_count_, 2u);
    EXPECT_EQ(summary.total_hit_count_, 1u);
    EXPECT_EQ(summary.net_name_miss_count_, 1u);
    EXPECT_EQ(summary.cross_file_conflict_count_, 7u);
    // 逐文件字段可追溯（来源文件名清单）
    EXPECT_EQ(summary.files_[0].source_file_, file_path("a.twf"));
    EXPECT_EQ(summary.files_[1].source_file_, file_path("b.twf"));
}

TEST_F(TmPartitionTest, MergeClocksTopLevelPriority) {
    // 顶层（kind 0）定义优先于块绑定文件；周期差异计数（TIMG::0007）。
    // 块绑定文件先出（文件序 0）、顶层文件后出（文件序 1）——顶层仍优先
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    write_file("blk.twf",
               "(TIMING_WINDOWS (WAVEFORM \"clk\" 2.0 (POSEDGE 0.0) "
               "(NEGEDGE 1.0)) )\n");
    write_file("top.twf",
               "(TIMING_WINDOWS (WAVEFORM \"clk\" 1.0 (POSEDGE 0.0) "
               "(NEGEDGE 0.5)) (WAVEFORM \"clk2\" 3.0) )\n");
    const TMEntrySlice sb = convert_whole(*ctx, file_path("blk.twf"),
                                          TMFileBinding{}, 0);
    const TMEntrySlice st = convert_whole(*ctx, file_path("top.twf"),
                                          TMFileBinding{}, 1);
    // 文件内跨块归并（同名首份）后按 (kind, clocks) 传入（TMClock →
    // TMClockTable::Entry 搬运）
    CMVector<std::pair<int, TMClockTable>> file_clocks;
    TMClockTable blk_table;
    for (const TMClock& c : sb.stats_.clocks_) {
        TMClockTable::Entry e;
        e.name_ = c.name_;
        e.period_ = c.period_;
        e.posedge_ = c.posedge_;
        e.negedge_ = c.negedge_;
        blk_table.clocks_.push_back(std::move(e));
    }
    file_clocks.emplace_back(2, std::move(blk_table));   // 块绑定（kind 2）
    TMClockTable top_table;
    for (const TMClock& c : st.stats_.clocks_) {
        TMClockTable::Entry e;
        e.name_ = c.name_;
        e.period_ = c.period_;
        e.posedge_ = c.posedge_;
        e.negedge_ = c.negedge_;
        top_table.clocks_.push_back(std::move(e));
    }
    file_clocks.emplace_back(0, std::move(top_table));   // 顶层（kind 0）
    uint64_t conflicts = 0;
    const TMClockTable merged = tm_merge_clocks(file_clocks, conflicts);
    ASSERT_EQ(merged.clocks_.size(), 2u);
    EXPECT_EQ(merged.clocks_[0].name_, "clk");
    EXPECT_DOUBLE_EQ(merged.clocks_[0].period_, 1.0);    // 顶层定义保留
    EXPECT_EQ(merged.clocks_[1].name_, "clk2");
    EXPECT_EQ(conflicts, 1u);   // clk 周期差异（2.0 vs 1.0）
    EXPECT_EQ(merged.find("clk")->posedge_, 0.0);
    EXPECT_EQ(merged.find("clk")->negedge_, 0.5);
}

TEST_F(TmPartitionTest, MergeClocksBindsFileOrderFallback) {
    // 无顶层文件：块绑定文件按文件序首份兜底
    CMVector<std::pair<int, TMClockTable>> file_clocks;
    TMClockTable first;
    TMClockTable::Entry c1;
    c1.name_ = "clk";
    c1.period_ = 2.0;
    first.clocks_.push_back(c1);
    file_clocks.emplace_back(1, std::move(first));
    TMClockTable second;
    TMClockTable::Entry c2;
    c2.name_ = "clk";
    c2.period_ = 3.0;
    second.clocks_.push_back(c2);
    file_clocks.emplace_back(2, std::move(second));
    uint64_t conflicts = 0;
    const TMClockTable merged = tm_merge_clocks(file_clocks, conflicts);
    ASSERT_EQ(merged.clocks_.size(), 1u);
    EXPECT_DOUBLE_EQ(merged.clocks_[0].period_, 2.0);   // 文件序首份兜底
    EXPECT_EQ(conflicts, 1u);
}

// ── 9. 落库形态序列化 round-trip ────────────────────────────────────

TEST_F(TmPartitionTest, SerializationRoundTrip) {
    const SynthEnv env;
    const auto ctx = make_context(env, make_partition_nets());
    write_file("top.twf", kTopTwf);
    const TMEntrySlice slice = convert_whole(*ctx, file_path("top.twf"),
                                             TMFileBinding{}, 0);

    // TMEntrySlice（含分区片段 + 统计 + 时钟表）
    CMString blob;
    FLY_ENCODE(slice, blob);
    TMEntrySlice back;
    FLY_DECODE(blob, TMEntrySlice, back);
    ASSERT_EQ(back.partitions_.size(), slice.partitions_.size());
    const TMPinTiming* z_back =
        back.partitions_.at(kPid).items_.at(CMInstanceId{1}).find_pin(kPinZ);
    ASSERT_NE(z_back, nullptr);
    EXPECT_NEAR(z_back->rise_arrival_.max_, 0.2, 1e-9);
    ASSERT_EQ(back.stats_.files_.size(), 1u);
    EXPECT_EQ(back.stats_.files_[0].entry_count_, 8u);
    ASSERT_EQ(back.stats_.clocks_.size(), 1u);

    // TMPartitionTiming / TMClockTable / TMSummary / TMChunkPlan
    const TMPartitionTiming& part = slice.partitions_.at(kPid);
    CMString part_blob;
    FLY_ENCODE(part, part_blob);
    TMPartitionTiming part_back;
    FLY_DECODE(part_blob, TMPartitionTiming, part_back);
    EXPECT_EQ(part_back.size(), part.size());
    EXPECT_EQ(part_back.part_id_, kPid);

    TMClockTable clocks;
    TMClockTable::Entry e;
    e.name_ = "clk";
    e.period_ = 1.0;
    clocks.clocks_.push_back(e);
    CMString clocks_blob;
    FLY_ENCODE(clocks, clocks_blob);
    TMClockTable clocks_back;
    FLY_DECODE(clocks_blob, TMClockTable, clocks_back);
    ASSERT_NE(clocks_back.find("clk"), nullptr);
    EXPECT_DOUBLE_EQ(clocks_back.find("clk")->period_, 1.0);

    TMSummary summary;
    summary.total_hit_count_ = 5;
    summary.files_.emplace_back();
    summary.files_[0].source_file_ = "x.twf";
    CMString summary_blob;
    FLY_ENCODE(summary, summary_blob);
    TMSummary summary_back;
    FLY_DECODE(summary_blob, TMSummary, summary_back);
    EXPECT_EQ(summary_back.total_hit_count_, 5u);
    ASSERT_EQ(summary_back.files_.size(), 1u);
    EXPECT_EQ(summary_back.files_[0].source_file_, "x.twf");

    TMChunkPlan plan;
    TMFileChunkPlan fp;
    fp.file_name_ = "x.twf";
    fp.binding_kind_ = 1;
    fp.block_inst_ = "c1";
    fp.chunk_starts_ = {3, 100};
    fp.chunk_ends_ = {100, 300};
    plan.files_.push_back(fp);
    CMString plan_blob;
    FLY_ENCODE(plan, plan_blob);
    TMChunkPlan plan_back;
    FLY_DECODE(plan_blob, TMChunkPlan, plan_back);
    ASSERT_EQ(plan_back.files_.size(), 1u);
    EXPECT_EQ(plan_back.files_[0].binding_kind_, 1);
    EXPECT_EQ(plan_back.files_[0].block_inst_, "c1");
    ASSERT_EQ(plan_back.files_[0].chunk_starts_.size(), 2u);
    EXPECT_EQ(plan_back.files_[0].chunk_ends_[1], 300u);
}


}  // namespace
