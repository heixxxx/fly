// spef db 换算/路由/合并层单测（spef-db-plan §9.3 换算矩阵 + 任务规格
// 测试设计 14 场景全条目；夹具用合成 design db 数据——同 wf_partition_
// test 先例；输入用 sp_parse_spef_text 解析手写 SPEF 文本，走「解析 →
// 换算 → 合并」真实链路）：
//   1. 路由与复制（PATH 形态，两个分区：连接位实例跨区 → 每区各一条）；
//   2. 跳过与计数（网名未命中 / pg 网 / 未布线 / 无驱动接收位 / 未放置
//      实例逐项计数断言）；
//   3. 查找容错（SPEF 转义名 n\_a → 去转义回退命中）；
//   4. 层次分隔符归一（*DIVIDER . → 设计库 '/'；被转义的点不当分隔符）；
//   5. 块绑定三形态（BLOCK_INST 局部名偏移 / BLOCK_CELL 定义级建库期
//      复制 / strip_prefix 段级剥离与剥后余空）；
//   6. 合并——跨作用域相加（三值相加 + 来源位图）；7. 同作用域保留首
//      份（裁定 8）；8. 合并顺序确定性（相加与掩码或交换律——打乱指针
//      序结果逐字段相等）；9. 缩减形态（R_NET 收录 / R_NET + D_NET 合并）；
//   10. 行首值与明细合计不一致观测（容差内外两态）；
//   11. 覆盖率统计四项（裁定 10）；12. 空结果放行 + 无 NETS 对象；
//   13. 汇总（逐文件按名归并 + 全局计数）；14. 序列化 round-trip；
//   15. 电气网分组合并（并查集注入：组内各段负载相加、组内未被描述的
//       成员网也按合计补条目〔条目跟随本区 NETS 集合〕/ 缩减标记与运
//       算〔含补条目路径〕/ 单成员组与现状一致 / 覆盖率按电气网口径）；
//       多成员电气网数 = 设计级参数（汇总任务原样落字段）。
//
// 合成场景 id 备忘：
//   FlatEnv（顶层块 design_name；实例 local 从 1 起 = global；网 local
//   从 1 起、root net_start = 0 → global = local，注册序 = id）；
//   BlkEnv（cells: WCELL(0) BLK_S(1) sp_blk_top(2)；树 DFS 前序：
//   #0 sp_blk_top{u_s1,#1(S#1)} {u_s2,#2(S#2)}；inst 区间 top [0,3)
//   S#1 [3,5) S#2 [5,7)——u_i1=4/6；net 区间 top [0,1) S#1 [1,4)
//   S#2 [4,7)——n1=2/5、n2=3/6）。
#include <emir/spef/cpp/sp_parser.h>
#include <emir/spef/cpp/sp_partition.h>

#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_types.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fly;

constexpr CMPartitionId kPid0{0};
constexpr CMPartitionId kPid1{1};

// —— 合成 design 环境①：扁平顶层块（PATH 形态与计数/合并/覆盖率载体）——

struct FlatEnv {
    std::shared_ptr<DSDesign> design;
    std::shared_ptr<const DSBlockNames> top_names;
    CMPinId pin_a{0};    // WCELL A（INPUT）
    CMPinId pin_zn{1};   // WCELL ZN（OUTPUT）
    CMUnorderedMap<CMString, uint64_t> net_ids;    // 网名 → global id
    CMUnorderedMap<CMString, uint64_t> inst_ids;   // 实例名 → global id

    FlatEnv(const CMString& design_name,
            const CMVector<CMString>& inst_names,
            const CMVector<CMString>& net_names) {
        auto d = std::make_shared<DSDesign>();
        // WCELL：A(INPUT) / ZN(OUTPUT)
        DSCell wcell;
        wcell.set_name("WCELL");
        wcell.set_lef_cell();
        wcell.add_pin(DSPin{});
        wcell.add_pin(DSPin{});
        d->add_cell(std::move(wcell));
        DSCell& wref = d->get_cell(CMCellId{0});
        wref.pins_[0].direction_ = DSPinDirection::INPUT;
        wref.pins_[0].pin_id_ = pin_a;
        wref.pins_[1].direction_ = DSPinDirection::OUTPUT;
        wref.pins_[1].pin_id_ = pin_zn;
        // 顶层块 cell（名 = design_name）
        DSCell top_cell;
        top_cell.set_name(design_name);
        top_cell.set_block_cell();
        d->add_cell(std::move(top_cell));
        // 全局 pin 名字空间（注册序 = id）
        d->register_pin("A");
        d->register_pin("ZN");

        // 顶层块产物（实例 local 从 1 起、网 local 从 1 起）
        DSBlockBuildData top_block;
        top_block.init_placeholder(design_name, CMCellId{1});
        CMVector<std::pair<CMString, uint64_t>> inst_reg;
        for (const CMString& n : inst_names) {
            DSInstance inst;
            inst.set_cell_id(CMCellId{0});
            const CMInstanceId local =
                top_block.add_instance(std::move(inst), n);
            inst_reg.emplace_back(n, local.value());
            inst_ids[n] = local.value();
        }
        CMVector<std::pair<CMString, uint64_t>> net_reg;
        for (const CMString& n : net_names) {
            const CMNetId local = top_block.register_net(n);
            net_reg.emplace_back(n, local.value());
            net_ids[n] = local.value();
        }
        auto names = std::make_shared<DSBlockNames>();
        names->block_name_ = design_name;
        for (const auto& item : inst_reg) {
            names->instance_names_->assign(item.first,
                                           CMInstanceId{item.second});
        }
        for (const auto& item : net_reg) {
            names->net_names_->assign(item.first, CMNetId{item.second});
        }
        top_names = std::move(names);

        // 层级树（单根块；S5b 网产物占位空对象）
        CMVector<DSBlockBuildData> blocks;
        blocks.push_back(std::move(top_block));
        CMVector<DSNetBuildData> net_data(blocks.size());
        CMVector<CMSharedPtr<const DSBlockBuildData>> block_shares;
        CMVector<CMSharedPtr<const DSNetBuildData>> net_shares;
        for (const auto& b : blocks) {
            block_shares.push_back(CMSharedPtr<const DSBlockBuildData>{
                CMSharedPtr<DSBlockBuildData>(), &b});
        }
        for (const auto& n : net_data) {
            net_shares.push_back(CMSharedPtr<const DSNetBuildData>{
                CMSharedPtr<DSNetBuildData>(), &n});
        }
        d->set_hier_tree(ds_build_hier_tree(block_shares, net_shares, *d));
        design = std::move(d);
    }
};

// —— 合成 design 环境②：两层块树、同一块定义（BLK_S）实例化两次（块
//    绑定三形态载体；id 备忘见文件头）———————————————————————————————

struct BlkEnv {
    std::shared_ptr<DSDesign> design;
    std::shared_ptr<const DSBlockNames> top_names;
    std::shared_ptr<const DSBlockNames> s_names;
    CMPinId pin_a{0};
    CMPinId pin_zn{1};
    // 实例 global id
    static constexpr uint64_t kUS1 = 1;     // top.u_s1（BLK_S#1）
    static constexpr uint64_t kUS2 = 2;     // top.u_s2（BLK_S#2）
    static constexpr uint64_t kUI1S1 = 4;   // S#1 内 u_i1
    static constexpr uint64_t kUI1S2 = 6;   // S#2 内 u_i1
    // 网 global id
    static constexpr uint64_t kNetN1S1 = 2;
    static constexpr uint64_t kNetN2S1 = 3;
    static constexpr uint64_t kNetN1S2 = 5;
    static constexpr uint64_t kNetN2S2 = 6;

    BlkEnv() {
        auto d = std::make_shared<DSDesign>();
        // WCELL：A(INPUT) / ZN(OUTPUT)
        DSCell inv;
        inv.set_name("WCELL");
        inv.set_lef_cell();
        inv.add_pin(DSPin{});
        inv.add_pin(DSPin{});
        d->add_cell(std::move(inv));
        DSCell& iref = d->get_cell(CMCellId{0});
        iref.pins_[0].direction_ = DSPinDirection::INPUT;
        iref.pins_[0].pin_id_ = pin_a;
        iref.pins_[1].direction_ = DSPinDirection::OUTPUT;
        iref.pins_[1].pin_id_ = pin_zn;
        // block cells：BLK_S(1) / sp_blk_top(2)
        DSCell s_cell;
        s_cell.set_name("BLK_S");
        s_cell.set_block_cell();
        d->add_cell(std::move(s_cell));
        DSCell top_cell;
        top_cell.set_name("sp_blk_top");
        top_cell.set_block_cell();
        d->add_cell(std::move(top_cell));
        d->register_pin("A");
        d->register_pin("ZN");

        // per-DEF 产物（S → top；实例 local 从 1 起、网 local 从 1 起）
        auto make_block =
            [](const char* name, CMCellId cell,
               const CMVector<std::pair<const char*, CMCellId>>& insts,
               const CMVector<const char*>& net_names) {
                auto b = std::make_shared<DSBlockBuildData>();
                b->init_placeholder(name, cell);
                for (const auto& inst : insts) {
                    DSInstance obj;
                    obj.set_cell_id(inst.second);
                    b->add_instance(std::move(obj), inst.first);
                }
                for (const char* n : net_names) {
                    (void)b->register_net(n);
                }
                return b;
            };
        const auto s_block = make_block("BLK_S", CMCellId{1},
                                        {{"u_i1", CMCellId{0}}},
                                        {"n1", "n2"});
        const auto top_block =
            make_block("sp_blk_top", CMCellId{2},
                       {{"u_s1", CMCellId{1}}, {"u_s2", CMCellId{1}}}, {});

        // 块 local 名空间伴生对象（与 per-DEF 产物同名对齐）
        auto names = [](const char* block,
                        const CMVector<std::pair<const char*, uint64_t>>&
                            insts,
                        const CMVector<std::pair<const char*, uint64_t>>&
                            nets) {
            auto n = std::make_shared<DSBlockNames>();
            n->block_name_ = block;
            for (const auto& item : insts) {
                n->instance_names_->assign(item.first,
                                           CMInstanceId{item.second});
            }
            for (const auto& item : nets) {
                n->net_names_->assign(item.first, CMNetId{item.second});
            }
            return n;
        };
        top_names = names("sp_blk_top", {{"u_s1", 1}, {"u_s2", 2}}, {});
        s_names = names("BLK_S", {{"u_i1", 1}}, {{"n1", 1}, {"n2", 2}});

        // 层级树
        CMVector<DSBlockBuildData> blocks;
        blocks.push_back(*s_block);
        blocks.push_back(*top_block);
        CMVector<DSNetBuildData> net_data(blocks.size());
        CMVector<CMSharedPtr<const DSBlockBuildData>> block_shares;
        CMVector<CMSharedPtr<const DSNetBuildData>> net_shares;
        for (const auto& b : blocks) {
            block_shares.push_back(CMSharedPtr<const DSBlockBuildData>{
                CMSharedPtr<DSBlockBuildData>(), &b});
        }
        for (const auto& n : net_data) {
            net_shares.push_back(CMSharedPtr<const DSNetBuildData>{
                CMSharedPtr<DSNetBuildData>(), &n});
        }
        d->set_hier_tree(ds_build_hier_tree(block_shares, net_shares, *d));
        design = std::move(d);
    }
};

