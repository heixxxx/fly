// waveform db 换算/路由/合并/物化单测（plan §11.2 wf_partition_test 矩
// 阵，裁定 V 重构后口径；用合成 design db 数据——同 tm_partition_test 先
// 例；真实样张 small.vcd/part_a*/block_b*/instpin.vcd 为 Icarus Verilog
// 确定性产物，断言依据 qa/emir/data/waveform/README.md 推导表）：
//   1. 名字换算两形态（net 命中 → 全部连接位共享 waveform_id；inst pin
//      直配；双未命中计数 + pg 跳过）；
//   2. 向量位级匹配三情形（位名后缀完整匹配 / 总线 var 各位直和并入单
//      名网 / 位宽失配观测不 fatal——WFMG::0013）；
//   3. 块绑定三形态 + strip_prefix（block_inst local 名偏移 / block_cell
//      定义级建库期复制 / 纯路径 + strip_prefix 段级剥离）+ 块端口归属
//      与 pg 端口跳过；
//   4. 合并后统计口径（裁定 V-2：完整序列前导/间隔/尾段覆盖 [t_start,
//      t_end] 全程——块界缝隙消除；多块拼接统计 == 整读单块统计显式断
//      言；重复行合并端过滤）；
//   5. x/z 严格口径（rise/fall 不含 x/z 参与转换，TX/TZ 分列——裁定 7）；
//   6. 轨道身份归并（§7.2 同 id 双名统一立网身份——引脚名不另立实体）
//      + 四级回落（§7.3 逐级命中/多 output 取第一/全无哨兵）+ 引脚身份
//      波形复制范围（§8 自身分区 + 所连网全部连接位分区）；
//   7. 合并：场景 a 直和 + 偏移后单调（part_a1+part_a2，勿断言拼接 ==
//      单跑）、场景 b 后文件覆盖（block_b1+block_b2 重合取 b2 值 +
//      WFMG::0006 计数 1）、波形 id 身份键全序分配（跨插入序可复现）；
//   8. settings-only 物化（哨兵 waveform_id + overrides 形态①推导展开 +
//      未解析计数 + default 元数据）；8b. 混合模式覆盖应用（裁定 IV-2：
//      统计值替换 + waveform_id 引用保留 + 无条目新建哨兵）；
//   9. small.vcd 全链换算（统计值与 README 推导表逐项断言——重构后口
//      径变化不改变数值）；
//   10. 落库形态序列化 round-trip。
//
// 合成场景 id 备忘（BlkEnv，两层块树各双实例化）：
//   cells: INV(0) DFF(1) XOR2(2) BLK_S(3) BLK_A(4) wf_blk_top(5)
//   pin 注册序 = id：A=0 ZN=1 D=2 CK=3 RN=4 Q=5 QN=6 B=7 Z=8
//   树（DFS 前序）：#0 top{u_blk_a,#1(A#1){u_sub,#2(S#1)}} / {u_blk_a2,
//   #3(A#2){u_sub,#4(S#2)}}；叶单元（INV/DFF/XOR2 实例）不产树节点
//   inst 区间：top [0,3) A#1 [3,7) S#1 [7,11) A#2 [11,15) S#2 [15,19)
//     u_blk_a=1 u_blk_a2=2 u_sub=4 u_dff_a=5 u_inv1=6
//     u_dff_s=8 u_inv_s=9 u_xor_s=10（A#2/S#2 侧 = +8：12/13/14、16/17/18）
//   net 区间（count = 真网数 + 1，global = start + local）：top [0,1)
//     A#1 [1,7)：n_a1=2 n_a2=3 n_out=4 clk=5 rn=6
//     S#1 [7,14)：n1=8 n2=9 n3=10 clk=11 rn=12 n_in=13
//     A#2 [14,20) S#2 [20,27)（= A#1/S#1 + 13）
#include <emir/waveform/cpp/wf_parser.h>
#include <emir/waveform/cpp/wf_partition.h>

#include <common/testing/cpp/test_helpers.h>
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_types.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fly;
using namespace fly::test;

constexpr CMPartitionId kPid{0};
// 1ns 档 fs 因子（真实样张全部 1ns）
constexpr uint64_t kNs = 1000000;

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

// —— 合成 design 环境①：扁平网（顶层块 + 每网一门实例，receiver 挂点；
//    可选前置 extra 实例〔形态②素材〕）———————————————————————————

struct FlatEnv {
    std::shared_ptr<DSDesign> design;
    std::shared_ptr<const DSBlockNames> top_names;
    CMPinId pin_a{0};
    CMPinId pin_zn{1};
    CMVector<uint64_t> net_globals;    // 与入参 nets 平行（global id）
    CMVector<uint64_t> gate_globals;   // 每网挂点实例（pin A，receiver）