// —— 上下文组装（段表默认全部分区 0；测试持有段表指针改分区分配）——

struct FlatCtx {
    std::shared_ptr<SPDesignContext> ctx;
    std::shared_ptr<DSIdPartitionSegment> seg;
};

FlatCtx make_flat_ctx(const FlatEnv& env,
                      const CMVector<uint64_t>& pg_net_ids = {}) {
    auto ctx = std::make_shared<SPDesignContext>();
    ctx->set_design(env.design);
    ctx->add_block_names(env.top_names);
    auto index = std::make_shared<DSIdPartitionIndex>();
    index->id_starts_ = {0};
    ctx->set_inst_id_map(index);
    auto seg = std::make_shared<DSIdPartitionSegment>();
    seg->id_start_ = 0;
    seg->pids_.assign(32, CMPartitionId{0});
    ctx->add_inst_segment(seg);
    auto pg = std::make_shared<DSPgNetSet>();
    for (const uint64_t id : pg_net_ids) {
        pg->power_.insert(CMNetId{id});
    }
    ctx->set_pg_nets(pg);
    return {std::move(ctx), std::move(seg)};
}

// —— 分区 NETS 搭建 ————————————————————————————————————————————————

// NETS 连接位描述（inst = 实例 global id；port 位条目 inst 0 = 块自身）。
// pin 按位型推导：driver → ZN、receiver / port → A
struct TestConn {
    uint64_t net;
    uint64_t inst;
    bool driver;
    bool receiver;
    bool port = false;
};

DSPartitionNets build_nets(CMPartitionId pid,
                           const CMVector<TestConn>& conns,
                           const FlatEnv& env) {
    DSPartitionNets nets;
    nets.part_id_ = pid;
    for (const TestConn& c : conns) {
        const CMNetId key{c.net};
        auto it = nets.nets_.find(key);
        if (it == nets.nets_.end()) {
            auto n = std::make_shared<DSNet>();
            n->net_id_ = key;
            it = nets.nets_.emplace(key, std::move(n)).first;
        }
        DSNetConnEntry e;
        e.inst_id_ = CMInstanceId{c.inst};
        e.pin_id_ = c.driver ? env.pin_zn : env.pin_a;
        if (c.driver) {
            e.set_driver();
        }
        if (c.receiver) {
            e.set_receiver();
        }
        if (c.port) {
            e.set_port();
        }
        it->second->connections_.add(std::move(e));
    }
    // 2026-11-24 连接表唯一性裁定：下游读封存后的表——构造后封存
    for (auto& [nid, n] : nets.nets_) {
        n->connections_.seal();
    }
    return nets;
}

DSPartitionNets build_nets(CMPartitionId pid,
                           const CMVector<TestConn>& conns,
                           const BlkEnv& env) {
    DSPartitionNets nets;
    nets.part_id_ = pid;
    for (const TestConn& c : conns) {
        const CMNetId key{c.net};
        auto it = nets.nets_.find(key);
        if (it == nets.nets_.end()) {
            auto n = std::make_shared<DSNet>();
            n->net_id_ = key;
            it = nets.nets_.emplace(key, std::move(n)).first;
        }
        DSNetConnEntry e;
        e.inst_id_ = CMInstanceId{c.inst};
        e.pin_id_ = c.driver ? env.pin_zn : env.pin_a;
        if (c.driver) {
            e.set_driver();
        }
        if (c.receiver) {
            e.set_receiver();
        }
        if (c.port) {
            e.set_port();
        }
        it->second->connections_.add(std::move(e));
    }
    // 2026-11-24 连接表唯一性裁定：下游读封存后的表——构造后封存
    for (auto& [nid, n] : nets.nets_) {
        n->connections_.seal();
    }
    return nets;
}

void add_nets(SPDesignContext& ctx, CMPartitionId pid, DSPartitionNets nets) {
    nets.part_id_ = pid;
    ctx.add_partition_nets(std::make_shared<DSPartitionNets>(std::move(nets)));
}

// 实例连接条目（inst 全局 id, net 全局 id）列表 → 本分区实例连接对象
//（第 H 节第 4 条的覆盖率材料；键 0 = root 设计级端口连接）
DSPartInstConnections build_inst_conns(
    const CMVector<std::pair<uint64_t, uint64_t>>& inst_nets) {
    DSPartInstConnections conns;
    for (const auto& [inst, net] : inst_nets) {
        DSPartConnection c;
        c.inst_id_ = CMInstanceId{inst};
        c.net_global_id_ = CMNetId{net};
        conns.items_[CMInstanceId{inst}].add(std::move(c));
    }
    for (auto& [iid, list] : conns.items_) {
        list.seal();
    }
    return conns;
}

void add_inst_conns(SPDesignContext& ctx, CMPartitionId pid,
                    DSPartInstConnections conns) {
    ctx.add_partition_inst_connections(
        pid, std::make_shared<DSPartInstConnections>(std::move(conns)));
}

// 并查集替身构造（等价类组表 → 两层树 + root 规范形态——同
// ds_build_net_union 产物：root_of_ 全员指向组首（root）、members_of_
// 升序含 root 自身）
std::shared_ptr<DSNetUnion> make_union(
    const CMVector<CMVector<uint64_t>>& groups) {
    auto u = std::make_shared<DSNetUnion>();
    for (const CMVector<uint64_t>& group : groups) {
        const CMNetId root{group[0]};
        auto members = std::make_shared<CMVector<CMNetId>>();
        for (const uint64_t id : group) {
            members->push_back(CMNetId{id});
            u->root_of_[CMNetId{id}] = root;
        }
        u->members_of_[root] = std::move(members);
    }
    return u;
}

FlatCtx make_blk_ctx(const BlkEnv& env) {
    auto ctx = std::make_shared<SPDesignContext>();
    ctx->set_design(env.design);
    ctx->add_block_names(env.top_names);
    ctx->add_block_names(env.s_names);
    auto index = std::make_shared<DSIdPartitionIndex>();
    index->id_starts_ = {0};
    ctx->set_inst_id_map(index);
    auto seg = std::make_shared<DSIdPartitionSegment>();
    seg->id_start_ = 0;
    seg->pids_.assign(16, CMPartitionId{0});
    ctx->add_inst_segment(seg);
    ctx->set_pg_nets(std::make_shared<DSPgNetSet>());
    add_nets(*ctx, kPid0,
             build_nets(kPid0,
                        {{BlkEnv::kNetN1S1, BlkEnv::kUI1S1, true, false},
                         {BlkEnv::kNetN2S1, BlkEnv::kUI1S1, false, true},
                         {BlkEnv::kNetN1S2, BlkEnv::kUI1S2, true, false},
                         {BlkEnv::kNetN2S2, BlkEnv::kUI1S2, false, true}},
                        env));
    return {std::move(ctx), std::move(seg)};
}

// —— 测试辅助 ————————————————————————————————————————————————

// 最小合法 SPEF 文本（单位 1 FF / 1 OHM——数值直读 fF；分隔符可变）
CMString spef_text(const CMString& body, char divider = '/') {
    CMString out = "*SPEF \"IEEE 1481-1998\"\n*DIVIDER ";
    out += divider;
    out += "\n*DELIMITER :\n*C_UNIT 1 FF\n*R_UNIT 1 OHM\n";
    out += body;
    return out;
}

// 解析 + 换算一步（真实链路：sp_parse_spef_text → sp_convert_chunk）
SPEntrySlice convert_text(const SPDesignContext& ctx, const CMString& text,
                          uint32_t file_index = 0, uint32_t scope_index = 0,
                          const SPFileBinding& binding = {},
                          const CMString& source = "mem.spef") {
    SPFileHeader header;
    const SPChunkResult parsed =
        sp_parse_spef_text(text, header, 0, false, source);
    return sp_convert_chunk(ctx, header, parsed, file_index, scope_index,
                            binding, source);
}

const SPNetEntry* find_entry(const SPEntrySlice& slice, CMPartitionId pid,
                             CMNetId net_id) {
    const auto it = slice.partitions_.find(pid);
    if (it == slice.partitions_.end()) {
        return nullptr;
    }
    for (const SPNetEntry& e : it->second.entries_) {
        if (e.net_id_ == net_id) {
            return &e;
        }
    }
    return nullptr;
}

// 逐网负载条目逐字段相等（合并顺序确定性断言用）
void expect_same_load(const SPNetLoad& a, const SPNetLoad& b) {
    EXPECT_EQ(a.net_id_, b.net_id_);
    EXPECT_DOUBLE_EQ(a.total_cap_reported_ff_, b.total_cap_reported_ff_);
    EXPECT_DOUBLE_EQ(a.ground_cap_ff_, b.ground_cap_ff_);
    EXPECT_DOUBLE_EQ(a.coupling_cap_ff_, b.coupling_cap_ff_);
    EXPECT_EQ(a.source_mask_, b.source_mask_);
    EXPECT_EQ(a.flags_, b.flags_);
}

// ── 1. 路由与复制（PATH 形态，两个分区）─────────────────────────────

TEST(SpPartitionTest, RouteAndCopy) {
    // 网名 → id：n_a=1 n_b=2 n_c=3；实例：u_d_a=1 u_r_a=2 u_d_b=3 u_r_b=4
    // u_d_c=5 u_r_c=6
    const FlatEnv env("top", {"u_d_a", "u_r_a", "u_d_b", "u_r_b", "u_d_c",
                              "u_r_c"},
                      {"n_a", "n_b", "n_c"});
    const FlatCtx fc = make_flat_ctx(env);
    // 分区分配：u_r_a(2) → 分区 1；u_d_c(5)/u_r_c(6) → 分区 1；其余分区 0
    fc.seg->pids_[2] = kPid1;
    fc.seg->pids_[5] = kPid1;
    fc.seg->pids_[6] = kPid1;
    // 两分区各一份全量 NETS 副本（任一副本保有该网完整连接表）
    const CMVector<TestConn> conns = {
        {1, 1, true, false},    // n_a ← u_d_a
        {1, 2, false, true},    // n_a → u_r_a
        {2, 3, true, false},    // n_b ← u_d_b
        {2, 4, false, true},    // n_b → u_r_b
        {3, 5, true, false},    // n_c ← u_d_c
        {3, 6, false, true},    // n_c → u_r_c
    };
    add_nets(*fc.ctx, kPid0, build_nets(kPid0, conns, env));
    add_nets(*fc.ctx, kPid1, build_nets(kPid1, conns, env));

    const CMString text =
        spef_text("*D_NET n_a 10.0\n*END\n*D_NET n_b 20.0\n*END\n"
                  "*D_NET n_c 30.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);

    // 分区 0：n_a、n_b；分区 1：n_a、n_c（网跨区每区各一条）
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{1}), nullptr);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{2}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{3}), nullptr);
    EXPECT_NE(find_entry(slice, kPid1, CMNetId{1}), nullptr);
    EXPECT_NE(find_entry(slice, kPid1, CMNetId{3}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid1, CMNetId{2}), nullptr);
    // 值取自 SPNet（自报 10 / 20 / 30 fF）
    EXPECT_DOUBLE_EQ(
        find_entry(slice, kPid0, CMNetId{1})->total_cap_reported_ff_, 10.0);
    EXPECT_DOUBLE_EQ(
        find_entry(slice, kPid0, CMNetId{2})->total_cap_reported_ff_, 20.0);
    EXPECT_DOUBLE_EQ(
        find_entry(slice, kPid1, CMNetId{3})->total_cap_reported_ff_, 30.0);
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{1})->flags_, 0u);
    // 计数：三网全命中
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.hit_count_, 3u);
    EXPECT_EQ(st.net_name_miss_count_, 0u);
    EXPECT_EQ(st.unrouted_net_count_, 0u);
    // 条目按 net id 升序（确定性）
    const CMVector<SPNetEntry>& e0 = slice.partitions_.at(kPid0).entries_;
    ASSERT_EQ(e0.size(), 2u);
    EXPECT_TRUE(e0[0].net_id_ < e0[1].net_id_);
}

// ── 2. 跳过与计数（每项一个断言）────────────────────────────────────

// ── 2b. 端口条目不是实例（路由层排除——不计未放置实例）──────────────

TEST(SpPartitionTest, PortEntriesNotRoutedAsInstances) {
    // 端口条目的实例编号是块自身占位值（根块为 0），不是真实实例——
    // 路由跳过端口条目、不计未放置实例；排除端口后没有任何连接位的网
    //（只连端口的 n_p）按「未布线 / 无可挂连接位」计（既有口径）。
    // n_a=1 连 u_x=1（receiver）+ 端口；n_p=2 只连端口。
    const FlatEnv env("top", {"u_x"}, {"n_a", "n_p"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0, {{1, 1, false, true},
                                {1, 0, false, false, true},
                                {2, 0, false, false, true}},
                        env));
    const CMString text = spef_text(
        "*D_NET n_a 5.0\n*END\n"
        "*D_NET n_p 3.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);
    // n_a 条目落在真实连接位实例所在分区（端口条目不产生分区条目）
    ASSERT_TRUE(find_entry(slice, kPid0, CMNetId{1}) != nullptr);
    // 只连端口的网：无实例连接位 → 未布线计数、无条目
    ASSERT_TRUE(find_entry(slice, kPid0, CMNetId{2}) == nullptr);
    ASSERT_EQ(slice.stats_.files_.size(), 1u);
    const SPFileStats& fs = slice.stats_.files_[0];
    EXPECT_EQ(fs.unrouted_net_count_, 1u);
    EXPECT_EQ(fs.unplaced_instance_count_, 0u);   // 端口条目不计未放置
    EXPECT_EQ(fs.hit_count_, 1u);
}

TEST(SpPartitionTest, SkipCounters) {
    // 实例：u_d_a=1 u_r_a=2 u_d_b=3 u_r_b=4 u_d_c=5 u_r_c=6 u_late=7；
    // 网：n_a=1 n_b=2 n_c=3 n_unrouted=4 n_portonly=5 n_late=6 vdd=7
    const FlatEnv env("top",
                      {"u_d_a", "u_r_a", "u_d_b", "u_r_b", "u_d_c", "u_r_c",
                       "u_late"},
                      {"n_a", "n_b", "n_c", "n_unrouted", "n_portonly",
                       "n_late", "vdd"});
    const FlatCtx fc = make_flat_ctx(env, {env.net_ids.at("vdd")});
    // 段表只覆盖 id 0..6：u_late(7) 落在 id 映射空洞（未放置）
    fc.seg->pids_.resize(7);
    fc.seg->pids_[2] = kPid1;
    fc.seg->pids_[5] = kPid1;
    fc.seg->pids_[6] = kPid1;
    const CMVector<TestConn> conns = {
        {1, 1, true, false},
        {1, 2, false, true},
        {2, 3, true, false},
        {2, 4, false, true},
        {3, 5, true, false},
        {3, 6, false, true},
        {5, 0, false, false, true},   // n_portonly：NETS 有但仅 port 位
        {6, 7, false, true},          // n_late：唯一连接位未放置
    };
    add_nets(*fc.ctx, kPid0, build_nets(kPid0, conns, env));
    add_nets(*fc.ctx, kPid1, build_nets(kPid1, conns, env));

    const CMString text = spef_text(
        "*D_NET n_a 10.0\n*END\n*D_NET n_b 20.0\n*END\n*D_NET n_c 30.0\n"
        "*END\n*D_NET n_ghost 40.0\n*END\n*D_NET vdd 50.0\n*END\n"
        "*D_NET n_unrouted 60.0\n*END\n*D_NET n_portonly 70.0\n*END\n"
        "*D_NET n_late 80.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.net_name_miss_count_, 1u);     // n_ghost：设计里没有
    EXPECT_EQ(st.pg_net_skip_count_, 1u);       // vdd：pg 集命中
    EXPECT_EQ(st.unrouted_net_count_, 3u);      // n_unrouted（NETS 无此网）
                                                // + n_portonly（无驱动接收位）
                                                // + n_late（唯一连接位未放置）
    EXPECT_EQ(st.unplaced_instance_count_, 1u); // u_late
    EXPECT_EQ(st.hit_count_, 3u);               // n_a / n_b / n_c
    // n_late 不产条目
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{6}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid1, CMNetId{6}), nullptr);
}

// ── 3. 查找容错（转义名回退）────────────────────────────────────────

TEST(SpPartitionTest, EscapedNameFallback) {
    const FlatEnv env("top", {"u_g_a"}, {"n_a"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0, {{1, 1, true, false}}, env));

    // 设计网名 n_a；SPEF 写成 n\_a（反斜杠转义——商业工具不合规转义形
    // 态）：原名查未命中 → 去掉全部反斜杠回退查 → 命中
    const CMString text = spef_text("*D_NET n\\_a 10.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.net_name_miss_count_, 0u);
    EXPECT_EQ(st.hit_count_, 1u);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{1}), nullptr);
}

// ── 4. 层次分隔符归一 ───────────────────────────────────────────────