    FlatEnv(const CMString& design_name, const CMVector<CMString>& nets,
            const CMVector<std::pair<CMString, CMCellId>>& extra_insts = {}) {
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
        // DFF：D/CK/RN 输入、Q/QN 输出（形态②素材实例的 cell）
        DSCell dff;
        dff.set_name("DFF");
        dff.set_lef_cell();
        for (int i = 0; i < 5; ++i) {
            dff.add_pin(DSPin{});
        }
        d->add_cell(std::move(dff));
        DSCell& dref = d->get_cell(CMCellId{1});
        const CMPinId dff_pins[5] = {CMPinId{2}, CMPinId{3}, CMPinId{4},
                                     CMPinId{5}, CMPinId{6}};
        const DSPinDirection dff_dirs[5] = {
            DSPinDirection::INPUT, DSPinDirection::INPUT,
            DSPinDirection::INPUT, DSPinDirection::OUTPUT,
            DSPinDirection::OUTPUT};
        for (int i = 0; i < 5; ++i) {
            dref.pins_[i].direction_ = dff_dirs[i];
            dref.pins_[i].pin_id_ = dff_pins[i];
        }
        // top block cell（id 2；名 = design_name——根 scope 剥除基准）
        DSCell top_cell;
        top_cell.set_name(design_name);
        top_cell.set_block_cell();
        d->add_cell(std::move(top_cell));
        // 全局 pin 名字空间（注册序 = id：A ZN D CK RN Q QN）
        d->register_pin("A");
        d->register_pin("ZN");
        d->register_pin("D");
        d->register_pin("CK");
        d->register_pin("RN");
        d->register_pin("Q");
        d->register_pin("QN");

        // top 块产物：extra 实例在前、门实例在后（实例 local 从 1 起）
        DSBlockBuildData top_block;
        top_block.init_placeholder(design_name, CMCellId{2});
        CMVector<std::pair<CMString, uint64_t>> inst_reg;
        for (const auto& e : extra_insts) {
            DSInstance inst;
            inst.set_cell_id(e.second);
            const CMInstanceId local =
                top_block.add_instance(std::move(inst), e.first);
            inst_reg.emplace_back(e.first, local.value());
        }
        for (const CMString& n : nets) {
            DSInstance inst;
            inst.set_cell_id(CMCellId{0});
            const CMInstanceId local = top_block.add_instance(
                std::move(inst), "u_g_" + n);
            gate_globals.push_back(local.value());
        }
        for (size_t i = 0; i < nets.size(); ++i) {
            inst_reg.emplace_back("u_g_" + nets[i],
                                  gate_globals[i]);
            net_globals.push_back(top_block.register_net(nets[i]).value());
        }
        auto names = std::make_shared<DSBlockNames>();
        names->block_name_ = design_name;
        for (const auto& item : inst_reg) {
            names->instance_names_->assign(item.first,
                                           CMInstanceId{item.second});
        }
        for (size_t i = 0; i < nets.size(); ++i) {
            names->net_names_->assign(nets[i], CMNetId{net_globals[i]});
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

// —— 合成 design 环境②：两层块树各双实例化（块绑定三形态载体；id 备
//    忘见文件头）———————————————————————————————————————————————

struct BlkEnv {
    std::shared_ptr<DSDesign> design;
    std::shared_ptr<const DSBlockNames> top_names;
    std::shared_ptr<const DSBlockNames> a_names;
    std::shared_ptr<const DSBlockNames> s_names;
    // pin id（注册序）
    CMPinId pin_a{0};
    CMPinId pin_zn{1};
    CMPinId pin_d{2};
    CMPinId pin_ck{3};
    CMPinId pin_rn{4};
    CMPinId pin_q{5};
    CMPinId pin_qn{6};
    CMPinId pin_b{7};
    CMPinId pin_z{8};
    // 实例 global id（A#1/S#1 侧；A#2/S#2 = +8/+12 见文件头）
    static constexpr uint64_t kUblkA = 1;
    static constexpr uint64_t kUblkA2 = 2;
    static constexpr uint64_t kUSub = 4;
    static constexpr uint64_t kUDffA = 5;
    static constexpr uint64_t kUInv1 = 6;
    static constexpr uint64_t kUDffS = 8;
    static constexpr uint64_t kUInvS = 9;
    static constexpr uint64_t kUXorS = 10;
    // 网 global id（A#1/S#1 侧）
    static constexpr uint64_t kNetNA1 = 2;
    static constexpr uint64_t kNetNA2 = 3;
    static constexpr uint64_t kNetNOut = 4;
    static constexpr uint64_t kNetClkA = 5;
    static constexpr uint64_t kNetRnA = 6;
    static constexpr uint64_t kNetN1 = 8;
    static constexpr uint64_t kNetN2 = 9;
    static constexpr uint64_t kNetN3 = 10;
    static constexpr uint64_t kNetClkS = 11;
    static constexpr uint64_t kNetRnS = 12;
    static constexpr uint64_t kNetNIn = 13;

    BlkEnv() {
        auto d = std::make_shared<DSDesign>();
        // INV_X1：A / ZN
        DSCell inv;
        inv.set_name("INV_X1");
        inv.set_lef_cell();
        inv.add_pin(DSPin{});
        inv.add_pin(DSPin{});
        d->add_cell(std::move(inv));
        DSCell& iref = d->get_cell(CMCellId{0});
        iref.pins_[0].direction_ = DSPinDirection::INPUT;
        iref.pins_[0].pin_id_ = pin_a;
        iref.pins_[1].direction_ = DSPinDirection::OUTPUT;
        iref.pins_[1].pin_id_ = pin_zn;
        // DFF_X1：D/CK/RN 输入、Q/QN 输出
        DSCell dff;
        dff.set_name("DFF_X1");
        dff.set_lef_cell();
        for (int i = 0; i < 5; ++i) {
            dff.add_pin(DSPin{});
        }
        d->add_cell(std::move(dff));
        DSCell& dref = d->get_cell(CMCellId{1});
        const CMPinId dff_pins[5] = {pin_d, pin_ck, pin_rn, pin_q, pin_qn};
        const DSPinDirection dff_dirs[5] = {
            DSPinDirection::INPUT, DSPinDirection::INPUT,
            DSPinDirection::INPUT, DSPinDirection::OUTPUT,
            DSPinDirection::OUTPUT};
        for (int i = 0; i < 5; ++i) {
            dref.pins_[i].direction_ = dff_dirs[i];
            dref.pins_[i].pin_id_ = dff_pins[i];
        }
        // XOR2_X1：A/B 输入、Z 输出
        DSCell xorg;
        xorg.set_name("XOR2_X1");
        xorg.set_lef_cell();
        for (int i = 0; i < 3; ++i) {
            xorg.add_pin(DSPin{});
        }
        d->add_cell(std::move(xorg));
        DSCell& xref = d->get_cell(CMCellId{2});
        xref.pins_[0].direction_ = DSPinDirection::INPUT;
        xref.pins_[0].pin_id_ = pin_a;
        xref.pins_[1].direction_ = DSPinDirection::INPUT;
        xref.pins_[1].pin_id_ = pin_b;
        xref.pins_[2].direction_ = DSPinDirection::OUTPUT;
        xref.pins_[2].pin_id_ = pin_z;
        // block cells：BLK_S(3) / BLK_A(4) / 顶层 wf_blk_top(5)
        DSCell s_cell;
        s_cell.set_name("BLK_S");
        s_cell.set_block_cell();
        d->add_cell(std::move(s_cell));
        DSCell a_cell;
        a_cell.set_name("BLK_A");
        a_cell.set_block_cell();
        d->add_cell(std::move(a_cell));
        DSCell top_cell;
        top_cell.set_name("wf_blk_top");
        top_cell.set_block_cell();
        d->add_cell(std::move(top_cell));
        // 全局 pin 名字空间（注册序 = id）
        d->register_pin("A");
        d->register_pin("ZN");
        d->register_pin("D");
        d->register_pin("CK");
        d->register_pin("RN");
        d->register_pin("Q");
        d->register_pin("QN");
        d->register_pin("B");
        d->register_pin("Z");

        // per-DEF 产物（S → A → top；实例 local 从 1 起、网 local 从 1 起）
        auto make_block = [](const char* name, CMCellId cell,
                             const CMVector<std::pair<const char*, CMCellId>>&
                                 insts,
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
        const auto s_block = make_block(
            "BLK_S", CMCellId{3},
            {{"u_dff_s", CMCellId{1}}, {"u_inv_s", CMCellId{0}},
             {"u_xor_s", CMCellId{2}}},
            {"n1", "n2", "n3", "clk", "rn", "n_in"});
        const auto a_block = make_block(
            "BLK_A", CMCellId{4},
            {{"u_sub", CMCellId{3}}, {"u_dff_a", CMCellId{1}},
             {"u_inv1", CMCellId{0}}},
            {"n_a1", "n_a2", "n_out", "clk", "rn"});
        const auto top_block =
            make_block("wf_blk_top", CMCellId{5},
                       {{"u_blk_a", CMCellId{4}}, {"u_blk_a2", CMCellId{4}}},
                       {});

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
        top_names = names("wf_blk_top", {{"u_blk_a", 1}, {"u_blk_a2", 2}}, {});
        a_names = names("BLK_A",
                        {{"u_sub", 1}, {"u_dff_a", 2}, {"u_inv1", 3}},
                        {{"n_a1", 1}, {"n_a2", 2}, {"n_out", 3},
                         {"clk", 4}, {"rn", 5}});
        s_names = names("BLK_S",
                        {{"u_dff_s", 1}, {"u_inv_s", 2}, {"u_xor_s", 3}},
                        {{"n1", 1}, {"n2", 2}, {"n3", 3}, {"clk", 4},
                         {"rn", 5}, {"n_in", 6}});

        // 层级树
        CMVector<DSBlockBuildData> blocks;
        blocks.push_back(*s_block);
        blocks.push_back(*a_block);
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

// —— 上下文组装（单分区 pid 0）———————————————————————————————

std::shared_ptr<WFDesignContext> make_flat_ctx(
    const FlatEnv& env, const CMVector<uint64_t>& pg_net_ids = {}) {
    auto ctx = std::make_shared<WFDesignContext>();
    ctx->set_design(env.design);
    ctx->add_block_names(env.top_names);
    auto index = std::make_shared<DSIdPartitionIndex>();
    index->id_starts_ = {0};
    ctx->set_inst_id_map(index);
    auto seg = std::make_shared<DSIdPartitionSegment>();
    seg->id_start_ = 0;
    seg->pids_.resize(64, kPid);
    ctx->add_inst_segment(seg);
    auto pg = std::make_shared<DSPgNetSet>();
    for (const uint64_t id : pg_net_ids) {
        pg->power_.insert(CMNetId{id});
    }
    ctx->set_pg_nets(pg);
    return ctx;
}

// 扁平网分区 NETS：每网一条 receiver 连接（挂点实例 pin A）
void add_flat_partition_nets(WFDesignContext& ctx, const FlatEnv& env) {
    DSPartitionNets nets;
    nets.part_id_ = kPid;
    for (size_t i = 0; i < env.net_globals.size(); ++i) {
        auto n = std::make_shared<DSNet>();
        n->net_id_ = CMNetId{env.net_globals[i]};
        DSNetConnEntry c;
        c.inst_id_ = CMInstanceId{env.gate_globals[i]};
        c.pin_id_ = env.pin_a;
        c.set_receiver();
        n->connections_.push_back(std::move(c));
        nets.nets_[n->net_id_] = std::move(n);
    }
    ctx.add_partition_nets(std::make_shared<DSPartitionNets>(std::move(nets)));
}

// 块环境分区 NETS：A/S 两实例化副本的全部网连接（driver/receiver 按实
// 际方向）
void add_blk_partition_nets(WFDesignContext& ctx, const BlkEnv& env) {
    DSPartitionNets nets;
    nets.part_id_ = kPid;
    auto add_net = [&nets, &env](uint64_t net_id, uint64_t inst, CMPinId pin,
                                 bool driver, bool receiver) {
        auto n = std::make_shared<DSNet>();
        n->net_id_ = CMNetId{net_id};
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
        nets.nets_[n->net_id_] = std::move(n);
    };
    // A#1
    add_net(env.kNetNA1, env.kUDffA, env.pin_q, true, false);
    add_net(env.kNetNA2, env.kUDffA, env.pin_qn, true, false);
    add_net(env.kNetNOut, env.kUInv1, env.pin_zn, true, false);
    add_net(env.kNetClkA, env.kUDffA, env.pin_ck, false, true);
    add_net(env.kNetRnA, env.kUDffA, env.pin_rn, false, true);
    // S#1
    add_net(env.kNetN1, env.kUDffS, env.pin_q, true, false);
    add_net(env.kNetN2, env.kUInvS, env.pin_zn, true, false);
    add_net(env.kNetN3, env.kUXorS, env.pin_z, true, false);
    add_net(env.kNetClkS, env.kUDffS, env.pin_ck, false, true);
    add_net(env.kNetRnS, env.kUDffS, env.pin_rn, false, true);
    add_net(env.kNetNIn, env.kUXorS, env.pin_b, false, true);
    // A#2 / S#2（global = A#1/S#1 + 13；实例 = +8/+12）
    add_net(env.kNetNA1 + 13, env.kUDffA + 8, env.pin_q, true, false);
    add_net(env.kNetNA2 + 13, env.kUDffA + 8, env.pin_qn, true, false);
    add_net(env.kNetNOut + 13, env.kUInv1 + 8, env.pin_zn, true, false);
    add_net(env.kNetClkA + 13, env.kUDffA + 8, env.pin_ck, false, true);
    add_net(env.kNetRnA + 13, env.kUDffA + 8, env.pin_rn, false, true);
    add_net(env.kNetN1 + 13, env.kUDffS + 8, env.pin_q, true, false);
    add_net(env.kNetN2 + 13, env.kUInvS + 8, env.pin_zn, true, false);
    add_net(env.kNetN3 + 13, env.kUXorS + 8, env.pin_z, true, false);
    add_net(env.kNetClkS + 13, env.kUDffS + 8, env.pin_ck, false, true);
    add_net(env.kNetRnS + 13, env.kUDffS + 8, env.pin_rn, false, true);
    add_net(env.kNetNIn + 13, env.kUXorS + 8, env.pin_b, false, true);
    ctx.add_partition_nets(std::make_shared<DSPartitionNets>(std::move(nets)));
}

std::shared_ptr<WFDesignContext> make_blk_ctx(const BlkEnv& env) {
    auto ctx = std::make_shared<WFDesignContext>();
    ctx->set_design(env.design);
    ctx->add_block_names(env.top_names);
    ctx->add_block_names(env.a_names);
    ctx->add_block_names(env.s_names);
    auto index = std::make_shared<DSIdPartitionIndex>();
    index->id_starts_ = {0};
    ctx->set_inst_id_map(index);
    auto seg = std::make_shared<DSIdPartitionSegment>();
    seg->id_start_ = 0;
    seg->pids_.resize(64, kPid);
    ctx->add_inst_segment(seg);
    ctx->set_pg_nets(std::make_shared<DSPgNetSet>());
    add_blk_partition_nets(*ctx, env);
    return ctx;
}

// —— 测试辅助 ——————————————————————————————

class WfPartitionTest : public ::testing::Test {
protected:
    // 测试工作目录（qa_tmp_dir 惯例：TEST_TMPDIR 沙箱优先，bazel 自动
    // 回收；手动直跑 fallback .work/gtest_tmp）
    CMString tmp_;

    void SetUp() override {
        tmp_ = fly::test::qa_tmp_dir("wf_partition_test");
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
};

// 单文件生产路径口径：T1 规划（缺省大块界 = 单块；档位随切块计划头
// 部解析产出）→ 逐块 T2
struct FileSlices {
    WFVarTable vars;
    WFFileChunkPlan plan;
    CMVector<WFEntrySlice> slices;
};

FileSlices convert_file(const WFDesignContext& ctx, const std::string& path,
                        const WFFileBinding& binding, uint32_t file_index,
                        uint64_t chunk_size = 1u << 30) {
    FileSlices out;
    out.plan = wf_plan_file_chunks(path, chunk_size, out.vars);
    out.slices.reserve(out.plan.chunks_.size());
    for (const WFFileChunk& chunk : out.plan.chunks_) {
        out.slices.push_back(wf_convert_chunk(ctx, path, chunk, out.vars,
                                              out.plan.timescale_fs_,
                                              binding, file_index));
    }
    return out;
}

// T3 每分区合并（文件序 × 块序展开——slices 保活由调用方 FileSlices 承
// 担；ctx = design db 快照上下文，四级回落判定材料）
struct MergeResult {
    WFPartitionWaveform waveform;
    WFPartitionActivity activity;
    uint64_t overlap = 0;
    uint64_t anomaly = 0;
};

MergeResult merge_files(const WFDesignContext& ctx,
                        const CMVector<const FileSlices*>& files,
                        CMPartitionId pid) {
    CMVector<const WFEntrySlice*> ptrs;
    for (const FileSlices* f : files) {
        for (const WFEntrySlice& s : f->slices) {
            ptrs.push_back(&s);
        }
    }
    MergeResult r;
    wf_merge_partition(ctx, ptrs, pid, r.waveform, r.activity, r.overlap,
                       r.anomaly);
    return r;
}

const WFPinActivity* find_pin(const WFPartitionActivity& act, uint64_t inst,
                              CMPinId pin) {
    const auto it = act.items_.find(CMInstanceId{inst});
    return it == act.items_.end() ? nullptr : it->second.find_pin(pin);
}

// 波形事件查找（时刻 → 值；未命中返回 0xFF 哨兵）
int event_value_at(const WFWaveform& w, uint64_t t_fs) {
    for (const WFEvent& e : w.events_) {
        if (e.time_fs_ == t_fs) {
            return static_cast<int>(e.value_);
        }
    }
    return 0xFF;
}

// 最小 VCD 文本骨架（1ns 档；单层根 scope = design 名）
CMString wrap_vcd(const CMString& design, const CMVector<CMString>& var_lines,
                  const CMString& event_body) {
    CMString out = "$timescale 1ns $end\n$scope module " + design +
                   " $end\n";
    for (const CMString& v : var_lines) {
        out += v + "\n";
    }
    out += "$upscope $end\n$enddefinitions $end\n" + event_body;
    return out;
}

// ── 1. 名字换算两形态 ───────────────────────────────────────────────

// 形态①net 命中：全部连接位共享同一 waveform_id（去重引用——裁定 4）；
// 形态②inst pin 直配：单引脚独享波形；pg 网跳过计数
TEST_F(WfPartitionTest, ConvertTwoFormsAndDedup) {
    const FlatEnv env("top", {"nt", "pgnet"},
                      {{"i3", CMCellId{0}}, {"u_cnt0", CMCellId{1}}});
    const auto ctx = make_flat_ctx(env, {2});  // 网 2 = pgnet 入 pg 集
    // 分区 NETS：nt 挂两个连接位（门 3/A + u_cnt0=2/A——同网多位共享波
    // 形的载体）；pgnet 挂门 4/A
    {
        DSPartitionNets nets;
        nets.part_id_ = kPid;
        auto add_net = [&nets](uint64_t net_id) {
            auto n = std::make_shared<DSNet>();
            n->net_id_ = CMNetId{net_id};
            nets.nets_[n->net_id_] = std::move(n);
            return nets.nets_[CMNetId{net_id}];
        };
        auto add_conn = [](const CMSharedPtr<DSNet>& n, uint64_t inst,
                           CMPinId pin) {
            DSNetConnEntry c;
            c.inst_id_ = CMInstanceId{inst};
            c.pin_id_ = pin;
            c.set_receiver();
            n->connections_.push_back(std::move(c));
        };
        const CMSharedPtr<DSNet> nt = add_net(1);
        add_conn(nt, 3, env.pin_a);
        add_conn(nt, 2, env.pin_a);
        add_conn(add_net(2), 4, env.pin_a);
        ctx->add_partition_nets(
            std::make_shared<DSPartitionNets>(std::move(nets)));
    }
    // 实例 id：extra 在前 i3=1 u_cnt0=2；门 u_g_nt=3 u_g_pgnet=4
    write_file("top.vcd",
               wrap_vcd("top",
                        {"$var wire 1 ! nt $end", "$var wire 1 \" pgnet $end",
                         "$scope module i3 $end", "$var wire 1 # A $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n0!\nz\"\n1#\n$end\n#10\n1!\n#20\n0!\n"));
    const WFFileBinding binding;  // 纯路径
    const FileSlices file = convert_file(*ctx, file_path("top.vcd"), binding, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);

    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    EXPECT_EQ(fs.hit_count_, 2u);              // nt + i3/A（pgnet 跳过不计）
    EXPECT_EQ(fs.net_name_miss_count_, 1u);    // A 走形态②（形态①未命中即计）
    EXPECT_EQ(fs.inst_pin_miss_count_, 0u);
    EXPECT_EQ(fs.pg_net_skip_count_, 1u);

    // 形态①：nt → 门实例(3)/A；形态②：i3(1)/A 独享
    const WFPinActivity* nt_pin = find_pin(merged.activity, 3, env.pin_a);
    const WFPinActivity* i3_pin = find_pin(merged.activity, 1, env.pin_a);
    ASSERT_NE(nt_pin, nullptr);
    ASSERT_NE(i3_pin, nullptr);
    EXPECT_EQ(nt_pin->t0_fs_, 10 * kNs);
    EXPECT_EQ(nt_pin->t1_fs_, 10 * kNs);
    EXPECT_EQ(nt_pin->rise_count_, 1u);
    EXPECT_EQ(nt_pin->fall_count_, 1u);
    // 去重引用：nt 统计条目的 waveform_id 指向同一波形实体；形态②波形
    // 独立分配（分配序 = 身份键全序——net 形态先行，分配值本身不假设
    // 声明序）
    ASSERT_EQ(merged.waveform.waveforms_.size(), 2u);
    EXPECT_NE(nt_pin->waveform_id_, i3_pin->waveform_id_);
    EXPECT_LT(nt_pin->waveform_id_, 2u);
    EXPECT_LT(i3_pin->waveform_id_, 2u);
    const WFWaveform& w0 = merged.waveform.waveforms_[nt_pin->waveform_id_];
    EXPECT_EQ(event_value_at(w0, 0), static_cast<int>(WFValue::ZERO));
    EXPECT_EQ(event_value_at(w0, 10 * kNs), static_cast<int>(WFValue::ONE));
    EXPECT_EQ(event_value_at(w0, 20 * kNs), static_cast<int>(WFValue::ZERO));
    EXPECT_EQ(w0.t_start_fs_, 0u);
    EXPECT_EQ(w0.t_end_fs_, 20 * kNs);
    EXPECT_EQ(merged.waveform.waveforms_[i3_pin->waveform_id_].waveform_id_,
              i3_pin->waveform_id_);
    // pgnet 无统计条目（跳过）
    EXPECT_EQ(find_pin(merged.activity, 4, env.pin_a), nullptr);
    // 波形去重核心裁定（裁定 4）：同网全部连接位共享同一 waveform_id，
    // 统计值同源相同——事件序列只存一份
    const WFPinActivity* nt_pin2 = find_pin(merged.activity, 2, env.pin_a);
    ASSERT_NE(nt_pin2, nullptr);
    EXPECT_EQ(nt_pin2->waveform_id_, nt_pin->waveform_id_);
    EXPECT_EQ(nt_pin2->t0_fs_, nt_pin->t0_fs_);
    EXPECT_EQ(nt_pin2->rise_count_, nt_pin->rise_count_);
    // 波形 id = 容器数组下标（裁定 4）
    EXPECT_EQ(merged.waveform.waveforms_[1].waveform_id_, 1u);
}

// 双未命中：形态①未命中（0001）+ 形态②未命中（0002）分别计数
TEST_F(WfPartitionTest, ConvertDoubleMissCounts) {
    const FlatEnv env("top", {"nt"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    write_file("miss.vcd",
               wrap_vcd("top", {"$var wire 1 ! ghost $end"},
                        "#0\n$dumpvars\n0!\n$end\n#10\n1!\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("miss.vcd"), WFFileBinding{}, 0);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    EXPECT_EQ(fs.hit_count_, 0u);
    EXPECT_EQ(fs.net_name_miss_count_, 1u);   // WFMG::0001（形态①）
    EXPECT_EQ(fs.inst_pin_miss_count_, 1u);   // WFMG::0002（形态②双未命中）
    EXPECT_TRUE(file.slices.at(0).partitions_.empty());
}

// ── 2. 向量位级匹配三情形（裁定 8）──────────────────────────────────

TEST_F(WfPartitionTest, VectorBitMatchingThreeCases) {
    // 网：sig[0..3] 逐位独立 / bus 单名 / part[0..1] 逐位（部分命中载体）
    const FlatEnv env("vtop", {"sig[0]", "sig[1]", "sig[2]", "sig[3]", "bus",
                               "part[0]", "part[1]"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    // 门实例 id = 网 id（1..7）：sig[0]→1 … sig[3]→4 bus→5 part[0]→6
    // part[1]→7
    write_file("vec.vcd",
               wrap_vcd("vtop",
                        {"$var wire 1 ! sig[2] $end",
                         "$var wire 4 \" bus [3:0] $end",
                         "$var wire 4 # part [3:0] $end"},
                        "#0\n$dumpvars\n0!\nb0000 \"\nb0000 #\n$end\n"
                        "#10\n1!\nb0101 \"\nb0001 #\n"
                        "#20\n0!\nb0011 \"\nb0010 #\n"
                        "#30\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("vec.vcd"), WFFileBinding{}, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);

    // 情形 1：位名后缀完整匹配——sig[2] → 网 3（窗口 [0,30]，1 段 [10,20)）
    const WFPinActivity* sig2 = find_pin(merged.activity, 3, env.pin_a);
    ASSERT_NE(sig2, nullptr);
    EXPECT_EQ(sig2->t0_fs_, 20 * kNs);
    EXPECT_EQ(sig2->t1_fs_, 10 * kNs);
    EXPECT_EQ(sig2->rise_count_, 1u);
    EXPECT_EQ(sig2->fall_count_, 1u);
    // 情形 2：总线 var 单名各位直和并入 bus 网（位统计直和：t0 = 80、
    // t1 = 40、rise 3、fall 1——逐位推导见测试数据注释）
    const WFPinActivity* bus = find_pin(merged.activity, 5, env.pin_a);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->t0_fs_, 80 * kNs);
    EXPECT_EQ(bus->t1_fs_, 40 * kNs);
    EXPECT_EQ(bus->rise_count_, 3u);
    EXPECT_EQ(bus->fall_count_, 1u);
    // 同 var 全部位事件并入同一波形（bus 波形含位 0/1/2 的全部事件）
    const WFPinActivity* part0 = find_pin(merged.activity, 6, env.pin_a);
    const WFPinActivity* part1 = find_pin(merged.activity, 7, env.pin_a);
    ASSERT_NE(part0, nullptr);
    ASSERT_NE(part1, nullptr);
    // 情形 3：位宽失配观测（part 位 2/3 无 design 网）——计数不 fatal，
    // 命中位照常挂载（part[0]：1@[10,20) rise1 fall1；part[1]：1@[20,30]）
    EXPECT_EQ(fs.width_mismatch_count_, 1u);   // WFMG::0013
    EXPECT_EQ(file.slices.at(0).stats_.files_.at(0).failed_chunk_count_, 0u);
    EXPECT_EQ(part0->t1_fs_, 10 * kNs);
    EXPECT_EQ(part0->rise_count_, 1u);
    EXPECT_EQ(part0->fall_count_, 1u);
    EXPECT_EQ(part1->t1_fs_, 10 * kNs);
    EXPECT_EQ(part1->rise_count_, 1u);
    EXPECT_EQ(part1->fall_count_, 0u);
    EXPECT_EQ(fs.hit_count_, 3u);
}

// ── 3. 块绑定三形态 + strip_prefix ──────────────────────────────────

// 块实例绑定（§7.1）：局部名经块 hasher 偏移换算；块内实例 pin 条目经
// 局部下降 + 全局 pin 名字空间
TEST_F(WfPartitionTest, BlockInstBinding) {
    const BlkEnv env;
    const auto ctx = make_blk_ctx(env);
    write_file("b1s.vcd",
               wrap_vcd("wf_blk_top",
                        {"$scope module u_blk_a $end",
                         "$var wire 1 ! n_a1 $end",
                         "$var wire 1 \" clk $end",
                         "$var wire 1 # u_dff_a/RN $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n0!\n0\"\n1#\n$end\n#10\n1!\n1\"\n"
                        "0#\n#20\n"));
    WFFileBinding binding;
    binding.kind = WFFileBindingKind::BLOCK_INST;
    binding.block_inst = "u_blk_a";
    binding.strip_prefix = "u_blk_a";  // 根剥后余块实例前缀（README 口径）
    const FileSlices file =
        convert_file(*ctx, file_path("b1s.vcd"), binding, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    EXPECT_EQ(fs.hit_count_, 3u);
    // n_a1 → A#1 网 2 → u_dff_a(5)/Q driver 位
    const WFPinActivity* q = find_pin(merged.activity, BlkEnv::kUDffA,
                                      env.pin_q);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->t1_fs_, 10 * kNs);
    EXPECT_EQ(q->rise_count_, 1u);
    // clk → A#1 网 5 → u_dff_a(5)/CK receiver 位
    const WFPinActivity* ck = find_pin(merged.activity, BlkEnv::kUDffA,
                                       env.pin_ck);
    ASSERT_NE(ck, nullptr);
    EXPECT_EQ(ck->t0_fs_, 10 * kNs);
    EXPECT_EQ(ck->t1_fs_, 10 * kNs);
    // u_dff_a/RN → 形态②局部下降：u_dff_a local 2 → global 5
    const WFPinActivity* rn = find_pin(merged.activity, BlkEnv::kUDffA,
                                       env.pin_rn);
    ASSERT_NE(rn, nullptr);
    EXPECT_EQ(rn->t0_fs_, 10 * kNs);
    EXPECT_EQ(rn->t1_fs_, 10 * kNs);
}

// 块定义绑定（§7.2）：定义级建库期复制——对全部实例化节点独立换算
TEST_F(WfPartitionTest, BlockCellBindingReplicates) {
    const BlkEnv env;
    const auto ctx = make_blk_ctx(env);
    write_file("bcell.vcd",
               wrap_vcd("wf_blk_top",
                        {"$scope module u_blk_a $end",
                         "$var wire 1 ! n_a1 $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n0!\n$end\n#10\n1!\n#20\n0!\n"));
    WFFileBinding binding;
    binding.kind = WFFileBindingKind::BLOCK_CELL;
    binding.block_cell = "BLK_A";
    binding.strip_prefix = "u_blk_a";
    const FileSlices file =
        convert_file(*ctx, file_path("bcell.vcd"), binding, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    // 两实例化（u_blk_a=1 / u_blk_a2=2 侧）：n_a1 → 网 2 / 网 15，同值复制
    const WFPinActivity* qa = find_pin(merged.activity, BlkEnv::kUDffA,
                                       env.pin_q);
    const WFPinActivity* qb = find_pin(merged.activity, BlkEnv::kUDffA + 8,
                                       env.pin_q);
    ASSERT_NE(qa, nullptr);
    ASSERT_NE(qb, nullptr);
    EXPECT_EQ(qa->t1_fs_, 10 * kNs);   // 窗口 [0,20]：1 段 [10,20)
    EXPECT_EQ(qb->t1_fs_, 10 * kNs);
    EXPECT_EQ(qa->rise_count_, qb->rise_count_);
    EXPECT_EQ(file.slices.at(0).stats_.files_.at(0).hit_count_, 1u);
}

// 纯路径 + strip_prefix 段级精确剥离（§7.4；剥后余空与 u_blk_ax 段不匹
// 配两防御情形）
TEST_F(WfPartitionTest, StripPrefixPathForm) {
    const BlkEnv env;
    const auto ctx = make_blk_ctx(env);
    write_file("wrapped.vcd",
               wrap_vcd("wf_blk_top",
                        {"$var wire 1 ! tb/u_blk_a/n_a1 $end",
                         "$var wire 1 \" tb/u_blk_ax/n_a1 $end",
                         "$var wire 1 # tb $end"},
                        "#0\n$dumpvars\n0!\n0\"\n1#\n$end\n#10\n1!\n#20\n"));
    WFFileBinding binding;  // 纯路径 + strip_prefix（剥 testbench 外壳段）
    binding.strip_prefix = "tb";
    const FileSlices file =
        convert_file(*ctx, file_path("wrapped.vcd"), binding, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    EXPECT_EQ(fs.strip_miss_count_, 1u);   // 剥后余空（var3 = "tb"）
    EXPECT_EQ(fs.hit_count_, 1u);          // var1：剥后 u_blk_a/n_a1 全局命中
    EXPECT_EQ(fs.net_name_miss_count_, 1u);  // var2：剥后 u_blk_ax/n_a1 未命中
    EXPECT_EQ(fs.inst_pin_miss_count_, 1u);
    const WFPinActivity* q = find_pin(merged.activity, BlkEnv::kUDffA,
                                      env.pin_q);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->t1_fs_, 10 * kNs);
}

// 块端口归属（§7.1 归属键 = 块实例自身全局 id）+ pg 端口跳过（§7.5）
TEST_F(WfPartitionTest, BlockPortBindingAndPgPort) {
    // 专用环境：BLKP 块 cell 带端口 clk(INPUT) + VDD(POWER)、内部网 na
    auto d = std::make_shared<DSDesign>();
    DSCell inv;
    inv.set_name("INV_X1");
    inv.set_lef_cell();
    inv.add_pin(DSPin{});
    inv.add_pin(DSPin{});
    d->add_cell(std::move(inv));
    DSCell& iref = d->get_cell(CMCellId{0});
    iref.pins_[0].direction_ = DSPinDirection::INPUT;
    iref.pins_[0].pin_id_ = CMPinId{0};
    iref.pins_[1].direction_ = DSPinDirection::OUTPUT;
    iref.pins_[1].pin_id_ = CMPinId{1};
    DSCell blkp;
    blkp.set_name("BLKP");
    blkp.set_block_cell();
    blkp.add_pin(DSPin{});  // clk 端口
    blkp.add_pin(DSPin{});  // VDD 端口
    d->add_cell(std::move(blkp));
    DSCell& bref = d->get_cell(CMCellId{1});
    bref.pins_[0].set_port();
    bref.pins_[0].direction_ = DSPinDirection::INPUT;
    bref.pins_[0].pin_id_ = CMPinId{2};
    bref.pins_[1].set_port();
    bref.pins_[1].type_ = DSPinType::POWER;
    bref.pins_[1].direction_ = DSPinDirection::INPUT;
    bref.pins_[1].pin_id_ = CMPinId{3};
    DSCell top_cell;
    top_cell.set_name("ptop");
    top_cell.set_block_cell();
    d->add_cell(std::move(top_cell));
    d->register_pin("A");
    d->register_pin("ZN");
    d->register_pin("clk");
    d->register_pin("VDD");
    DSBlockBuildData blk_block;
    blk_block.init_placeholder("BLKP", CMCellId{1});
    DSInstance u_d;
    u_d.set_cell_id(CMCellId{0});
    blk_block.add_instance(std::move(u_d), "u_d");
    blk_block.register_net("na");
    DSBlockBuildData top_block;
    top_block.init_placeholder("ptop", CMCellId{2});
    DSInstance ub;
    ub.set_cell_id(CMCellId{1});
    top_block.add_instance(std::move(ub), "ub");
    auto top_names = std::make_shared<DSBlockNames>();
    top_names->block_name_ = "ptop";
    top_names->instance_names_->assign("ub", CMInstanceId{1});
    auto blk_names = std::make_shared<DSBlockNames>();
    blk_names->block_name_ = "BLKP";
    blk_names->instance_names_->assign("u_d", CMInstanceId{1});
    blk_names->net_names_->assign("na", CMNetId{1});
    CMVector<DSBlockBuildData> blocks;
    blocks.push_back(std::move(blk_block));
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

    auto ctx = std::make_shared<WFDesignContext>();
    ctx->set_design(d);
    ctx->add_block_names(top_names);
    ctx->add_block_names(blk_names);
    {
        auto index = std::make_shared<DSIdPartitionIndex>();
        index->id_starts_ = {0};
        ctx->set_inst_id_map(index);
        auto seg = std::make_shared<DSIdPartitionSegment>();
        seg->id_start_ = 0;
        seg->pids_.resize(16, kPid);
        ctx->add_inst_segment(seg);
        ctx->set_pg_nets(std::make_shared<DSPgNetSet>());
    }
    DSPartitionNets nets;
    nets.part_id_ = kPid;
    {
        auto n = std::make_shared<DSNet>();
        n->net_id_ = CMNetId{2};  // BLKP 网区间 [1,3)：na = 1 + 1 = 2
        DSNetConnEntry c;
        c.inst_id_ = CMInstanceId{3};  // u_d = BLKP 区间 [2,4) local 1
        c.pin_id_ = CMPinId{1};
        c.set_driver();
        n->connections_.push_back(std::move(c));
        nets.nets_[n->net_id_] = std::move(n);
    }
    ctx->add_partition_nets(std::make_shared<DSPartitionNets>(std::move(nets)));

    write_file("port.vcd",
               wrap_vcd("ptop",
                        {"$scope module ub $end", "$var wire 1 ! clk $end",
                         "$var wire 1 \" VDD $end", "$var wire 1 # na $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n0!\n0#\n$end\n#10\n1!\n1#\n#20\n"));
    WFFileBinding binding;
    binding.kind = WFFileBindingKind::BLOCK_INST;
    binding.block_inst = "ub";
    binding.strip_prefix = "ub";
    const FileSlices file =
        convert_file(*ctx, file_path("port.vcd"), binding, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    // 端口 clk → 块实例自身 ub(1)/clk(2)；VDD = pg 端口跳过；na = 块内网
    EXPECT_EQ(fs.hit_count_, 2u);
    EXPECT_EQ(fs.pg_net_skip_count_, 1u);
    const WFPinActivity* clk = find_pin(merged.activity, 1, CMPinId{2});
    ASSERT_NE(clk, nullptr);
    EXPECT_EQ(clk->t0_fs_, 10 * kNs);
    EXPECT_EQ(clk->t1_fs_, 10 * kNs);
    const WFPinActivity* na = find_pin(merged.activity, 3, CMPinId{1});
    ASSERT_NE(na, nullptr);
    EXPECT_EQ(na->t1_fs_, 10 * kNs);
}

// ── 4. 合并后统计口径（裁定 V-2：块界概念于统计中不存在）────────────

// 多块拼接全程覆盖：信号仅在块 1 有初值事件、跨块无变化（chunk_size 10B
// 强制两块：块 1 = [#0]（窗 [0,0]）、块 2 = [#40..#80]（窗 [40,80]））。
// 统计窗口 = 文件全程 [0,80]，初值态覆盖至窗口末（旧「块界缝隙」口径下
// 块 2 全窗归快照态仅得 40ns，合并后统计无缝隙 = 80ns）
TEST_F(WfPartitionTest, MergedStatsCoverWholeFileWindow) {
    const FlatEnv env("snap", {"a"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    write_file("snap.vcd",
               wrap_vcd("snap", {"$var wire 1 ! a $end"},
                        "#0\n$dumpvars\n1!\n$end\n#40\n#80\n"));
    const FileSlices file = convert_file(*ctx, file_path("snap.vcd"),
                                         WFFileBinding{}, 0, 10);
    ASSERT_EQ(file.plan.chunks_.size(), 2u);
    EXPECT_EQ(file.plan.chunks_[1].t_start_fs_, 40 * kNs);
    EXPECT_EQ(file.plan.chunks_[1].t_end_fs_, 80 * kNs);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFPinActivity* a = find_pin(merged.activity, 1, env.pin_a);
    ASSERT_NE(a, nullptr);
    // 全程 [0,80] 归初值态 1（$dumpvars 初值行 = 序列首事件）；无块界缝
    // 隙——单块整读同值
    EXPECT_EQ(a->t0_fs_, 0u);
    EXPECT_EQ(a->t1_fs_, 80 * kNs);
    EXPECT_EQ(a->tx_fs_, 0u);
    EXPECT_EQ(a->tz_fs_, 0u);
    EXPECT_EQ(a->rise_count_, 0u);   // X→1 非 0→1（严格口径）
    EXPECT_EQ(a->fall_count_, 0u);
    // 分块事件与整读一致（事件不依赖窗口；切片为逐位事件列表结构）
    uint64_t events = 0;
    for (const WFEntrySlice& s : file.slices) {
        for (const auto& [pid, part] : s.partitions_) {
            (void)pid;
            for (const auto& [key, frag] : part.waveforms_) {
                (void)key;
                for (const CMVector<WFEvent>& bit : frag) {
                    events += bit.size();
                }
            }
        }
    }
    EXPECT_EQ(events, 1u);  // @0（1!）
}

// 等价锚点（裁定 V-2 核心回归）：多块拼接合并统计 == 整读单块合并统计
//（波形事件与逐引脚统计逐项一致）——重复行跨块由合并端等值过滤消除
TEST_F(WfPartitionTest, MultiChunkMergeEqualsSingleChunk) {
    const FlatEnv env("eq", {"sig", "bus"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    // 构造跨块重复行：bus 值 0010 → #10 重复行（旧实现依赖块首快照过滤
    // 的形态）+ sig 全程翻转
    write_file("eq.vcd",
               wrap_vcd("eq",
                        {"$var wire 1 ! sig $end",
                         "$var wire 4 \" bus [3:0] $end"},
                        "#0\n$dumpvars\n0!\nb0010 \"\n$end\n"
                        "#10\n1!\nb0010 \"\n"      // bus 重复行（同值）
                        "#20\n0!\nb0100 \"\n"
                        "#30\n1!\nb0110 \"\n"
                        "#40\nx!\nb0110 \"\n"      // bus 再重复 + sig 置 x
                        "#60\n"));
    // 单块
    const FileSlices single = convert_file(*ctx, file_path("eq.vcd"),
                                           WFFileBinding{}, 0);
    ASSERT_EQ(single.plan.chunks_.size(), 1u);
    // 多块（chunk_size 40 字节 → 多块；时间戳行边界收口）
    const FileSlices multi = convert_file(*ctx, file_path("eq.vcd"),
                                          WFFileBinding{}, 0, 40);
    ASSERT_GT(multi.plan.chunks_.size(), 1u);
    const MergeResult m1 = merge_files(*ctx, {&single}, kPid);
    const MergeResult m2 = merge_files(*ctx, {&multi}, kPid);
    // WFMG::0007 不虚计（bus 多位片段 + 单块/跨块两路径锁定）：位间时
    // 刻交错属健康形态，该计数只管文件间时间轴回退（方案 §9）
    EXPECT_EQ(m1.anomaly, 0u);
    EXPECT_EQ(m2.anomaly, 0u);
    // 波形实体集合一致 + 事件逐项一致
    ASSERT_EQ(m1.waveform.waveform_count(), m2.waveform.waveform_count());
    for (size_t i = 0; i < m1.waveform.waveform_count(); ++i) {
        const WFWaveform& w1 = m1.waveform.waveforms_[i];
        const WFWaveform& w2 = m2.waveform.waveforms_[i];
        EXPECT_EQ(w1.waveform_id_, w2.waveform_id_);
        ASSERT_EQ(w1.events_.size(), w2.events_.size());
        for (size_t e = 0; e < w1.events_.size(); ++e) {
            EXPECT_EQ(w1.events_[e].time_fs_, w2.events_[e].time_fs_);
            EXPECT_EQ(w1.events_[e].value_, w2.events_[e].value_);
        }
    }
    // 统计逐项一致（窗口 = 文件全程 [0,60]，两口径同窗）
    ASSERT_EQ(m1.activity.size(), m2.activity.size());
    for (const auto& [inst, ia] : m1.activity.items_) {
        const auto inst_it = m2.activity.items_.find(inst);
        ASSERT_NE(inst_it, m2.activity.items_.end());
        ASSERT_EQ(ia.pins_.size(), inst_it->second.pins_.size());
        for (const WFPinActivity& p1 : ia.pins_) {
            const WFPinActivity* p2 =
                inst_it->second.find_pin(p1.pin_id_);
            ASSERT_NE(p2, nullptr);
            EXPECT_EQ(p1.t0_fs_, p2->t0_fs_);
            EXPECT_EQ(p1.t1_fs_, p2->t1_fs_);
            EXPECT_EQ(p1.tx_fs_, p2->tx_fs_);
            EXPECT_EQ(p1.tz_fs_, p2->tz_fs_);
            EXPECT_EQ(p1.rise_count_, p2->rise_count_);
            EXPECT_EQ(p1.fall_count_, p2->fall_count_);
        }
    }
    // 语义抽检：sig 窗口 [0,60]——0@[0,10) 1@[10,20) 0@[20,30) 1@[30,40)
    // x@[40,60]；1→x 转换不计 fall（严格口径）
    const WFPinActivity* sig = find_pin(m1.activity, 1, env.pin_a);
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(sig->t0_fs_, 20 * kNs);
    EXPECT_EQ(sig->t1_fs_, 20 * kNs);
    EXPECT_EQ(sig->tx_fs_, 20 * kNs);
    EXPECT_EQ(sig->rise_count_, 2u);
    EXPECT_EQ(sig->fall_count_, 1u);
    // bus 四位直和守恒（逐位推导：位 3 恒 0=60、位 2 = 20+40、位 1 =
    // 10+10+10+30、位 0 = 60；合计 240 = 4 位 × 60ns 窗口；rise 2 =
    // 位 1@30 + 位 2@20，重复行不重复计段）
    const WFPinActivity* bus = find_pin(m1.activity, 2, env.pin_a);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->t0_fs_, 150 * kNs);
    EXPECT_EQ(bus->t1_fs_, 90 * kNs);
    EXPECT_EQ(bus->rise_count_, 2u);
    EXPECT_EQ(bus->fall_count_, 1u);
    EXPECT_EQ(bus->t0_fs_ + bus->t1_fs_ + bus->tx_fs_ + bus->tz_fs_,
              4u * 60 * kNs);
}

// ── 5. x/z 严格口径（裁定 7）────────────────────────────────────────

TEST_F(WfPartitionTest, XZExcludedFromRiseFall) {
    const FlatEnv env("xz", {"sig"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    write_file("xz.vcd",
               wrap_vcd("xz", {"$var wire 1 ! sig $end"},
                        "#0\n$dumpvars\nx!\n$end\n#10\n1!\n#20\n0!\n#30\n1!\n"
                        "#40\nx!\n#50\n0!\n#60\n1!\n#70\nz!\n#80\n1!\n"
                        "#100\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("xz.vcd"), WFFileBinding{}, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFPinActivity* sig = find_pin(merged.activity, 1, env.pin_a);
    ASSERT_NE(sig, nullptr);
    // 窗口 [0,100]：X 20 / 1 50 / 0 20 / Z 10；rise 仅 0→1（@30、@60），
    // x→1、z→1 不计；fall 仅 1→0（@20），1→x、1→z 不计
    EXPECT_EQ(sig->tx_fs_, 20 * kNs);
    EXPECT_EQ(sig->tz_fs_, 10 * kNs);
    EXPECT_EQ(sig->t0_fs_, 20 * kNs);
    EXPECT_EQ(sig->t1_fs_, 50 * kNs);
    EXPECT_EQ(sig->rise_count_, 2u);
    EXPECT_EQ(sig->fall_count_, 1u);
    // x/z 事件观测（T2 未过滤口径）：x@0（$dumpvars 初值行——T2 无过滤
    // 全部进分片，裁定 V-2）+ x@40 + z@70
    EXPECT_EQ(file.slices.at(0).stats_.x_z_event_count_, 3u);
}

// ── 6. 合并：场景 a 直和 + 场景 b 覆盖 ──────────────────────────────

// 场景 a（§2.1 主语义）：part_a1 + part_a2 统计直和 + 偏移后全局单调
//（README 推导表；断言写直和成立，勿断言拼接 == 单跑 small.vcd）
TEST_F(WfPartitionTest, MergeScenarioADirectSumWithOffset) {
    // 两个独立环境（根 scope = 各自 DESIGN 名 wf_small_a1/a2）；网序 =
    // FlatEnv 网序，门实例 id = 网序（1..16，无 extra 实例）
    const CMVector<CMString> nets = {
        "clk", "rn", "count[0]", "count[1]", "count[2]", "count[3]",
        "clk_div2", "n_and", "n_xor", "n_b1", "n_b2", "n_cnt0_d",
        "n_cnt1_d", "n_cnt2_d", "n_cnt3_d", "n_div_d"};
    const FlatEnv env1("wf_small_a1", nets);
    const FlatEnv env2("wf_small_a2", nets);
    const auto ctx1 = make_flat_ctx(env1);
    const auto ctx2 = make_flat_ctx(env2);
    add_flat_partition_nets(*ctx1, env1);
    add_flat_partition_nets(*ctx2, env2);

    FileSlices f1 = convert_file(*ctx1, data_path("part_a1.vcd").string(),
                                 WFFileBinding{}, 0);
    FileSlices f2 = convert_file(*ctx2, data_path("part_a2.vcd").string(),
                                 WFFileBinding{}, 1);
    ASSERT_EQ(f1.plan.chunks_.size(), 1u);
    ASSERT_EQ(f2.plan.chunks_.size(), 1u);
    // 嗅探：两文件独立 [0,..] 形态（首时间戳 0）→ a2 偏移 = a1 时长 101ns
    EXPECT_EQ(f1.plan.chunks_[0].t_start_fs_, 0u);
    EXPECT_EQ(f1.plan.chunks_[0].t_end_fs_, 101 * kNs);
    EXPECT_EQ(f2.plan.chunks_[0].t_start_fs_, 0u);
    const MergeResult merged = merge_files(*ctx1, {&f1, &f2}, kPid);
    EXPECT_EQ(merged.overlap, 0u);
    EXPECT_EQ(merged.anomaly, 0u);

    // 直和（README 分段表）：count[0]（门 3）/A：a1 61/40 4/4 + a2 51/50
    // 5/5（T1 直和 90 = small；T0 多 1ns = 分段固有相位）
    const WFPinActivity* c0 = find_pin(merged.activity, env1.gate_globals[2],
                                       env1.pin_a);
    ASSERT_NE(c0, nullptr);
    EXPECT_EQ(c0->t0_fs_, 112 * kNs);
    EXPECT_EQ(c0->t1_fs_, 90 * kNs);
    EXPECT_EQ(c0->rise_count_, 9u);
    EXPECT_EQ(c0->fall_count_, 9u);
    // count[3]（a1 95/6 1/0 + a2 26/75 0/1——SAIF 原始量直和口径）
    const WFPinActivity* c3 = find_pin(merged.activity, env1.gate_globals[5],
                                       env1.pin_a);
    ASSERT_NE(c3, nullptr);
    EXPECT_EQ(c3->t0_fs_, 121 * kNs);
    EXPECT_EQ(c3->t1_fs_, 81 * kNs);
    EXPECT_EQ(c3->rise_count_, 1u);
    EXPECT_EQ(c3->fall_count_, 1u);
    // n_xor（门 9）：a1 61/40 2/2 + a2 45/56 3/2 = small 对应值（rise/fall
    // /T1 直和 == 单跑——README 场景 a 直和关系）
    const WFPinActivity* x = find_pin(merged.activity, env1.gate_globals[8],
                                      env1.pin_a);
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->t0_fs_, 106 * kNs);
    EXPECT_EQ(x->t1_fs_, 96 * kNs);
    EXPECT_EQ(x->rise_count_, 5u);
    EXPECT_EQ(x->fall_count_, 4u);
    // 偏移拼接：全部波形事件全局单调不减、至少一条波形跨过 a1 窗口
    bool found_offset = false;
    for (const WFWaveform& w : merged.waveform.waveforms_) {
        for (size_t i = 1; i < w.events_.size(); ++i) {
            EXPECT_LE(w.events_[i - 1].time_fs_, w.events_[i].time_fs_);
        }
        if (w.t_end_fs_ > 101 * kNs) {
            found_offset = true;   // a2 段整体 +101ns 生效
        }
    }
    EXPECT_TRUE(found_offset);
    // T4 汇总：全局 DURATION = Σ文件时长 = 202ns（README 分段固有语义）
    CMVector<const WFStatsDelta*> deltas;
    deltas.push_back(&f1.slices[0].stats_);
    deltas.push_back(&f2.slices[0].stats_);
    const WFSummary summary = wf_merge_summary(deltas, 0, 0, 0, 0, 0, {});
    EXPECT_EQ(summary.total_duration_fs_, 202 * kNs);
    EXPECT_EQ(summary.files_.size(), 2u);
    EXPECT_EQ(summary.total_hit_count_,
              f1.slices[0].stats_.files_[0].hit_count_ +
                  f2.slices[0].stats_.files_[0].hit_count_);
}

// 场景 b（§2.1）：block_b1 + block_b2 重合条目后文件覆盖（README 覆盖
// 断言：(1) u_blk_a/n_a1 取 b2 值 + WFMG::0006 计数 1；(2) b1 独有保留；
// (3) b2 独有新值）
TEST_F(WfPartitionTest, MergeScenarioBLastFileOverrides) {
    const BlkEnv env;
    const auto ctx = make_blk_ctx(env);
    WFFileBinding binding;
    binding.kind = WFFileBindingKind::BLOCK_INST;
    binding.block_inst = "u_blk_a";
    binding.strip_prefix = "u_blk_a";
    FileSlices b1 = convert_file(*ctx, data_path("block_b1.vcd").string(),
                                 binding, 0);
    FileSlices b2 = convert_file(*ctx, data_path("block_b2.vcd").string(),
                                 binding, 1);
    ASSERT_EQ(b1.plan.chunks_.size(), 1u);
    ASSERT_EQ(b2.plan.chunks_.size(), 1u);
    EXPECT_EQ(b1.slices[0].stats_.files_[0].hit_count_, 5u);
    EXPECT_EQ(b2.slices[0].stats_.files_[0].hit_count_, 17u);  // q_r 未命中
    const MergeResult merged = merge_files(*ctx, {&b1, &b2}, kPid);
    EXPECT_EQ(merged.overlap, 1u);   // WFMG::0006：重合实体恰 1（n_a1）
    EXPECT_EQ(merged.anomaly, 0u);

    // (1) 重合实体 n_a1 → u_dff_a(5)/Q 取 b2 值（≠ b1 的 54/30/3/3）
    const WFPinActivity* na1 = find_pin(merged.activity, BlkEnv::kUDffA,
                                        env.pin_q);
    ASSERT_NE(na1, nullptr);
    EXPECT_EQ(na1->t0_fs_, 45 * kNs);
    EXPECT_EQ(na1->t1_fs_, 29 * kNs);
    EXPECT_EQ(na1->rise_count_, 3u);
    EXPECT_EQ(na1->fall_count_, 2u);
    // (2) b1 独有：clk（74? 否——b1 窗 [0,84]）44/40 8/8 保留
    const WFPinActivity* ck = find_pin(merged.activity, BlkEnv::kUDffA,
                                       env.pin_ck);
    ASSERT_NE(ck, nullptr);
    EXPECT_EQ(ck->t0_fs_, 44 * kNs);
    EXPECT_EQ(ck->t1_fs_, 40 * kNs);
    EXPECT_EQ(ck->rise_count_, 8u);
    EXPECT_EQ(ck->fall_count_, 8u);
    const WFPinActivity* na2 = find_pin(merged.activity, BlkEnv::kUDffA,
                                        env.pin_qn);
    ASSERT_NE(na2, nullptr);
    EXPECT_EQ(na2->t0_fs_, 30 * kNs);
    EXPECT_EQ(na2->t1_fs_, 54 * kNs);
    // (3) b2 独有：n1 → u_dff_s(8)/Q 45/29 2/1
    const WFPinActivity* n1 = find_pin(merged.activity, BlkEnv::kUDffS,
                                       env.pin_q);
    ASSERT_NE(n1, nullptr);
    EXPECT_EQ(n1->t0_fs_, 45 * kNs);
    EXPECT_EQ(n1->t1_fs_, 29 * kNs);
    EXPECT_EQ(n1->rise_count_, 2u);
    EXPECT_EQ(n1->fall_count_, 1u);
    // 波形实体 = b1 五网 + b2 六网 + b2 的 u_dff_s/QN 形态②身份（其余
    // b2 形态②名与网共 id——§7.2 轨道归并不另立实体；n_a1 覆盖不新增；
    // id 分配序 = 身份键全序 = 容器下标）
    ASSERT_EQ(merged.waveform.waveform_count(), 12u);
    for (size_t i = 0; i < merged.waveform.waveform_count(); ++i) {
        EXPECT_EQ(merged.waveform.waveforms_[i].waveform_id_, i);
    }
    // 轨道归并抽检：b2 的 u_dff_s/CK（与 u_sub/clk 共 id）不再有独立实
    // 体——统计挂网身份引用（波形值与网一致，S#1 clk 网统计）
    const WFPinActivity* cks = find_pin(merged.activity, BlkEnv::kUDffS,
                                        env.pin_ck);
    ASSERT_NE(cks, nullptr);
    EXPECT_EQ(cks->t0_fs_, 39 * kNs);
    EXPECT_EQ(cks->t1_fs_, 35 * kNs);
}

// WFMG::0007 逐位独立游标（方案 §9：该计数只管文件间时间轴回退）：多
// 位片段位间时刻交错（后追加位首事件早于前位末事件——总线整挂单网/位
// 直和的健康形态）不虚计
TEST_F(WfPartitionTest, MergeBitInterleaveNotAnomaly) {
    WFEntrySlice slice;
    slice.file_index_ = 0;
    WFPartitionSlice part;
    part.part_id_ = kPid;
    WFWaveformKey key;
    key.is_net_ = true;
    key.net_id_ = CMNetId{1};
    // 位 0 = [0,20]、位 1 = [5,15]——位间交错（旧跨位共享游标虚计 2：
    // 5 < 20 与 15 < 20）
    part.waveforms_.emplace(
        key, CMVector<CMVector<WFEvent>>{
                 {{WFEvent{0, WFValue::ONE},
                   WFEvent{20 * kNs, WFValue::ZERO}},
                  {WFEvent{5 * kNs, WFValue::ZERO},
                   WFEvent{15 * kNs, WFValue::ONE}}}});
    slice.partitions_.emplace(kPid, std::move(part));

    WFDesignContext empty_ctx;  // 空 ctx：本用例不触达四级回落
    CMVector<const WFEntrySlice*> ptrs{&slice};
    MergeResult r;
    wf_merge_partition(empty_ctx, ptrs, kPid, r.waveform, r.activity,
                       r.overlap, r.anomaly);
    EXPECT_EQ(r.anomaly, 0u);
    ASSERT_EQ(r.waveform.waveform_count(), 1u);
    EXPECT_EQ(r.waveform.waveforms_[0].events_.size(), 4u);
}

// WFMG::0007 该计则计（跨文件时间重叠语义保留）：后文件首时间戳非 0 =
// 已在全局时间轴上不再偏移（§2.1），同位偏移对齐后仍回退 → 位内计数
// 恰 1（旧实现同值——锁定修复不误伤该语义）
TEST_F(WfPartitionTest, MergeWithinBitRegressionCounts) {
    const auto make_slice = [](uint32_t file_index, uint64_t t0, WFValue v0,
                               uint64_t t1, WFValue v1) {
        WFEntrySlice slice;
        slice.file_index_ = file_index;
        WFPartitionSlice part;
        part.part_id_ = kPid;
        WFWaveformKey key;
        key.is_net_ = true;
        key.net_id_ = CMNetId{1};
        CMVector<CMVector<WFEvent>> bits;
        CMVector<WFEvent> bit;
        bit.push_back(WFEvent{t0, v0});
        if (t1 > t0) {
            bit.push_back(WFEvent{t1, v1});
        }
        bits.push_back(std::move(bit));
        part.waveforms_.emplace(key, std::move(bits));
        // 文件时间范围（①偏移对齐嗅探材料——取自统计片段）
        WFFileStats fs;
        fs.t_start_fs_ = t0;
        fs.t_end_fs_ = t1;
        slice.stats_.files_.push_back(fs);
        slice.partitions_.emplace(kPid, std::move(part));
        return slice;
    };

    // 文件 0 = [0,100]；文件 1 起点非 0（不偏移）→ 事件 @50(X) < 100 位
    // 内回退
    WFEntrySlice f0 = make_slice(0, 0, WFValue::ONE, 100 * kNs,
                                 WFValue::ZERO);
    WFEntrySlice f1 = make_slice(1, 50 * kNs, WFValue::X, 50 * kNs,
                                 WFValue::X);

    WFDesignContext empty_ctx;
    CMVector<const WFEntrySlice*> ptrs{&f0, &f1};
    MergeResult r;
    wf_merge_partition(empty_ctx, ptrs, kPid, r.waveform, r.activity,
                       r.overlap, r.anomaly);
    EXPECT_EQ(r.anomaly, 1u);
    ASSERT_EQ(r.waveform.waveform_count(), 1u);
    // 0/50/100 三值互异全存活（ONE/X/ZERO——等值过滤不吞）
    EXPECT_EQ(r.waveform.waveforms_[0].events_.size(), 3u);
}

// 波形 id 分配序 = 身份键全序（wf_waveform_key_less）：同一组身份键以
// 相反插入序构造分片 → 合并产物 id → 身份键映射一致，且 id 序 = 全序
//（net 形态按 net_id_ 先行，形态②按 (inst_id_, pin_id_) 字典序后行）
//——与分片无序 map 迭代序解耦（跨编译器/标准库 id 标号可复现）
TEST_F(WfPartitionTest, MergeWaveformIdFollowsIdentityKeyOrder) {
    const CMPinId pin_a{0};
    const auto net_key = [](uint64_t net) {
        WFWaveformKey k;
        k.is_net_ = true;
        k.net_id_ = CMNetId{net};
        return k;
    };
    const auto pin_key = [pin_a](uint64_t inst) {
        WFWaveformKey k;
        k.inst_id_ = CMInstanceId{inst};
        k.pin_id_ = pin_a;
        return k;
    };
    // 各身份键独立事件时刻（合并产物按 t_start 辨识实体 → 键的落位；
    // 时刻跟随键本身——两分片插入序相反，时刻归属不变）
    const auto events_for_key = [&](const WFWaveformKey& k) {
        CMVector<WFEvent> evs;
        const uint64_t t = k.is_net_ ? (k.net_id_.value() == 2u
                                            ? 0u
                                            : 10 * kNs)
                                     : 20 * kNs;
        evs.push_back(WFEvent{t, WFValue::ONE});
        return evs;
    };
    const auto make_slice = [&](const CMVector<WFWaveformKey>& keys) {
        WFEntrySlice slice;
        slice.file_index_ = 0;
        WFPartitionSlice part;
        part.part_id_ = kPid;
        for (const WFWaveformKey& k : keys) {
            // 逐位事件列表结构（裁定 V-2 纯事件分片：键 → 逐位列表）
            part.waveforms_.emplace(
                k, CMVector<CMVector<WFEvent>>{{events_for_key(k)}});
        }
        // 统计挂载引用形态②键（重编后统计侧引用经键查表同步绑定）
        WFStatsItem item;
        item.inst_id_ = CMInstanceId{9};
        item.pin_id_ = pin_a;
        item.waveform_key_ = pin_key(9);
        item.bits_ = {0};
        part.stats_.emplace((item.inst_id_.value() << 32) |
                                item.pin_id_.value(),
                            std::move(item));
        slice.partitions_.emplace(kPid, std::move(part));
        return slice;
    };

    WFEntrySlice s0 = make_slice({net_key(2), net_key(5), pin_key(9)});
    WFEntrySlice s1 = make_slice({pin_key(9), net_key(5), net_key(2)});

    // 空 ctx（回落仅走 1 级自身轨道——NETS 判定材料本测试不触达）
    WFDesignContext empty_ctx;
    const auto merge_one = [](WFDesignContext& ctx, WFEntrySlice& slice) {
        CMVector<const WFEntrySlice*> ptrs{&slice};
        MergeResult r;
        wf_merge_partition(ctx, ptrs, kPid, r.waveform, r.activity, r.overlap,
                           r.anomaly);
        return r;
    };
    const MergeResult m0 = merge_one(empty_ctx, s0);
    const MergeResult m1 = merge_one(empty_ctx, s1);

    // 期望落位（身份键全序）：net2 → id 0、net5 → id 1、(9,pin_a) → id 2
    const CMVector<uint64_t> expect_times = {0u, 10 * kNs, 20 * kNs};
    ASSERT_EQ(m0.waveform.waveform_count(), 3u);
    ASSERT_EQ(m1.waveform.waveform_count(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(m0.waveform.waveforms_[i].waveform_id_, i);
        EXPECT_EQ(m1.waveform.waveforms_[i].waveform_id_, i);
        EXPECT_EQ(m0.waveform.waveforms_[i].t_start_fs_, expect_times[i]);
        EXPECT_EQ(m1.waveform.waveforms_[i].t_start_fs_, expect_times[i]);
    }
    // 统计侧引用同步重写：形态②条目绑重编后的 id（两插入序一致）
    const WFPinActivity* p0 = find_pin(m0.activity, 9, pin_a);
    const WFPinActivity* p1 = find_pin(m1.activity, 9, pin_a);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p0->waveform_id_, 2u);
    EXPECT_EQ(p1->waveform_id_, 2u);
}

// instpin.vcd（形态②直配真样张）：var 名不在任何 net 名空间 → 形态①
// 必然未命中 → 落形态②；统计与 b1 形态①网统计交叉一致（README）
TEST_F(WfPartitionTest, InstpinVcdFormTwo) {
    const BlkEnv env;
    const auto ctx = make_blk_ctx(env);
    const FileSlices file = convert_file(
        *ctx, data_path("instpin.vcd").string(), WFFileBinding{}, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    EXPECT_EQ(fs.hit_count_, 2u);
    // u_inv1 = A#1 区间 [3,7) local 3 → global 6；(6)/A 54/30 3/3、
    // (6)/ZN 30/54 3/3（README instpin 表）
    const WFPinActivity* a = find_pin(merged.activity, BlkEnv::kUInv1,
                                      env.pin_a);
    const WFPinActivity* zn = find_pin(merged.activity, BlkEnv::kUInv1,
                                       env.pin_zn);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(zn, nullptr);
    EXPECT_EQ(a->t0_fs_, 54 * kNs);
    EXPECT_EQ(a->t1_fs_, 30 * kNs);
    EXPECT_EQ(a->rise_count_, 3u);
    EXPECT_EQ(a->fall_count_, 3u);
    EXPECT_EQ(zn->t0_fs_, 30 * kNs);
    EXPECT_EQ(zn->t1_fs_, 54 * kNs);
    // 形态②单引脚独享波形（两 id 独立）
    EXPECT_NE(a->waveform_id_, zn->waveform_id_);
}

// ── 6b. 轨道身份归并（§7.2，裁定 V-1）＋ 四级回落（§7.3，裁定 V-4）
//          ＋ 复制范围扩展（§8，裁定 V-5）＋ 首块起点事件级断言 ───────

// 同 id 码轨道双名（网名 + 引脚名）统一立网身份：引脚名不另立实体、统
// 计挂网身份引用（波形与统计与网一致）；对照组：独立 id 的引脚名照常
// 立自身实体
TEST_F(WfPartitionTest, TrackIdentityMergesToNetForm) {
    const FlatEnv env("mrg", {"nt"}, {{"u_cnt0", CMCellId{1}}});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    // pin 注册序（FlatEnv）：A=0 ZN=1 D=2 CK=3 RN=4 Q=5 QN=6
    const CMPinId pin_q{5};
    const CMPinId pin_qn{6};
    // 轨道 "!"：nt（网名，命中）+ u_cnt0/Q（引脚名）共 id；轨道 "\""：
    // u_cnt0/QN 独立 id（引脚身份对照组）。实例 id：u_cnt0=1、门 u_g_nt=2
    write_file("mrg.vcd",
               wrap_vcd("mrg",
                        {"$var wire 1 ! nt $end", "$scope module u_cnt0 $end",
                         "$var wire 1 ! Q $end", "$var wire 1 \" QN $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n0!\n1\"\n$end\n#10\n1!\n0\"\n#20\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("mrg.vcd"), WFFileBinding{}, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    // 计数口径不变：Q 走形态②命中（net 未命中 0001 照计）
    EXPECT_EQ(fs.hit_count_, 3u);            // nt + u_cnt0/Q + u_cnt0/QN
    EXPECT_EQ(fs.net_name_miss_count_, 2u);  // Q/QN 形态①未命中
    // 实体归并：nt 网身份 + u_cnt0/QN 自身 = 2（Q 不另立实体）
    ASSERT_EQ(merged.waveform.waveform_count(), 2u);
    const WFPinActivity* q = find_pin(merged.activity, 1, pin_q);
    const WFPinActivity* qn = find_pin(merged.activity, 1, pin_qn);
    const WFPinActivity* gate = find_pin(merged.activity, 2, env.pin_a);
    ASSERT_NE(q, nullptr);
    ASSERT_NE(qn, nullptr);
    ASSERT_NE(gate, nullptr);
    // 引脚挂网身份引用：Q 与网连接位同 waveform_id、统计同源
    EXPECT_EQ(q->waveform_id_, gate->waveform_id_);
    EXPECT_EQ(q->t0_fs_, gate->t0_fs_);
    EXPECT_EQ(q->rise_count_, gate->rise_count_);
    // 对照组：独立 id 引脚名照常立自身实体（引用独立）
    EXPECT_NE(qn->waveform_id_, gate->waveform_id_);
    // 归并实体统计 = 网统计（窗口 [0,20]：0@[0,10) 1@[10,20]）
    EXPECT_EQ(q->t0_fs_, 10 * kNs);
    EXPECT_EQ(q->t1_fs_, 10 * kNs);
    EXPECT_EQ(q->rise_count_, 1u);
}

// 四级回落逐级命中（§7.3）：1 自身轨道 → 2 所连网轨道 → 3 同网 output
// 引脚轨道（连接表序第一个）→ 4 连接表第一个有波形引脚（不区分方向）；
// 四级全无 = 哨兵 + 统计全零
TEST_F(WfPartitionTest, FourLevelWaveformFallback) {
    // 实例 id：d1=1 d2=2 r1=3 r2=4 d3=5 r4=6 d4=7 d5=8 r5=9 d6=10 r6=11
    //（门实例 u_g_* 在后，不入连接表）
    const FlatEnv env(
        "fb", {"na", "nb", "nc", "nd"},
        {{"d1", CMCellId{0}}, {"d2", CMCellId{0}}, {"r1", CMCellId{0}},
         {"r2", CMCellId{0}}, {"d3", CMCellId{0}}, {"r4", CMCellId{0}},
         {"d4", CMCellId{0}}, {"d5", CMCellId{0}}, {"r5", CMCellId{0}},
         {"d6", CMCellId{0}}, {"r6", CMCellId{0}}});
    const auto ctx = make_flat_ctx(env);
    // 连接表（条目序 = 落表序）：na = d1/d2 driver + r1/r2 receiver；
    // nb = d3 driver + r4 receiver；nc = d4/d5 driver + r5 receiver；
    // nd = d6 driver + r6 receiver
    {
        DSPartitionNets nets;
        nets.part_id_ = kPid;
        const auto add_net = [&nets](uint64_t net_id) {
            auto n = std::make_shared<DSNet>();
            n->net_id_ = CMNetId{net_id};
            nets.nets_[n->net_id_] = std::move(n);
            return nets.nets_[CMNetId{net_id}];
        };
        const auto conn = [](CMSharedPtr<DSNet>& n, uint64_t inst,
                             CMPinId pin, bool driver) {
            DSNetConnEntry c;
            c.inst_id_ = CMInstanceId{inst};
            c.pin_id_ = pin;
            if (driver) {
                c.set_driver();
            } else {
                c.set_receiver();
            }
            n->connections_.push_back(std::move(c));
        };
        CMSharedPtr<DSNet> na = add_net(1);
        conn(na, 1, env.pin_zn, true);
        conn(na, 2, env.pin_zn, true);
        conn(na, 3, env.pin_a, false);
        conn(na, 4, env.pin_a, false);
        CMSharedPtr<DSNet> nb = add_net(2);
        conn(nb, 5, env.pin_zn, true);
        conn(nb, 6, env.pin_a, false);
        CMSharedPtr<DSNet> nc = add_net(3);
        conn(nc, 7, env.pin_zn, true);
        conn(nc, 8, env.pin_zn, true);
        conn(nc, 9, env.pin_a, false);
        CMSharedPtr<DSNet> nd = add_net(4);
        conn(nd, 10, env.pin_zn, true);
        conn(nd, 11, env.pin_a, false);
        ctx->add_partition_nets(
            std::make_shared<DSPartitionNets>(std::move(nets)));
    }
    // 波形各异的自身轨道：d1/ZN、r1/A、r4/A（nb 唯一有波形者）、
    // d4/ZN（nc 第一 driver）；r5/A、d5/ZN、d6/ZN 声明但零事件（挂载无
    // 实体——回落与哨兵的挂载材料）
    write_file("fb.vcd",
               wrap_vcd("fb",
                        {"$scope module d1 $end", "$var wire 1 ! ZN $end",
                         "$upscope $end",
                         "$scope module r1 $end", "$var wire 1 \" A $end",
                         "$upscope $end",
                         "$scope module r4 $end", "$var wire 1 # A $end",
                         "$upscope $end",
                         "$scope module d3 $end", "$var wire 1 ( ZN $end",
                         "$upscope $end",
                         "$scope module d4 $end", "$var wire 1 $ ZN $end",
                         "$upscope $end",
                         "$scope module r5 $end", "$var wire 1 % A $end",
                         "$upscope $end",
                         "$scope module d5 $end", "$var wire 1 ' ZN $end",
                         "$upscope $end",
                         "$scope module d6 $end", "$var wire 1 & ZN $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n0!\n1\"\n1#\n0$\n$end\n"
                        "#10\n1!\n0\"\n1$\n#20\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("fb.vcd"), WFFileBinding{}, 0);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    // 实体 = 四条自身轨道（r5/d6 零事件不产实体）
    ASSERT_EQ(merged.waveform.waveform_count(), 4u);

    // 1 级：d1/ZN、r1/A 自身轨道
    const WFPinActivity* d1 = find_pin(merged.activity, 1, env.pin_zn);
    const WFPinActivity* r1 = find_pin(merged.activity, 3, env.pin_a);
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(d1->t0_fs_, 10 * kNs);
    EXPECT_EQ(d1->t1_fs_, 10 * kNs);
    EXPECT_EQ(d1->rise_count_, 1u);
    EXPECT_EQ(r1->fall_count_, 1u);
    // 3 级：r5/A（挂载零实体）→ nc 网无轨道 → nc 第一 driver（d4/ZN）
    const WFPinActivity* r5 = find_pin(merged.activity, 9, env.pin_a);
    const WFPinActivity* d4 = find_pin(merged.activity, 7, env.pin_zn);
    ASSERT_NE(r5, nullptr);
    ASSERT_NE(d4, nullptr);
    EXPECT_EQ(r5->waveform_id_, d4->waveform_id_);
    EXPECT_EQ(r5->t0_fs_, d4->t0_fs_);
    EXPECT_EQ(r5->t1_fs_, d4->t1_fs_);
    EXPECT_EQ(r5->rise_count_, d4->rise_count_);
    // 多 output 取连接表序第一：d5/ZN（nc 第二 driver，无自身波形）→
    // 同样落 d4/ZN（表序在前）
    const WFPinActivity* d5 = find_pin(merged.activity, 8, env.pin_zn);
    ASSERT_NE(d5, nullptr);
    EXPECT_EQ(d5->waveform_id_, d4->waveform_id_);
    // 4 级：d3/ZN（挂载零实体）→ nb 无网轨道 → nb 无有波形 driver →
    // 连接表第一个有波形引脚（r4/A，不区分方向）
    const WFPinActivity* d3 = find_pin(merged.activity, 5, env.pin_zn);
    const WFPinActivity* r4 = find_pin(merged.activity, 6, env.pin_a);
    ASSERT_NE(d3, nullptr);
    ASSERT_NE(r4, nullptr);
    EXPECT_EQ(d3->waveform_id_, r4->waveform_id_);
    EXPECT_EQ(d3->t1_fs_, r4->t1_fs_);
    EXPECT_EQ(d3->rise_count_, r4->rise_count_);
    // 哨兵：d6/ZN（挂载零实体）→ nd 全链无波形 → waveform_id 哨兵 +
    // 统计全零（未覆盖信号——消费侧走 default）
    const WFPinActivity* d6 = find_pin(merged.activity, 10, env.pin_zn);
    ASSERT_NE(d6, nullptr);
    EXPECT_EQ(d6->waveform_id_, kWfNoWaveform);
    EXPECT_EQ(d6->t0_fs_, 0u);
    EXPECT_EQ(d6->t1_fs_, 0u);
    EXPECT_EQ(d6->tx_fs_, 0u);
    EXPECT_EQ(d6->tz_fs_, 0u);
    EXPECT_EQ(d6->rise_count_, 0u);
    EXPECT_EQ(d6->fall_count_, 0u);
    // 2 级：r2/A（无自身 var）不被挂载——不出现在统计容器
    EXPECT_EQ(find_pin(merged.activity, 4, env.pin_a), nullptr);
}

// 复制范围扩展（§8，裁定 V-5）：引脚身份波形复制到自身实例 primary 分
// 区 + 所连网全部连接位实例 primary 分区——回落目标波形在其它分区可引
// 用（每区自足）
TEST_F(WfPartitionTest, PinFragmentCopiedToNetPartitions) {
    // 实例 id：x1=1（pid 0）、门 u_g_nx=2（pid 1）——两分区布局
    const FlatEnv env("cp", {"nx"}, {{"x1", CMCellId{0}}});
    const CMPartitionId kPid2{1};
    // 自建 ctx：INST 段表 inst 1 → pid0、inst 2 → pid1
    auto ctx = std::make_shared<WFDesignContext>();
    ctx->set_design(env.design);
    ctx->add_block_names(env.top_names);
    {
        auto index = std::make_shared<DSIdPartitionIndex>();
        index->id_starts_ = {0};
        ctx->set_inst_id_map(index);
        auto seg = std::make_shared<DSIdPartitionSegment>();
        seg->id_start_ = 0;
        seg->pids_ = {kPid, kPid, kPid2};
        ctx->add_inst_segment(seg);
        ctx->set_pg_nets(std::make_shared<DSPgNetSet>());
    }
    {
        DSPartitionNets nets;
        nets.part_id_ = kPid;
        auto n = std::make_shared<DSNet>();
        n->net_id_ = CMNetId{1};
        DSNetConnEntry d;
        d.inst_id_ = CMInstanceId{1};       // x1/ZN driver（pid 0）
        d.pin_id_ = env.pin_zn;
        d.set_driver();
        n->connections_.push_back(std::move(d));
        DSNetConnEntry r;
        r.inst_id_ = CMInstanceId{2};       // 门/A receiver（pid 1）
        r.pin_id_ = env.pin_a;
        r.set_receiver();
        n->connections_.push_back(std::move(r));
        nets.nets_[n->net_id_] = std::move(n);
        ctx->add_partition_nets(
            std::make_shared<DSPartitionNets>(std::move(nets)));
    }
    write_file("cp.vcd",
               wrap_vcd("cp",
                        {"$scope module x1 $end", "$var wire 1 ! ZN $end",
                         "$upscope $end",
                         "$scope module u_g_nx $end", "$var wire 1 \" A $end",
                         "$upscope $end"},
                        "#0\n$dumpvars\n1!\n$end\n#10\n0!\n#20\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("cp.vcd"), WFFileBinding{}, 0);
    // T2 路由：x1/ZN 引脚身份波形 → 自身分区 pid0 + 所连网连接位分区
    // pid1（片段复制、统计不复制）；u_g_nx/A 挂载在自身分区 pid1
    ASSERT_EQ(file.slices.at(0).partitions_.size(), 2u);
    const auto p0 = file.slices.at(0).partitions_.find(kPid);
    const auto p1 = file.slices.at(0).partitions_.find(kPid2);
    ASSERT_NE(p0, file.slices.at(0).partitions_.end());
    ASSERT_NE(p1, file.slices.at(0).partitions_.end());
    const WFWaveformKey pin_key{false, CMNetId{}, CMInstanceId{1},
                                env.pin_zn};
    EXPECT_EQ(p0->second.waveforms_.count(pin_key), 1u);
    EXPECT_EQ(p1->second.waveforms_.count(pin_key), 1u);   // 复制件
    EXPECT_EQ(p0->second.stats_.size(), 1u);               // (1, ZN)
    EXPECT_EQ(p1->second.stats_.size(), 1u);               // (2, A)
    // T3：pid1 内 u_g_nx/A 回落——2 级（网无轨道）落空 → 3 级命中复制件
    const MergeResult merged1 = merge_files(*ctx, {&file}, kPid2);
    const WFPinActivity* gate = find_pin(merged1.activity, 2, env.pin_a);
    ASSERT_NE(gate, nullptr);
    EXPECT_NE(gate->waveform_id_, kWfNoWaveform);
    // 引用波形 = x1/ZN 轨道（复制件统计：窗口 [0,20]，1@[0,10) 0@[10,20]）
    EXPECT_EQ(gate->t0_fs_, 10 * kNs);
    EXPECT_EQ(gate->t1_fs_, 10 * kNs);
    EXPECT_EQ(gate->fall_count_, 1u);
    const WFWaveform& referenced =
        merged1.waveform.waveforms_[gate->waveform_id_];
    EXPECT_EQ(referenced.t_start_fs_, 0u);
    EXPECT_EQ(referenced.t_end_fs_, 10 * kNs);
}

// 首块起点含 $dumpvars 段的事件级断言（裁定 IV-6）：$dumpvars 先于首个
// # 的形态下初值事件不丢失——首块 [dumpvars 起点, EOF) 扫出初值事件并
// 计入统计
TEST_F(WfPartitionTest, DumpvarsFirstChunkInitialEvents) {
    const FlatEnv env("dvi", {"a"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    const CMString body =
        wrap_vcd("dvi", {"$var wire 1 ! a $end"},
                 "$dumpvars\n1!\n$end\n#0\n0!\n#10\n1!\n#20\n");
    write_file("dvi.vcd", body);
    const FileSlices file = convert_file(*ctx, file_path("dvi.vcd"),
                                         WFFileBinding{}, 0);
    ASSERT_EQ(file.plan.chunks_.size(), 1u);
    // 首块起点 = $dumpvars 段起点（早于首个 # 行——裁定 IV-6）
    EXPECT_EQ(file.plan.chunks_[0].start_, body.find("$dumpvars"));
    // 事件不丢：dumpvars 段 1!@0 + #0 0!@0 + #10 1! 同刻合并
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFPinActivity* a = find_pin(merged.activity, 1, env.pin_a);
    ASSERT_NE(a, nullptr);
    // 序列 [(0,1),(0,0),(10,1)]：前导零长，[0,0) 归初值、[0,10) 归 0、
    // [10,20] 归 1；初值 X→1 不计 rise、0→1@10 计
    EXPECT_EQ(a->t0_fs_, 10 * kNs);
    EXPECT_EQ(a->t1_fs_, 10 * kNs);
    EXPECT_EQ(a->rise_count_, 1u);
    EXPECT_EQ(a->fall_count_, 1u);
}

// 无 # 时间戳行的纯 $dumpvars 形态（裁定 IV-6 同源防御）：切块扫描产
// 出单块兜底（块区间 = $dumpvars 段起至文件尾，时间范围 [0,0]）——初
// 值事件照常入库不退化为 0 块任务，与整读解析行为一致
TEST_F(WfPartitionTest, DumpvarsOnlyFileSingleChunkInitialEvents) {
    const FlatEnv env("dvo", {"a"});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    const CMString body =
        wrap_vcd("dvo", {"$var wire 1 ! a $end"}, "$dumpvars\n1!\n$end\n");
    write_file("dvo.vcd", body);
    const FileSlices file = convert_file(*ctx, file_path("dvo.vcd"),
                                         WFFileBinding{}, 0);
    ASSERT_EQ(file.plan.chunks_.size(), 1u);
    EXPECT_EQ(file.plan.chunks_[0].start_, body.find("$dumpvars"));
    EXPECT_EQ(file.plan.chunks_[0].end_, body.size());
    EXPECT_EQ(file.plan.chunks_[0].t_start_fs_, 0u);
    EXPECT_EQ(file.plan.chunks_[0].t_end_fs_, 0u);
    // 初值事件在产物中（事件完整 + 实体绑定；无 # 行 = 无统计窗口，统
    // 计全零属 [0,0] 设计口径）
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFPinActivity* a = find_pin(merged.activity, 1, env.pin_a);
    ASSERT_NE(a, nullptr);
    EXPECT_NE(a->waveform_id_, kWfNoWaveform);
    const WFWaveform& w = merged.waveform.waveforms_[a->waveform_id_];
    ASSERT_EQ(w.events_.size(), 1u);
    EXPECT_EQ(w.events_[0].time_fs_, 0u);
    EXPECT_EQ(w.events_[0].value_, WFValue::ONE);
}

// ── 7. settings-only 物化 ───────────────────────────────────────────

TEST_F(WfPartitionTest, SettingsOnlyMaterialize) {
    const FlatEnv env("top", {"nt", "pgnet"},
                      {{"i3", CMCellId{0}}, {"u_cnt0", CMCellId{1}}});
    const auto ctx = make_flat_ctx(env, {2});
    add_flat_partition_nets(*ctx, env);
    // 本分区实例枚举（全量 1..4）
    CMVector<CMInstanceId> inst_ids;
    for (uint64_t i = 1; i <= 4; ++i) {
        inst_ids.push_back(CMInstanceId{i});
    }
    CMVector<WFActivityOverride> overrides;
    WFActivityOverride net_ov;      // 形态①推导展开：nt → 门(3)/A
    net_ov.name_ = "nt";
    net_ov.toggle_rate_ = 2.0e6;
    net_ov.static_probability_ = 0.4;
    net_ov.rise_fraction_ = 0.6;
    overrides.push_back(net_ov);
    WFActivityOverride pin_ov;      // 形态②直配：i3/A
    pin_ov.name_ = "i3/A";
    pin_ov.toggle_rate_ = 1.0e6;
    overrides.push_back(pin_ov);
    WFActivityOverride missing;     // 双形态未解析
    missing.name_ = "ghost/x";
    overrides.push_back(missing);

    uint64_t unresolved = 0;
    CMVector<CMString> resolved;
    const WFPartitionActivity act = wf_materialize_activity(
        *ctx, kPid, inst_ids, overrides, 1000000000000000ULL /* 1s */, unresolved, resolved);
    EXPECT_EQ(unresolved, 1u);
    // 本区物化成功条目名（flow 侧聚合判定的事实源；overrides 序）
    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_EQ(resolved[0], "nt");
    EXPECT_EQ(resolved[1], "i3/A");
    // 换算公式（plan §4）：T1 = p·D、T0 = (1−p)·D、rise = rate·D·rf
    const WFPinActivity* nt = find_pin(act, 3, env.pin_a);
    ASSERT_NE(nt, nullptr);
    EXPECT_EQ(nt->waveform_id_, kWfNoWaveform);   // 全哨兵
    EXPECT_EQ(nt->t1_fs_, 400000000000000ULL);
    EXPECT_EQ(nt->t0_fs_, 600000000000000ULL);
    EXPECT_EQ(nt->tx_fs_, 0u);
    EXPECT_EQ(nt->tz_fs_, 0u);
    EXPECT_EQ(nt->rise_count_, 1200000u);
    EXPECT_EQ(nt->fall_count_, 800000u);
    const WFPinActivity* i3 = find_pin(act, 1, env.pin_a);
    ASSERT_NE(i3, nullptr);
    EXPECT_EQ(i3->t1_fs_, 500000000000000ULL);
    EXPECT_EQ(i3->rise_count_, 500000u);
    // pg 网覆盖条目 = 未解析（无活动语义）
    CMVector<WFActivityOverride> pg_ov(1);
    pg_ov[0].name_ = "pgnet";
    uint64_t pg_unresolved = 0;
    CMVector<CMString> pg_resolved;
    const WFPartitionActivity act2 = wf_materialize_activity(
        *ctx, kPid, inst_ids, pg_ov, 1000000000000000ULL, pg_unresolved,
        pg_resolved);
    EXPECT_EQ(pg_unresolved, 1u);
    EXPECT_TRUE(pg_resolved.empty());   // pg 网条目不动物化（无活动语义）
    EXPECT_TRUE(act2.items_.empty());
    // 空波形容器构造口（消费接口恒定）
    const WFPartitionWaveform empty = wf_empty_partition_waveform(kPid);
    EXPECT_EQ(empty.part_id_, kPid);
    EXPECT_EQ(empty.waveform_count(), 0u);
    // default 元数据随 summary 落库（不逐引脚物化——裁定 6）
    WFSettingsMeta meta;
    meta.default_toggle_rate_ = 2.0e6;
    meta.default_static_probability_ = 0.5;
    meta.duration_sec_ = 1.0;
    meta.x_z_handling_ = "exclude";
    const WFSummary summary = wf_merge_summary({}, 0, 0, 0, 0, 0, meta);
    EXPECT_DOUBLE_EQ(summary.default_toggle_rate_, 2.0e6);
    EXPECT_DOUBLE_EQ(summary.default_static_probability_, 0.5);
    EXPECT_DOUBLE_EQ(summary.duration_sec_, 1.0);
    EXPECT_EQ(summary.x_z_handling_, "exclude");
}

// ── 7b. 混合模式覆盖应用（裁定 IV-2）────────────────────────────────

TEST_F(WfPartitionTest, MixedModeApplyOverrides) {
    const FlatEnv env("top", {"nt"}, {{"i3", CMCellId{0}}});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    CMVector<CMInstanceId> inst_ids;
    for (uint64_t i = 1; i <= 4; ++i) {
        inst_ids.push_back(CMInstanceId{i});
    }
    // 种子 = T3 合并产物形态：nt 落点（NETS 连接表的门实例 pin A——
    // gate 实例 local 序在 extra 实例 i3 之后）带实测统计 + 波形引用 7
    //（i3/A 无条目——新建口径素材）
    const uint64_t nt_inst = env.gate_globals[0];
    WFPartitionActivity act;
    act.part_id_ = kPid;
    WFInstanceActivity& seeded = act.items_[CMInstanceId{nt_inst}];
    WFPinActivity measured;
    measured.pin_id_ = env.pin_a;
    measured.waveform_id_ = 7u;
    measured.t0_fs_ = 161 * kNs;
    measured.t1_fs_ = 40 * kNs;
    measured.tx_fs_ = 5 * kNs;
    measured.rise_count_ = 1u;
    seeded.pins_.push_back(measured);

    CMVector<WFActivityOverride> ovs;
    WFActivityOverride net_ov;      // 形态①推导展开：nt → 门(3)/A
    net_ov.name_ = "nt";
    net_ov.toggle_rate_ = 2.0e6;
    net_ov.static_probability_ = 0.4;
    net_ov.rise_fraction_ = 0.6;
    ovs.push_back(net_ov);
    WFActivityOverride pin_ov;      // 形态②直配：i3/A 无条目 → 新建哨兵
    pin_ov.name_ = "i3/A";
    pin_ov.toggle_rate_ = 1.0e6;
    ovs.push_back(pin_ov);

    CMVector<CMString> applied;
    const uint64_t count = wf_apply_overrides(
        *ctx, kPid, inst_ids, act, ovs, 1000000000000000ULL /* 1s */,
        applied);
    EXPECT_EQ(count, 2u);
    ASSERT_EQ(applied.size(), 2u);
    EXPECT_EQ(applied[0], "nt");
    EXPECT_EQ(applied[1], "i3/A");
    // 命中条目：统计值替换（T1 = p·D 等）+ waveform_id 引用保留（裁定
    // IV-2）+ TX/TZ 归零（换算公式口径）
    const WFPinActivity* nt = find_pin(act, nt_inst, env.pin_a);
    ASSERT_NE(nt, nullptr);
    EXPECT_EQ(nt->waveform_id_, 7u);
    EXPECT_EQ(nt->t1_fs_, 400000000000000ULL);
    EXPECT_EQ(nt->t0_fs_, 600000000000000ULL);
    EXPECT_EQ(nt->tx_fs_, 0u);
    EXPECT_EQ(nt->tz_fs_, 0u);
    EXPECT_EQ(nt->rise_count_, 1200000u);
    EXPECT_EQ(nt->fall_count_, 800000u);
    // 无条目落点：物化口径新建（哨兵引用）
    const WFPinActivity* i3 = find_pin(act, 1, env.pin_a);
    ASSERT_NE(i3, nullptr);
    EXPECT_EQ(i3->waveform_id_, kWfNoWaveform);
    EXPECT_EQ(i3->t1_fs_, 500000000000000ULL);
    EXPECT_EQ(i3->rise_count_, 500000u);
    // 未命中条目：不进 applied_names（flow 侧聚合判定 WFMG::0001 的事实源）
    CMVector<WFActivityOverride> ghost_ovs(1);
    ghost_ovs[0].name_ = "ghost/x";
    CMVector<CMString> ghost_applied;
    const uint64_t ghost_count = wf_apply_overrides(
        *ctx, kPid, inst_ids, act, ghost_ovs, 1000000000000000ULL,
        ghost_applied);
    EXPECT_EQ(ghost_count, 0u);
    EXPECT_TRUE(ghost_applied.empty());
}

// ── 8. small.vcd 全链换算（README 推导表逐项断言）───────────────────

TEST_F(WfPartitionTest, SmallVcdFullChainStats) {
    // 16 网扁平环境（网 id = 序 1..16 → 门实例 id 同序）；另含 5 个 DFF
    // 实例（u_cnt0..u_div = 形态②素材，id 1..5）
    const CMVector<CMString> nets = {
        "clk", "rn", "count[0]", "count[1]", "count[2]", "count[3]",
        "clk_div2", "n_and", "n_xor", "n_b1", "n_b2", "n_cnt0_d",
        "n_cnt1_d", "n_cnt2_d", "n_cnt3_d", "n_div_d"};
    const FlatEnv env("wf_small", nets,
                      {{"u_cnt0", CMCellId{1}}, {"u_cnt1", CMCellId{1}},
                       {"u_cnt2", CMCellId{1}}, {"u_cnt3", CMCellId{1}},
                       {"u_div", CMCellId{1}}});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);

    const FileSlices file = convert_file(
        *ctx, data_path("small.vcd").string(), WFFileBinding{}, 0);
    ASSERT_EQ(file.plan.chunks_.size(), 1u);
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    const WFFileStats& fs = file.slices.at(0).stats_.files_.at(0);
    EXPECT_EQ(fs.failed_chunk_count_, 0u);
    // 命中 = 13 网形态① + 25 形态②（5 DFF × CK/Q/QN/RN/D；q_r 未命中）
    EXPECT_EQ(fs.hit_count_, 38u);
    EXPECT_EQ(fs.net_name_miss_count_, 55u);   // 68 − 13
    EXPECT_EQ(fs.inst_pin_miss_count_, 30u);   // 68 − 13 − 25
    EXPECT_EQ(fs.width_mismatch_count_, 0u);   // count 位探测全命中
    EXPECT_EQ(fs.timescale_sec_, 1e-9);
    EXPECT_EQ(fs.t_end_fs_, 201 * kNs);
    // 实例覆盖：5 DFF + 16 门 = 21
    EXPECT_EQ(merged.activity.size(), 21u);

    // README 推导表逐项断言（门实例 id = 网序 1..16；fs = ns × 1e6）
    struct Row {
        const char* net;
        uint64_t t0;
        uint64_t t1;
        uint64_t rise;
        uint64_t fall;
    };
    const Row table[] = {
        {"clk", 101, 100, 20, 20},        {"rn", 23, 178, 1, 0},
        {"count[0]", 111, 90, 9, 9},      {"count[1]", 115, 86, 5, 4},
        {"count[2]", 121, 80, 2, 2},      {"count[3]", 121, 80, 1, 1},
        {"clk_div2", 111, 90, 9, 9},      {"n_and", 161, 40, 1, 1},
        {"n_xor", 105, 96, 5, 4},         {"n_b1", 161, 40, 4, 4},
        {"n_b2", 181, 20, 2, 2},          {"n_cnt0_d", 90, 111, 9, 9},
        {"n_cnt1_d", 105, 96, 5, 4},      {"n_cnt2_d", 121, 80, 2, 2},
        {"n_cnt3_d", 121, 80, 1, 1},      {"n_div_d", 90, 111, 9, 9},
    };
    for (const Row& row : table) {
        const size_t idx = static_cast<size_t>(std::find(nets.begin(),
                                                         nets.end(),
                                                         row.net) -
                                              nets.begin());
        const WFPinActivity* pin = find_pin(merged.activity,
                                            env.gate_globals[idx],
                                            env.pin_a);
        ASSERT_NE(pin, nullptr) << row.net;
        EXPECT_EQ(pin->t0_fs_, row.t0 * kNs) << row.net;
        EXPECT_EQ(pin->t1_fs_, row.t1 * kNs) << row.net;
        EXPECT_EQ(pin->tx_fs_, 0u) << row.net;
        EXPECT_EQ(pin->tz_fs_, 0u) << row.net;
        EXPECT_EQ(pin->rise_count_, row.rise) << row.net;
        EXPECT_EQ(pin->fall_count_, row.fall) << row.net;
    }
    // 形态②交叉一致：u_cnt0(1)/CK ≡ clk（同 id 轨道——README 特征 3）
    const WFPinActivity* ck = find_pin(merged.activity, 1, CMPinId{3});
    ASSERT_NE(ck, nullptr);
    EXPECT_EQ(ck->t0_fs_, 101 * kNs);
    EXPECT_EQ(ck->t1_fs_, 100 * kNs);
    EXPECT_EQ(ck->rise_count_, 20u);
    // 波形去重：波形实体数 = 16 网 + 9 形态②身份（Q×4 + QN×5）——端口
    // 连接别名 var（CK/RN/D/u_div/Q 与网名共 id）经 §7.2 轨道归并挂网身
    // 份引用，不再各立实体
    EXPECT_EQ(merged.waveform.waveform_count(), 25u);
    // T4：全局 DURATION = 201ns；settings 元数据缺省
    CMVector<const WFStatsDelta*> deltas;
    deltas.push_back(&file.slices[0].stats_);
    const WFSummary summary = wf_merge_summary(deltas, 25, 0, 0, 0, 0, {});
    EXPECT_EQ(summary.total_duration_fs_, 201 * kNs);
    EXPECT_EQ(summary.waveform_count_, 25u);
    EXPECT_EQ(summary.failed_chunk_count_, 0u);
}

// ── 9. 落库形态序列化 round-trip ────────────────────────────────────

TEST_F(WfPartitionTest, SerializationRoundTrip) {
    const FlatEnv env("top", {"nt"}, {{"i3", CMCellId{0}}});
    const auto ctx = make_flat_ctx(env);
    add_flat_partition_nets(*ctx, env);
    write_file("rt.vcd",
               wrap_vcd("top",
                        {"$var wire 1 ! nt $end", "$scope module i3 $end",
                         "$var wire 1 # A $end", "$upscope $end"},
                        "#0\n$dumpvars\n0!\n1#\n$end\n#10\n1!\n#20\n"));
    const FileSlices file =
        convert_file(*ctx, file_path("rt.vcd"), WFFileBinding{}, 0);

    // WFEntrySlice（含身份键 map + 统计片段 + delta）
    CMString slice_blob;
    FLY_ENCODE(file.slices.at(0), slice_blob);
    WFEntrySlice slice_back;
    FLY_DECODE(slice_blob, WFEntrySlice, slice_back);
    ASSERT_EQ(slice_back.partitions_.size(), 1u);
    const WFPartitionSlice& part = slice_back.partitions_.begin()->second;
    EXPECT_EQ(part.waveforms_.size(), 2u);   // nt 网 + i3/A
    EXPECT_EQ(part.stats_.size(), 2u);
    EXPECT_EQ(slice_back.stats_.files_.at(0).hit_count_, 2u);
    EXPECT_EQ(slice_back.file_index_, 0u);

    // T3/T4 正式对象
    const MergeResult merged = merge_files(*ctx, {&file}, kPid);
    CMString wave_blob;
    FLY_ENCODE(merged.waveform, wave_blob);
    WFPartitionWaveform wave_back;
    FLY_DECODE(wave_blob, WFPartitionWaveform, wave_back);
    EXPECT_EQ(wave_back.part_id_, kPid);
    ASSERT_EQ(wave_back.waveform_count(), 2u);
    // 波形 id = 容器数组下标；事件数据经 pin 引用侧核对（id 分配序 =
    // 合并处理序，不假设 var 声明序）
    const WFPinActivity* nt_live = find_pin(merged.activity, 2, env.pin_a);
    ASSERT_NE(nt_live, nullptr);
    EXPECT_EQ(wave_back.waveforms_[nt_live->waveform_id_].events_.size(), 2u);
    EXPECT_EQ(wave_back.waveforms_[nt_live->waveform_id_].waveform_id_,
              nt_live->waveform_id_);

    CMString act_blob;
    FLY_ENCODE(merged.activity, act_blob);
    WFPartitionActivity act_back;
    FLY_DECODE(act_blob, WFPartitionActivity, act_back);
    EXPECT_EQ(act_back.part_id_, kPid);
    const WFPinActivity* nt = find_pin(act_back, 2, env.pin_a);
    ASSERT_NE(nt, nullptr);
    EXPECT_EQ(nt->t0_fs_, 10 * kNs);
    EXPECT_EQ(nt->waveform_id_, nt_live->waveform_id_);

    WFSummary summary = wf_merge_summary({}, 3, 1, 2, 4, 5, {});
    CMString summary_blob;
    FLY_ENCODE(summary, summary_blob);
    WFSummary summary_back;
    FLY_DECODE(summary_blob, WFSummary, summary_back);
    EXPECT_EQ(summary_back.waveform_count_, 3u);
    EXPECT_EQ(summary_back.overlap_override_count_, 1u);
    EXPECT_EQ(summary_back.time_anomaly_count_, 2u);
    EXPECT_EQ(summary_back.invalid_binding_count_, 4u);
    EXPECT_EQ(summary_back.override_entry_count_, 5u);   // 混合模式覆盖计数

    WFSettingsMeta meta;
    meta.x_z_handling_ = "hold_previous";
    CMString meta_blob;
    FLY_ENCODE(meta, meta_blob);
    WFSettingsMeta meta_back;
    FLY_DECODE(meta_blob, WFSettingsMeta, meta_back);
    EXPECT_EQ(meta_back.x_z_handling_, "hold_previous");
}

}  // namespace