TEST(SpPartitionTest, DividerNormalization) {
    // 文件头 *DIVIDER .，SPEF 网名 u_s1.n1 → 归一 u_s1/n1 → 命中设计里
    // 的全局网（S#1 实例内 n1 = 全局 2）
    BlkEnv env;
    const FlatCtx fc = make_blk_ctx(env);
    const CMString text = spef_text("*D_NET u_s1.n1 10.0\n*END\n", '.');
    const SPEntrySlice slice = convert_text(*fc.ctx, text);
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.hit_count_, 1u);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN1S1}), nullptr);

    // 名字里被转义的点不被当作分隔符：设计网名 odd.name（名字真含点），
    // SPEF 写 odd\.name → 归一保留转义对（点不当分隔符）→ 原名查未命中
    // → 去转义 odd.name → 命中
    const FlatEnv env2("top", {"u_g_odd"}, {"odd.name"});
    const FlatCtx fc2 = make_flat_ctx(env2);
    add_nets(*fc2.ctx, kPid0,
             build_nets(kPid0, {{1, 1, true, false}}, env2));
    const CMString text2 = spef_text("*D_NET odd\\.name 5.0\n*END\n", '.');
    const SPEntrySlice slice2 = convert_text(*fc2.ctx, text2);
    const SPFileStats& st2 = slice2.stats_.files_.at(0);
    EXPECT_EQ(st2.hit_count_, 1u);
    EXPECT_EQ(st2.net_name_miss_count_, 0u);
    EXPECT_NE(find_entry(slice2, kPid0, CMNetId{1}), nullptr);
}

// ── 5. 块绑定三形态 ─────────────────────────────────────────────────

TEST(SpPartitionTest, BlockInstBinding) {
    BlkEnv env;
    const FlatCtx fc = make_blk_ctx(env);
    SPFileBinding binding;
    binding.kind = SPFileBindingKind::BLOCK_INST;
    binding.block_inst = "u_s1";
    // 局部名在指定块实例（S#1）内解析成对应全局网 id；另一实例化（S#2）
    // 的同名网不受影响
    const CMString text =
        spef_text("*D_NET n1 10.0\n*END\n*D_NET n2 20.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text, 0, 0, binding);
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.hit_count_, 2u);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN1S1}), nullptr);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN2S1}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN1S2}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN2S2}), nullptr);
}

TEST(SpPartitionTest, BlockCellBinding) {
    BlkEnv env;
    const FlatCtx fc = make_blk_ctx(env);
    SPFileBinding binding;
    binding.kind = SPFileBindingKind::BLOCK_CELL;
    binding.block_cell = "BLK_S";
    // 局部名对该块定义的两个实例化各解析一次 → 两个不同的全局网 id 都
    // 有条目（建库期复制）；hit 按 SPEF 网段计 1
    const CMString text = spef_text("*D_NET n1 10.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text, 0, 0, binding);
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.hit_count_, 1u);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN1S1}), nullptr);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN1S2}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN2S1}), nullptr);
    EXPECT_EQ(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN2S2}), nullptr);
}

TEST(SpPartitionTest, StripPrefix) {
    BlkEnv env;
    const FlatCtx fc = make_blk_ctx(env);
    SPFileBinding binding;
    binding.kind = SPFileBindingKind::BLOCK_INST;
    binding.block_inst = "u_s1";
    binding.strip_prefix = "pre";
    // pre.n1 → 剥后 n1 命中；x.n1 前缀不匹配 → strip_miss；n1 段数不足
    //（剥后余空）→ strip_miss
    const CMString text =
        spef_text("*D_NET pre.n1 10.0\n*END\n*D_NET x.n1 20.0\n*END\n"
                  "*D_NET n1 30.0\n*END\n",
                  '.');
    const SPEntrySlice slice = convert_text(*fc.ctx, text, 0, 0, binding);
    const SPFileStats& st = slice.stats_.files_.at(0);
    EXPECT_EQ(st.strip_miss_count_, 2u);
    EXPECT_EQ(st.hit_count_, 1u);
    EXPECT_NE(find_entry(slice, kPid0, CMNetId{BlkEnv::kNetN1S1}), nullptr);
}

// ── 6. 合并——跨作用域相加 ───────────────────────────────────────────

TEST(SpPartitionTest, MergeCrossScopeSum) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1"}, {"n1"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false}, {1, 2, false, true}}, env));
    // 文件 1（scope 0）：自报 10 / 对地 7 / 耦合 3；文件 2（scope 1）：
    // 自报 5 / 对地 4 / 耦合 1
    const CMString t1 = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 7.0\n"
                                  "2 n1:2 n2:1 3.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n1 5.0\n*CAP\n1 n1:1 4.0\n"
                                  "2 n1:2 n2:1 1.0\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 0, 0);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 1, 1);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 1u);
    const SPNetLoad& e = merged.items_[0];
    EXPECT_EQ(e.net_id_, CMNetId{1});
    EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 15.0);
    EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 11.0);
    EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 4.0);
    EXPECT_EQ(e.source_mask_, uint64_t{0b11});   // 两文件贡献位都置位
    EXPECT_EQ(e.flags_, 0u);
    EXPECT_EQ(st.cross_scope_sum_count_, 1u);
    EXPECT_EQ(st.same_scope_overlap_count_, 0u);
    EXPECT_EQ(st.cap_mismatch_count_, 0u);   // 行首 15 = 明细 11 + 4
}

// ── 7. 合并——同作用域保留首份（裁定 8）─────────────────────────────

TEST(SpPartitionTest, MergeSameScopeKeepFirst) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1"}, {"n1"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false}, {1, 2, false, true}}, env));
    // 两个文件作用域相同（都 0）：同一网被同作用域的两个文件描述——保
    // 留按文件序最先出现的（10），99 丢弃
    const CMString t1 = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 6.0\n"
                                  "2 n1:2 n2:1 4.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n1 99.0\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 0, 0);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 1, 0);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 1u);
    const SPNetLoad& e = merged.items_[0];
    EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 10.0);
    EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 6.0);
    EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 4.0);
    EXPECT_EQ(e.source_mask_, uint64_t{0b1});   // 只有首位（保留首份）
    EXPECT_EQ(st.same_scope_overlap_count_, 1u);
    EXPECT_EQ(st.cross_scope_sum_count_, 0u);
}

// 同作用域重合检测对任意到达序成立（评审回归）：到达序作用域
// [1, 0, 0]——首见作用域是 1，后续两份同作用域 0。旧实现只与首见作用
// 域比较，后两份被误相加（得 31、重合计数 0）；修复后按「已贡献作用
// 域集」检测：第二份相加、第三份同作用域重合丢弃（得 11、重合计数 1）
TEST(SpPartitionTest, MergeSameScopeKeepFirstAnyArrivalOrder) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1"}, {"n1"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false}, {1, 2, false, true}}, env));
    const CMString t0 = spef_text("*D_NET n1 1.0\n*END\n");
    const CMString t1 = spef_text("*D_NET n1 10.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n1 20.0\n*END\n");
    const SPEntrySlice s0 = convert_text(*fc.ctx, t0, 0, 1);
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 1, 0);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 2, 0);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&s0, &s1, &s2}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 1u);
    const SPNetLoad& e = merged.items_[0];
    EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 11.0);   // 1.0 + 10.0（20.0 丢弃）
    EXPECT_EQ(e.source_mask_, uint64_t{0b011});   // f2 重合丢弃、位不置位
    EXPECT_EQ(st.same_scope_overlap_count_, 1u);
    EXPECT_EQ(st.cross_scope_sum_count_, 1u);
}

// ── 8. 合并——不同作用域贡献的交换性 ────────────────────────────────

TEST(SpPartitionTest, MergeOrderIndependent) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1", "u_d_n2", "u_r_n2"},
                      {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false},
                         {1, 2, false, true},
                         {2, 3, true, false},
                         {2, 4, false, true}},
                        env));
    // 三文件：f0（scope 0）n1+n2；f1（scope 1）n1；f2（scope 1）n2——
    // 无同作用域重合，不同作用域贡献的相加与掩码或在此组合下交换。合
    // 并数值的确定性由调用方固定的「文件序 × 块序」到达序保证（浮点加
    // 法非严格结合律，实现不依赖顺序无关性）
    const CMString t0 =
        spef_text("*D_NET n1 10.0\n*END\n*D_NET n2 20.0\n*END\n");
    const CMString t1 = spef_text("*D_NET n1 5.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n2 1.0\n*END\n");
    const SPEntrySlice s0 = convert_text(*fc.ctx, t0, 0, 0);
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 1, 1);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 2, 1);

    SPMergeStats st_a;
    const SPPartitionNetLoad a =
        sp_merge_partition(*fc.ctx, {&s0, &s1, &s2}, kPid0, st_a);
    SPMergeStats st_b;
    const SPPartitionNetLoad b =
        sp_merge_partition(*fc.ctx, {&s2, &s0, &s1}, kPid0, st_b);
    ASSERT_EQ(a.items_.size(), 2u);
    ASSERT_EQ(b.items_.size(), a.items_.size());
    for (size_t i = 0; i < a.items_.size(); ++i) {
        expect_same_load(a.items_[i], b.items_[i]);
    }
    EXPECT_EQ(st_a.cross_scope_sum_count_, st_b.cross_scope_sum_count_);
    EXPECT_EQ(st_a.same_scope_overlap_count_, st_b.same_scope_overlap_count_);
    // 合并值本身钉死（n1=15/0b11、n2=21/0b101）
    EXPECT_DOUBLE_EQ(a.items_[0].total_cap_reported_ff_, 15.0);
    EXPECT_EQ(a.items_[0].source_mask_, uint64_t{0b011});
    EXPECT_DOUBLE_EQ(a.items_[1].total_cap_reported_ff_, 21.0);
    EXPECT_EQ(a.items_[1].source_mask_, uint64_t{0b101});
}

// ── 9. 缩减形态（R_NET）─────────────────────────────────────────────

TEST(SpPartitionTest, ReducedFormMerge) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1"}, {"n1"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false}, {1, 2, false, true}}, env));

    // 9a：一个网只来自 R_NET → 置缩减形态、两个明细合计为 0、不参与一
    // 致性检查
    const CMString rnet = spef_text(
        "*R_NET n1 10.0\n*DRV u_d_n1:ZN\n*CELL DCELL\n"
        "*C2_R1_C1 5.0 3.0 2.0\n*LOADS\n*RC u_r_n1:A 0.5\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, rnet, 0, 0);
    SPMergeStats st1;
    const SPPartitionNetLoad m1 =
        sp_merge_partition(*fc.ctx, {&s1}, kPid0, st1);
    ASSERT_EQ(m1.items_.size(), 1u);
    EXPECT_NE(m1.items_[0].flags_ & kSpFlagReduced, 0);
    EXPECT_DOUBLE_EQ(m1.items_[0].total_cap_reported_ff_, 10.0);
    EXPECT_DOUBLE_EQ(m1.items_[0].ground_cap_ff_, 0.0);
    EXPECT_DOUBLE_EQ(m1.items_[0].coupling_cap_ff_, 0.0);
    EXPECT_EQ(st1.cap_mismatch_count_, 0u);   // 缩减形态不检查

    // 9b：不同作用域的 R_NET（scope 0）+ D_NET（scope 1）→ 不置缩减形
    // 态、两个合计取自那条 D_NET、自报值相加。行首 15 与明细 5 的差额
    // = R_NET 自报值——缩减段无明细的固有观测（只观测不改数值）
    const CMString dnet = spef_text("*D_NET n1 5.0\n*CAP\n1 n1:1 2.0\n"
                                    "2 n1:2 n2:1 3.0\n*END\n");
    const SPEntrySlice s2 = convert_text(*fc.ctx, dnet, 1, 1);
    SPMergeStats st2;
    const SPPartitionNetLoad m2 =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid0, st2);
    ASSERT_EQ(m2.items_.size(), 1u);
    EXPECT_EQ(m2.items_[0].flags_, 0u);
    EXPECT_DOUBLE_EQ(m2.items_[0].total_cap_reported_ff_, 15.0);
    EXPECT_DOUBLE_EQ(m2.items_[0].ground_cap_ff_, 2.0);
    EXPECT_DOUBLE_EQ(m2.items_[0].coupling_cap_ff_, 3.0);
    EXPECT_EQ(st2.cross_scope_sum_count_, 1u);
    EXPECT_EQ(st2.cap_mismatch_count_, 1u);
    EXPECT_NEAR(st2.cap_mismatch_max_rel_, 10.0 / 15.0, 1e-12);
}

// ── 10. 行首值与明细合计不一致（只观测）─────────────────────────────

TEST(SpPartitionTest, CapMismatchObservation) {
    // 网 n_bad=1（自报 9.0 / 明细 8.0——差额超容差）、n_ok=2（自报 10.0 /
    // 明细 9.9995——差额 5e-4 在容差内）、n_zero=3（全零——不计）
    const FlatEnv env("top", {"u_g_1", "u_g_2", "u_g_3"},
                      {"n_bad", "n_ok", "n_zero"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false},
                         {2, 2, true, false},
                         {3, 3, true, false}},
                        env));
    const CMString text = spef_text(
        "*D_NET n_bad 9.0\n*CAP\n1 n_bad:1 8.0\n*END\n"
        "*D_NET n_ok 10.0\n*CAP\n1 n_ok:1 9.9995\n*END\n"
        "*D_NET n_zero 0.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);
    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&slice}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 3u);
    EXPECT_EQ(st.cap_mismatch_count_, 1u);
    EXPECT_NEAR(st.cap_mismatch_max_rel_, 1.0 / 9.0, 1e-12);
}

// ── 11. 覆盖率统计（裁定 10）────────────────────────────────────────

TEST(SpPartitionTest, CoverageStats) {
    // 分区 0 的 NETS 五个信号网：n1=1（有 SPEF 非零）、n2=2（有 SPEF
    // 非零）、n3=3（有 SPEF 三值全零）、n4=4（没有 SPEF）、n5=5（没有
    // SPEF，唯一连接条目是纯端口位）；I1=1 连 n1/n4，I2=2 只连 n3，
    // I3=3 连 n1/n2。覆盖率口径（2026-11-24 评审裁定 + 第 H 节第 4 条
    // 分母修正）：材料 = 本区实例连接对象 + 已布线信号网 id 集合（全部
    // NETS 对象网 id 并集）。键 0（root 设计级端口连接）整键排除——
    // n5 只出现在键 0 条目里，不进网分母（smoke_small 的多分区期望
    // 12/11/23 逐实例核对同口径）；各项按「分区 × 网」统计后逐分区求
    // 和——本用例单分区即本区值
    const FlatEnv env("top", {"I1", "I2", "I3"},
                      {"n1", "n2", "n3", "n4", "n5"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, false, true},
                         {1, 3, false, true},
                         {2, 3, false, true},
                         {3, 2, false, true},
                         {4, 1, false, true},
                         {5, 0, false, false, true}},
                        env));
    add_inst_conns(*fc.ctx, kPid0,
                   build_inst_conns({{1, 1}, {1, 4}, {3, 1}, {3, 2},
                                     {2, 3}, {0, 5}}));
    const CMString text = spef_text(
        "*D_NET n1 10.0\n*CAP\n1 n1:1 6.0\n2 n1:2 n2:1 4.0\n*END\n"
        "*D_NET n2 5.0\n*CAP\n1 n2:1 5.0\n*END\n"
        "*D_NET n3 0.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&slice}, kPid0, st);
    EXPECT_EQ(st.routed_signal_net_count_, 4u);    // n1..n4（n5 键 0 排除）
    EXPECT_EQ(st.covered_net_count_, 3u);          // n1/n2/n3 有条目
    EXPECT_EQ(st.uncovered_net_count_, 1u);        // n4
    // 键 0 不进实例集合——无「连 n5 的幻影实例」
    EXPECT_EQ(st.uncovered_instance_count_, 1u);   // I1（连了未覆盖的 n4）
    EXPECT_EQ(st.zero_cap_instance_count_, 1u);    // I2（只连全零已覆盖 n3）
    ASSERT_EQ(merged.items_.size(), 3u);
}

// ── 11b. 覆盖率分母口径（第 H 节第 4 条）：网几何所在分区 ≠ 连接位实
//    例所在分区——分母材料是本区实例连接对象，不是本区 NETS ──────────

TEST(SpPartitionTest, CoverageDenominatorUsesInstConnections) {
    // 网 n_a=1 的布线几何全在分区 0（NETS 对象归分区 0），但它的连接位
    // 实例 u_x=1 在分区 1；网 n_p=2 只有纯端口位条目（键 0 = root 设计
    // 级端口连接）。断言：分区 0 的分母不含 n_a（本区无它的连接位实
    // 例）；分区 1 的分母含 n_a 且按条目有无计覆盖；键 0 整键排除——
    // n_p 不进网分母、也不产生幻影实例（root 非实体实例）
    const FlatEnv env("top", {"u_x"}, {"n_a", "n_p"});
    const FlatCtx fc = make_flat_ctx(env);
    // 实例 u_x（global id 1）primary 分区 = 1
    fc.seg->pids_.assign(32, kPid1);
    // 分区 0：NETS 含 n_a（几何载体）与 n_p（纯端口位条目）；无实例连
    // 接对象（本区没有连接位实例）
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0, {{1, 1, false, true},
                                {2, 0, false, false, true}},
                        env));
    // 分区 1：实例连接含 u_x→n_a 与键 0→n_p；无 NETS 对象（n_a 几何不
    // 在本区）
    add_inst_conns(*fc.ctx, kPid1,
                   build_inst_conns({{1, 1}, {0, 2}}));
    // SPEF 只覆盖 n_a：路由按连接位实例分区落分区 1
    const CMString text = spef_text("*D_NET n_a 5.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);
    ASSERT_TRUE(find_entry(slice, kPid1, CMNetId{1}) != nullptr);
    ASSERT_FALSE(find_entry(slice, kPid0, CMNetId{1}) != nullptr);

    // 分区 0：本区实例连接对象缺失 → 覆盖率各项记 0（n_a 不因几何在
    // 本区而进分母）
    SPMergeStats st0;
    sp_merge_partition(*fc.ctx, {&slice}, kPid0, st0);
    EXPECT_EQ(st0.routed_signal_net_count_, 0u);
    EXPECT_EQ(st0.covered_net_count_, 0u);
    EXPECT_EQ(st0.uncovered_net_count_, 0u);
    EXPECT_EQ(st0.uncovered_instance_count_, 0u);
    EXPECT_EQ(st0.zero_cap_instance_count_, 0u);

    // 分区 1：分母 = 本区实例连接里的已布线信号网 {n_a}（键 0 的 n_p
    // 整键排除）；n_a 有条目 = 覆盖、零未覆盖；实例集合只含 u_x——
    // u_x 所连已布线网 n_a 已覆盖且非零
    SPMergeStats st1;
    sp_merge_partition(*fc.ctx, {&slice}, kPid1, st1);
    EXPECT_EQ(st1.routed_signal_net_count_, 1u);
    EXPECT_EQ(st1.covered_net_count_, 1u);
    EXPECT_EQ(st1.uncovered_net_count_, 0u);
    EXPECT_EQ(st1.uncovered_instance_count_, 0u);
    EXPECT_EQ(st1.zero_cap_instance_count_, 0u);
}

// ── 12. 空结果 ──────────────────────────────────────────────────────

TEST(SpPartitionTest, EmptyMerge) {
    const FlatEnv env("top", {"u_g_a"}, {"n_a"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0, {{1, 1, true, false}}, env));
    add_inst_conns(*fc.ctx, kPid0, build_inst_conns({{1, 1}}));

    // 没有任何分片 → 空对象（part_id_ 置位、条目为空）；本区有 NETS 对
    // 象时覆盖率照常统计（全未覆盖）
    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {}, kPid0, st);
    EXPECT_EQ(merged.part_id_, kPid0);
    EXPECT_TRUE(merged.items_.empty());
    EXPECT_EQ(st.routed_signal_net_count_, 1u);
    EXPECT_EQ(st.covered_net_count_, 0u);
    EXPECT_EQ(st.uncovered_net_count_, 1u);
    EXPECT_EQ(st.uncovered_instance_count_, 1u);   // u_g_a 所连 n_a 未覆盖
    EXPECT_EQ(st.zero_cap_instance_count_, 0u);

    // 本区没有实例连接对象与 NETS 对象时覆盖率五项全 0（不报错——第 H
    // 节第 4 条口径：实例连接对象缺失同「NETS 对象缺失」）
    SPMergeStats st1;
    const SPPartitionNetLoad m1 = sp_merge_partition(*fc.ctx, {}, kPid1, st1);
    EXPECT_EQ(m1.part_id_, kPid1);
    EXPECT_TRUE(m1.items_.empty());
    EXPECT_EQ(st1.routed_signal_net_count_, 0u);
    EXPECT_EQ(st1.covered_net_count_, 0u);
    EXPECT_EQ(st1.uncovered_net_count_, 0u);
    EXPECT_EQ(st1.uncovered_instance_count_, 0u);
    EXPECT_EQ(st1.zero_cap_instance_count_, 0u);
}

// ── 13. 汇总 ────────────────────────────────────────────────────────

TEST(SpPartitionTest, SummaryMerge) {
    // d1 = 同一文件 a.spef 的两个块分片；d2 = 文件 b.spef 单块分片
    SPFileStats a1;
    a1.source_file_ = "a.spef";
    a1.c_unit_ff_per_unit_ = 1000.0;
    a1.net_count_ = 3;
    a1.rnet_count_ = 1;
    a1.hit_count_ = 2;
    a1.multi_value_net_count_ = 1;
    a1.value_count_max_ = 3;
    SPFileStats a2 = a1;
    a2.net_count_ = 2;
    a2.rnet_count_ = 0;
    a2.hit_count_ = 2;
    a2.multi_value_net_count_ = 1;
    a2.value_count_max_ = 2;
    SPFileStats b;
    b.source_file_ = "b.spef";
    b.c_unit_ff_per_unit_ = 1.0;
    b.net_count_ = 5;
    b.hit_count_ = 4;
    b.value_count_max_ = 1;

    SPStatsDelta d1;
    d1.files_ = {a1, a2};
    SPStatsDelta d2;
    d2.files_ = {b};

    SPMergeStats m1;
    m1.same_scope_overlap_count_ = 2;
    m1.cross_scope_sum_count_ = 3;
    m1.cap_mismatch_count_ = 1;
    m1.cap_mismatch_max_rel_ = 0.25;
    m1.routed_signal_net_count_ = 10;
    m1.covered_net_count_ = 8;
    m1.uncovered_net_count_ = 2;
    m1.uncovered_instance_count_ = 4;
    m1.zero_cap_instance_count_ = 6;
    SPMergeStats m2;
    m2.cross_scope_sum_count_ = 7;
    m2.cap_mismatch_max_rel_ = 0.5;
    m2.routed_signal_net_count_ = 5;
    m2.covered_net_count_ = 5;
    m2.uncovered_net_count_ = 0;
    m2.uncovered_instance_count_ = 0;
    m2.zero_cap_instance_count_ = 1;

    const SPSummary s = sp_merge_summary({&d1, &d2}, 7, {m1, m2}, 9);

    // 逐文件按文件名归并（计数相加、不重复建条目、value_count_max 取最大）
    ASSERT_EQ(s.files_.size(), 2u);
    const SPFileStats& fa = s.files_[0];
    EXPECT_EQ(fa.source_file_, "a.spef");
    EXPECT_EQ(fa.net_count_, 5u);
    EXPECT_EQ(fa.rnet_count_, 1u);
    EXPECT_EQ(fa.hit_count_, 4u);
    EXPECT_EQ(fa.multi_value_net_count_, 2u);
    EXPECT_EQ(fa.value_count_max_, 3u);
    EXPECT_DOUBLE_EQ(fa.c_unit_ff_per_unit_, 1000.0);
    const SPFileStats& fb = s.files_[1];
    EXPECT_EQ(fb.source_file_, "b.spef");
    EXPECT_EQ(fb.net_count_, 5u);
    EXPECT_EQ(fb.hit_count_, 4u);
    // 全局计数 = 各分片之和
    EXPECT_EQ(s.same_scope_overlap_count_, 2u);
    EXPECT_EQ(s.cross_scope_sum_count_, 10u);
    EXPECT_EQ(s.cap_mismatch_count_, 1u);
    EXPECT_DOUBLE_EQ(s.cap_mismatch_max_rel_, 0.5);   // 最大相对偏差取最大
    EXPECT_EQ(s.routed_signal_net_count_, 15u);
    EXPECT_EQ(s.covered_net_count_, 13u);
    EXPECT_EQ(s.uncovered_net_count_, 2u);
    EXPECT_EQ(s.uncovered_instance_count_, 4u);
    EXPECT_EQ(s.zero_cap_instance_count_, 7u);
    // 多成员电气网数 = 设计级参数原样落字段（不聚合合并统计）
    EXPECT_EQ(s.electrical_net_count_, 9u);
    // 缩减网数 = Σ 逐文件
    EXPECT_EQ(s.rnet_count_, 1u);
    // 绑定目标未命中文件数原样落字段；内部单位基准声明
    EXPECT_EQ(s.invalid_binding_count_, 7u);
    EXPECT_DOUBLE_EQ(s.internal_cap_unit_ff_, 1.0);
    EXPECT_DOUBLE_EQ(s.internal_res_unit_ohm_, 1.0);
}

// ── 溢出计数：来源位图只记前 64 个文件（按 file_index 去重计数）─────

TEST(SpPartitionTest, SourceMaskOverflowCount) {
    const FlatEnv env("top", {"u_g_a"}, {"n_a"});
    const FlatCtx fc = make_flat_ctx(env);   // 无 NETS 对象（覆盖率全 0，无碍）

    // 手工构造溢出分片（file_index ≥ 64）：70（scope 0）、71（scope 1）、
    // 70（scope 2）——第三份同文件（70）再次溢出，按 file_index 去重后
    // 计 2 个文件；三份作用域互不相同，贡献相加
    const auto make_slice = [](uint32_t file_index, uint32_t scope_index) {
        SPEntrySlice s;
        s.file_index_ = file_index;
        s.scope_index_ = scope_index;
        SPNetEntry e;
        e.net_id_ = CMNetId{1};
        e.total_cap_reported_ff_ = 1.0;
        s.partitions_[kPid0].part_id_ = kPid0;
        s.partitions_[kPid0].entries_.push_back(e);
        return s;
    };
    SPEntrySlice a = make_slice(70, 0);
    SPEntrySlice b = make_slice(71, 1);
    SPEntrySlice c = make_slice(70, 2);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&a, &b, &c}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 1u);
    EXPECT_DOUBLE_EQ(merged.items_[0].total_cap_reported_ff_, 3.0);
    EXPECT_EQ(merged.items_[0].source_mask_, uint64_t{0});   // 溢出文件不置位
    EXPECT_EQ(st.source_mask_overflow_count_, 2u);           // 按 file_index 去重

    // 正常 file_index（< 64）照常置位、不计数
    SPEntrySlice d = make_slice(3, 0);
    SPMergeStats st2;
    const SPPartitionNetLoad m2 =
        sp_merge_partition(*fc.ctx, {&d}, kPid0, st2);
    ASSERT_EQ(m2.items_.size(), 1u);
    EXPECT_EQ(m2.items_[0].source_mask_, uint64_t{0b1000});
    EXPECT_EQ(st2.source_mask_overflow_count_, 0u);

    // 溢出文件数进汇总（SPSummary 求和聚合）
    const SPSummary s = sp_merge_summary({}, 0, {st}, 0);
    EXPECT_EQ(s.source_mask_overflow_count_, 2u);
}

// ── 15. 电气网分组合并（并查集注入；2026-11-24 用户裁定：flatten 后
//    经层次端口逻辑相连的各段网是同一张电气网）────────────────────────

TEST(SpPartitionTest, ElectricalGroupSumBothMembersInPartition) {
    // 组 {1, 2}（root 1）：文件 1（scope 0）描述 n1（10/7/3）、文件 2
    // （scope 1）描述 n2（5/4/1）——两段负载相加后本区两个网编号下都是
    // 合计 15/11/4、来源位图按位或
    const FlatEnv env("top", {"u_d_n1", "u_r_n1", "u_d_n2", "u_r_n2"},
                      {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    fc.ctx->set_net_union(make_union({{1, 2}}));
    const CMVector<TestConn> conns = {
        {1, 1, true, false}, {1, 2, false, true},
        {2, 3, true, false}, {2, 4, false, true},
    };
    add_nets(*fc.ctx, kPid0, build_nets(kPid0, conns, env));
    const CMString t1 = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 7.0\n"
                                  "2 n1:2 n2:1 3.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n2 5.0\n*CAP\n1 n2:1 4.0\n"
                                  "2 n2:2 n1:1 1.0\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 0, 0);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 1, 1);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 2u);
    for (const SPNetLoad& e : merged.items_) {
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 15.0);
        EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 11.0);
        EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 4.0);
        EXPECT_EQ(e.source_mask_, uint64_t{0b11});
        EXPECT_EQ(e.flags_, 0u);
    }
    // 成员网编号各一份（升序保持——find_net 二分可用）
    EXPECT_EQ(merged.items_[0].net_id_, CMNetId{1});
    EXPECT_EQ(merged.items_[1].net_id_, CMNetId{2});
    EXPECT_NE(merged.find_net(CMNetId{2}), nullptr);
    // 跨段相加不属于单网编号内的跨作用域贡献
    EXPECT_EQ(st.cross_scope_sum_count_, 0u);
    EXPECT_EQ(st.same_scope_overlap_count_, 0u);
}

TEST(SpPartitionTest, ElectricalGroupReducedFlagAnd) {
    // 缩减形态标记 = 全部成员都是缩减形态时才置位：两段 R_NET → 置位；
    // 一段换 D_NET → 与运算清零、明细合计取 D_NET 段
    const FlatEnv env("top", {"u_d_n1", "u_r_n1", "u_d_n2", "u_r_n2"},
                      {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    fc.ctx->set_net_union(make_union({{1, 2}}));
    const CMVector<TestConn> conns = {
        {1, 1, true, false}, {1, 2, false, true},
        {2, 3, true, false}, {2, 4, false, true},
    };
    add_nets(*fc.ctx, kPid0, build_nets(kPid0, conns, env));
    const CMString r1 = spef_text(
        "*R_NET n1 10.0\n*DRV u_d_n1:ZN\n*CELL DCELL\n"
        "*C2_R1_C1 5.0 3.0 2.0\n*LOADS\n*RC u_r_n1:A 0.5\n*END\n");
    const CMString r2 = spef_text(
        "*R_NET n2 5.0\n*DRV u_d_n2:ZN\n*CELL DCELL\n"
        "*C2_R1_C1 2.0 1.0 1.0\n*LOADS\n*RC u_r_n2:A 0.5\n*END\n");
    const SPEntrySlice rr1 = convert_text(*fc.ctx, r1, 0, 0);
    const SPEntrySlice rr2 = convert_text(*fc.ctx, r2, 1, 1);
    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&rr1, &rr2}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 2u);
    for (const SPNetLoad& e : merged.items_) {
        EXPECT_NE(e.flags_ & kSpFlagReduced, 0u);
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 15.0);   // 10 + 5
    }

    // 第二段换 D_NET（5/3/2 明细）→ 缩减位被与运算清零；明细合计取
    // D_NET 段（R_NET 的 π 模型明细不入库〔裁定 5〕——明细合计 0）
    const CMString d2 = spef_text("*D_NET n2 5.0\n*CAP\n1 n2:1 3.0\n"
                                  "2 n2:2 n1:1 2.0\n*END\n");
    const SPEntrySlice dd2 = convert_text(*fc.ctx, d2, 1, 1);
    SPMergeStats st2;
    const SPPartitionNetLoad merged2 =
        sp_merge_partition(*fc.ctx, {&rr1, &dd2}, kPid0, st2);
    ASSERT_EQ(merged2.items_.size(), 2u);
    for (const SPNetLoad& e : merged2.items_) {
        EXPECT_EQ(e.flags_ & kSpFlagReduced, 0u);
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 15.0);   // 10 + 5
        EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 3.0);   // R_NET 段无明细 + 3.0
        EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 2.0);
    }

    // 补条目路径：组里只有 n1 被描述（R_NET）→ 未被描述的 n2 照常补
    // 条目，来源位图与缩减标记同组一致（组只有缩减贡献 → 补条目同为
    // 缩减形态、明细合计无效值 0）
    SPMergeStats st3;
    const SPPartitionNetLoad merged3 =
        sp_merge_partition(*fc.ctx, {&rr1}, kPid0, st3);
    ASSERT_EQ(merged3.items_.size(), 2u);
    for (const SPNetLoad& e : merged3.items_) {
        EXPECT_NE(e.flags_ & kSpFlagReduced, 0u);
        EXPECT_EQ(e.source_mask_, uint64_t{0b1});
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 10.0);
        EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 0.0);
        EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 0.0);
    }
    EXPECT_EQ(merged3.items_[0].net_id_, CMNetId{1});
    EXPECT_EQ(merged3.items_[1].net_id_, CMNetId{2});
}

TEST(SpPartitionTest, ElectricalGroupMemberAbsentFromPartition) {
    // 成员网只落在它自己出现的分区（2026-11-24 评审修正口径：补条目只
    // 跟随本区 NETS 对象的网集合，不因电气网合并而外溢）：组 {1, 2}，
    // 文件 1（scope 0）描述 n1、文件 2（scope 1）描述 n2；分区 0 的
    // NETS 含 n1+n2（全量副本形态）、分区 1 的 NETS 只含 n2（防御形
    // 态——本区网表没有的网不补）。电气网合计要包含落在别的分区的段
    // ——全局逐网编号负载表从全部分片算
    const FlatEnv env("top", {"u_d_n1", "u_r_n1", "u_d_n2", "u_r_n2"},
                      {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    fc.ctx->set_net_union(make_union({{1, 2}}));
    fc.seg->pids_[3] = kPid1;
    fc.seg->pids_[4] = kPid1;
    const CMVector<TestConn> conns = {
        {1, 1, true, false}, {1, 2, false, true},
        {2, 3, true, false}, {2, 4, false, true},
    };
    add_nets(*fc.ctx, kPid0, build_nets(kPid0, conns, env));
    // 分区 1：NETS 只含 n2（防御形态——n1 不在本区网表）
    add_nets(*fc.ctx, kPid1,
             build_nets(kPid1, {{2, 3, true, false}, {2, 4, false, true}},
                        env));
    const CMString t1 = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 7.0\n"
                                  "2 n1:2 n2:1 3.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n2 5.0\n*CAP\n1 n2:1 4.0\n"
                                  "2 n2:2 n1:1 1.0\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 0, 0);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 1, 1);
    ASSERT_NE(find_entry(s1, kPid0, CMNetId{1}), nullptr);
    ASSERT_EQ(find_entry(s1, kPid0, CMNetId{2}), nullptr);
    ASSERT_EQ(find_entry(s2, kPid0, CMNetId{2}), nullptr);
    ASSERT_NE(find_entry(s2, kPid1, CMNetId{2}), nullptr);

    // 分区 0：n1 原始条目 + n2 补条目，都是电气网合计 15/11/4（含落在
    // 分区 1 的 n2 段）
    SPMergeStats st0;
    const SPPartitionNetLoad m0 =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid0, st0);
    ASSERT_EQ(m0.items_.size(), 2u);
    for (const SPNetLoad& e : m0.items_) {
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 15.0);
        EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 11.0);
        EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 4.0);
        EXPECT_EQ(e.source_mask_, uint64_t{0b11});
    }
    EXPECT_EQ(m0.items_[0].net_id_, CMNetId{1});
    EXPECT_EQ(m0.items_[1].net_id_, CMNetId{2});
    // 分区 1：只有 n2 一份（本区 NETS 没有 n1——不补），值相同
    SPMergeStats st1;
    const SPPartitionNetLoad m1 =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid1, st1);
    ASSERT_EQ(m1.items_.size(), 1u);
    EXPECT_EQ(m1.items_[0].net_id_, CMNetId{2});
    EXPECT_DOUBLE_EQ(m1.items_[0].total_cap_reported_ff_, 15.0);
    EXPECT_EQ(m1.items_[0].source_mask_, uint64_t{0b11});
}

// 组内未被描述的成员网也有条目（2026-11-24 评审修正）：组 {1, 2} 只有
// n1 被描述——本区 NETS 里有的 n2 照常拿到电气网合计（下游按手里任
// 意一段的网编号查到的都是整张网的负载；来源位图与缩减标记同组一致）
TEST(SpPartitionTest, ElectricalGroupUndescribedMemberGetsEntry) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1", "u_d_n2", "u_r_n2"},
                      {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    fc.ctx->set_net_union(make_union({{1, 2}}));
    const CMVector<TestConn> conns = {
        {1, 1, true, false}, {1, 2, false, true},
        {2, 3, true, false}, {2, 4, false, true},
    };
    add_nets(*fc.ctx, kPid0, build_nets(kPid0, conns, env));
    const CMString t1 = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 7.0\n"
                                  "2 n1:2 n2:1 3.0\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 0, 0);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&s1}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 2u);
    for (const SPNetLoad& e : merged.items_) {
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 10.0);
        EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 7.0);
        EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 3.0);
        EXPECT_EQ(e.source_mask_, uint64_t{0b1});
        EXPECT_EQ(e.flags_, 0u);
    }
    EXPECT_EQ(merged.items_[0].net_id_, CMNetId{1});
    EXPECT_EQ(merged.items_[1].net_id_, CMNetId{2});
    EXPECT_NE(merged.find_net(CMNetId{2}), nullptr);
}

TEST(SpPartitionTest, ElectricalGroupSingleMemberUnchanged) {
    // 单成员组与现状一致：并查集注入但被描述网不在表（未参与 union——
    // 平展常态）或组是单成员悬空类时，数值 = 本区合并值（跨作用域相加
    // 照旧、来源位图照旧）
    const FlatEnv env("top", {"u_d_n1", "u_r_n1"}, {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    // 组 {5, 6}（root 5）与被描述网无关 + 单成员悬空类 {4}——覆盖「组
    // 结构存在但不影响本网」的两个形态
    fc.ctx->set_net_union(make_union({{5, 6}, {4}}));
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false}, {1, 2, false, true}}, env));
    const CMString t1 = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 7.0\n"
                                  "2 n1:2 n2:1 3.0\n*END\n");
    const CMString t2 = spef_text("*D_NET n1 5.0\n*CAP\n1 n1:1 4.0\n"
                                  "2 n1:2 n2:1 1.0\n*END\n");
    const SPEntrySlice s1 = convert_text(*fc.ctx, t1, 0, 0);
    const SPEntrySlice s2 = convert_text(*fc.ctx, t2, 1, 1);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&s1, &s2}, kPid0, st);
    ASSERT_EQ(merged.items_.size(), 1u);
    EXPECT_DOUBLE_EQ(merged.items_[0].total_cap_reported_ff_, 15.0);
    EXPECT_DOUBLE_EQ(merged.items_[0].ground_cap_ff_, 11.0);
    EXPECT_EQ(merged.items_[0].source_mask_, uint64_t{0b11});
    EXPECT_EQ(st.cross_scope_sum_count_, 1u);
}

TEST(SpPartitionTest, CoverageByElectricalNet) {
    // 覆盖率按电气网口径：组 {1, 2}——n1 有 SPEF、n2 无 SPEF；分母不变
    // （本区实例连接里的已布线信号网 {n1, n2}），一张网「已覆盖」= 它
    // 所在的电气网有负载 → n2 算已覆盖；I1/I2 所连电气网负载非零 → 无
    // 零电容实例
    const FlatEnv env("top", {"I1", "I2"}, {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    fc.ctx->set_net_union(make_union({{1, 2}}));
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, false, true},
                         {1, 2, false, true},
                         {2, 2, false, true}},
                        env));
    add_inst_conns(*fc.ctx, kPid0, build_inst_conns({{1, 1}, {2, 2}}));
    const CMString text = spef_text("*D_NET n1 10.0\n*CAP\n1 n1:1 10.0\n"
                                    "*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text);

    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&slice}, kPid0, st);
    EXPECT_EQ(st.routed_signal_net_count_, 2u);    // 分母不变：n1 + n2
    EXPECT_EQ(st.covered_net_count_, 2u);          // n2 经电气网口径覆盖
    EXPECT_EQ(st.uncovered_net_count_, 0u);
    EXPECT_EQ(st.uncovered_instance_count_, 0u);
    EXPECT_EQ(st.zero_cap_instance_count_, 0u);
    // 未被描述的成员网 n2 也有条目（电气网合计——2026-11-24 评审修正）
    ASSERT_EQ(merged.items_.size(), 2u);
    for (const SPNetLoad& e : merged.items_) {
        EXPECT_DOUBLE_EQ(e.total_cap_reported_ff_, 10.0);
        EXPECT_DOUBLE_EQ(e.ground_cap_ff_, 10.0);
        EXPECT_DOUBLE_EQ(e.coupling_cap_ff_, 0.0);
    }
    EXPECT_EQ(merged.items_[0].net_id_, CMNetId{1});
    EXPECT_EQ(merged.items_[1].net_id_, CMNetId{2});

    // 对照：电气网负载全零（n1 自报/明细全 0）→ 两实例都是零电容实例
    const CMString zero_text = spef_text("*D_NET n1 0.0\n*END\n");
    const SPEntrySlice zero_slice = convert_text(*fc.ctx, zero_text);
    SPMergeStats st_zero;
    sp_merge_partition(*fc.ctx, {&zero_slice}, kPid0, st_zero);
    EXPECT_EQ(st_zero.routed_signal_net_count_, 2u);
    EXPECT_EQ(st_zero.covered_net_count_, 2u);
    EXPECT_EQ(st_zero.uncovered_net_count_, 0u);
    EXPECT_EQ(st_zero.zero_cap_instance_count_, 2u);   // I1 + I2
    // 并查集未注入（平展常态）→ 现状口径：n2 未被描述即未覆盖
    const FlatCtx fc_plain = make_flat_ctx(env);
    add_nets(*fc_plain.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, false, true},
                         {1, 2, false, true},
                         {2, 2, false, true}},
                        env));
    add_inst_conns(*fc_plain.ctx, kPid0, build_inst_conns({{1, 1}, {2, 2}}));
    SPMergeStats st_plain;
    sp_merge_partition(*fc_plain.ctx, {&slice}, kPid0, st_plain);
    EXPECT_EQ(st_plain.routed_signal_net_count_, 2u);
    EXPECT_EQ(st_plain.covered_net_count_, 1u);
    EXPECT_EQ(st_plain.uncovered_net_count_, 1u);
    EXPECT_EQ(st_plain.uncovered_instance_count_, 1u);   // I2 只连 n2
}

// ── 14. 序列化 round-trip ───────────────────────────────────────────

TEST(SpPartitionTest, SerializationRoundTrip) {
    const FlatEnv env("top", {"u_d_n1", "u_r_n1"}, {"n1", "n2"});
    const FlatCtx fc = make_flat_ctx(env);
    add_nets(*fc.ctx, kPid0,
             build_nets(kPid0,
                        {{1, 1, true, false},
                         {1, 2, false, true},
                         {2, 1, true, false},
                         {2, 2, false, true}},
                        env));
    // n1 = D_NET（10 = 10 一致）；n2 = R_NET（缩减形态——覆盖 flags 位）
    const CMString text = spef_text(
        "*D_NET n1 10.0\n*CAP\n1 n1:1 10.0\n*END\n"
        "*R_NET n2 20.0\n*DRV u_d_n1:A\n*C2_R1_C1 1.0 1.0 1.0\n*END\n");
    const SPEntrySlice slice = convert_text(*fc.ctx, text, 2, 5);

    // SPEntrySlice（含分区条目 map + 统计片段）
    CMString blob;
    FLY_ENCODE(slice, blob);
    SPEntrySlice back;
    FLY_DECODE(blob, SPEntrySlice, back);
    EXPECT_EQ(back.file_index_, 2u);
    EXPECT_EQ(back.scope_index_, 5u);
    ASSERT_EQ(back.partitions_.size(), 1u);
    ASSERT_EQ(back.partitions_.at(kPid0).entries_.size(), 2u);
    EXPECT_NE(back.stats_.files_.at(0).hit_count_, 0u);
    // 逐条目比对
    for (const SPNetEntry& e : slice.partitions_.at(kPid0).entries_) {
        const SPNetEntry* e2 = find_entry(back, kPid0, e.net_id_);
        ASSERT_NE(e2, nullptr);
        EXPECT_DOUBLE_EQ(e2->total_cap_reported_ff_,
                         e.total_cap_reported_ff_);
        EXPECT_DOUBLE_EQ(e2->ground_cap_ff_, e.ground_cap_ff_);
        EXPECT_DOUBLE_EQ(e2->coupling_cap_ff_, e.coupling_cap_ff_);
        EXPECT_EQ(e2->flags_, e.flags_);
    }

    // SPPartitionNetLoad（T3 产物；find_net 二分在读回对象上可用）
    SPMergeStats st;
    const SPPartitionNetLoad merged =
        sp_merge_partition(*fc.ctx, {&slice}, kPid0, st);
    CMString blob2;
    FLY_ENCODE(merged, blob2);
    SPPartitionNetLoad merged_back;
    FLY_DECODE(blob2, SPPartitionNetLoad, merged_back);
    EXPECT_EQ(merged_back.part_id_, kPid0);
    ASSERT_EQ(merged_back.items_.size(), merged.items_.size());
    for (size_t i = 0; i < merged.items_.size(); ++i) {
        expect_same_load(merged_back.items_[i], merged.items_[i]);
    }
    ASSERT_NE(merged_back.find_net(CMNetId{2}), nullptr);
    EXPECT_EQ(merged_back.find_net(CMNetId{2})->flags_, kSpFlagReduced);

    // SPSummary
    const SPSummary summary = sp_merge_summary({&slice.stats_}, 4, {st}, 3);
    CMString blob3;
    FLY_ENCODE(summary, blob3);
    SPSummary summary_back;
    FLY_DECODE(blob3, SPSummary, summary_back);
    ASSERT_EQ(summary_back.files_.size(), 1u);
    EXPECT_EQ(summary_back.files_[0].source_file_, "mem.spef");
    EXPECT_EQ(summary_back.files_[0].hit_count_,
              summary.files_[0].hit_count_);
    EXPECT_EQ(summary_back.invalid_binding_count_, 4u);
    EXPECT_EQ(summary_back.electrical_net_count_, 3u);
    EXPECT_EQ(summary_back.rnet_count_, summary.rnet_count_);
    EXPECT_DOUBLE_EQ(summary_back.internal_cap_unit_ff_, 1.0);
}

}  // namespace
