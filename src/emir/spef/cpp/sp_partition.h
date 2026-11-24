#pragma once

// =============================================================================
// spef db 换算/路由/合并层（spef-db-plan §2.2/§2.4/§5.2/§5.3/§6/§7，
// 2026-11-24 全裁定；签名与结构形态对齐 waveform wf_partition /
// timing tm_partition 先例——SPDesignContext 跨 db 直读组装 /
// sp_convert_chunk / sp_merge_partition / sp_merge_summary 同构）。
//
// 三层结构（对齐 plan §6 直接任务链）：
//   SPEntrySlice        T2 逐块解析产物分区分片（temp）：本块触及各分
//                       区的网负载条目（net id → 三电容值 + 缩减形态标
//                       记）+ 逐文件统计片段（解析层计数原样带入 + 本
//                       层命中/跳过计数）
//   SPPartitionNetLoad  T3 正式对象（PART_{xp}_{yp}.NET_LOAD）：按来源
//                       作用域合并后的逐网网负载——首期消费者 power db
//                       的全部所需（裁定 2：网负载轻对象，RC 明细不入库）
//   SPSummary           T4 正式对象（"summary"）：逐文件统计 + 全局计
//                       数 + 覆盖率统计（裁定 10）+ 行首值与明细合计不
//                       一致观测（SPMG::0013）
//
// 名字换算（§2.2）：消费 design db 快照（DSDesign 层级树 + net hasher、
// DSBlockNames_<i> 块 local 名空间、id_partition_map INST 段表与段对象、
// pg_nets 全局集、分区 NETS 对象）——上下文 SPDesignContext 以
// CMSharedPtr 共享注入（§16 业务层全禁裸指针；与 WFDesignContext /
// TMDesignContext 同构的 spef 自有 context 类，不改 design 模块）。
//
// 名字归一与查找容错（§2.2，2026-11-23 源码核实口径）：查找前先把名字
// 归一——文件头的层次分隔符不是 '/' 时，把未被转义的分隔符字符换成
// '/'（反斜杠转义的下一个字符原样保留），design db 层级路径恒 '/'。
// 查找容错：先按归一后的原名查，未命中且名字含反斜杠时去掉全部反斜杠
// 再查一次（部分商业提取工具不合规转义的宽容口径，OpenSTA 同款），
// 仍未命中才计未匹配。查找收敛在 lookup 函数单点，后续增加回退步骤只
// 动一处。
//
// 块绑定三形态（同 timing §7 先例）：PATH = 条目名是全层级路径（不含
// 设计名前缀，全局 net mapper 直查）；BLOCK_INST = 条目名是该块实例内
// 的局部名，经该块的 local 名空间加偏移换算成全局 id；BLOCK_CELL =
// 条目名是该块定义内的局部名，对该定义的全部实例化各算一次（建库期
// 复制）。strip_prefix 段级精确匹配后剥除（未命中或剥后余空 → 条目跳
// 过 + strip_miss_count）。
//
// 跨块边界的网与合并语义（§2.4，裁定 8）：同一展平网在块绑定文件与顶
// 层文件里是互补的两部分寄生——不同作用域的贡献相加（整网负载 = 各段
// 之和）；绑定目标相同的文件作用域相同（调用方以 scope_index 标注，
// 本层只把它记进分片供合并判定），同作用域重合保留首份（按文件序、
// 块序最先出现的）。
//
// 覆盖率统计口径（§2.4/裁定 10；分母材料 2026-11-24 实施期修正）：分
// 子分母材料 = design db 的**本分区实例连接对象**（
// PART_{xp}_{yp}.INST_CONNECTIONS——寄生条目按连接位实例归属分区）+
// **已布线信号网 id 集合**（上下文全部分区 NETS 对象里出现过的网 id
// 并集——信号网 NETS 全量补全，任一副本即全集）。已布线信号网数 =
// 本区实例连接里出现的、属于该集合的不同网数；实例集合 = 本区实例
// 连接里出现过的实例，且至少有一条连接落在已布线信号网上。**键 0**
//（root 块自身的设计级端口连接）整键排除——root 非实体实例，端口外
// 的片外负载也不在 SPEF 里（设计级端口连接既不进网分母也不进实例集
// 合）。NETS 对象按**布线几何**归属分区而寄生条目按**连接位实例**归
// 属分区，两者可以不同——几何落在本区、本区却没有它的连接位实例的
// 网（纯粹穿过的网）不计入本区分母（本区的负载消费者用不到它）。本
// 区实例连接对象缺失时各项记 0，不报错（同「NETS 对象缺失」口径）。
// 各项按「分区 × 网」统计后逐分区求和（跨分区的网与连接实例在多个
// 分区各计一次；比率可用，绝对数不做全局去重——2026-11-24 评审裁
// 定）。
//
// id 体系：全部 CM 族强类型（emir_ids.h）。确定性：分片条目与合并产物
// 均按 net id 升序，不依赖无序容器迭代序。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <emir/common/cpp/emir_ids.h>
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_id_map.h>
#include <emir/design/cpp/ds_name_hasher.h>
#include <emir/design/cpp/ds_name_mapper.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/design/cpp/ds_union.h>
#include <emir/spef/cpp/sp_types.h>

#include <cstdint>

namespace fly {

// 缩减形态标记位（SPNetEntry / SPNetLoad 的 flags_ bit0 = R_NET 收录，
// 裁定 5：行首总电容照常收录，两个明细合计值无效）
inline constexpr uint8_t kSpFlagReduced = 0x1;

// ── 文件绑定形态（三形态 + strip_prefix；TMFileBinding 同构的 spef 侧
//    副本——不引 timing 模块头；Python 侧构建后直传，无序列化需求）──

enum class SPFileBindingKind : uint8_t {
    PATH = 0,        // 纯路径（条目名 = 全层级路径，不含设计名前缀）
    BLOCK_INST = 1,  // block_inst（条目名 = 该块实例内局部名）
    BLOCK_CELL = 2,  // block_cell（条目名 = 该 cell 定义内局部名，定义级
                     // 对全部实例化成立——建库期复制）
};

struct SPFileBinding {
    SPFileBindingKind kind = SPFileBindingKind::PATH;
    CMString block_inst;
    CMString block_cell;
    CMString strip_prefix;
};

// ── T2 逐块产物（plan §5.3 中间形态，链上任务自清理）────────────────

// 单分区单网条目（值取自 SPNet 聚合量——三值并存口径 §5.2）
struct SPNetEntry {
    CMNetId net_id_{};
    double total_cap_reported_ff_ = 0.0;   // 行首自报总电容（提取工具口径）
    double ground_cap_ff_ = 0.0;           // 明细合计：对地电容
    double coupling_cap_ff_ = 0.0;         // 明细合计：耦合电容（全值）
    uint8_t flags_ = 0;                    // bit0 = 缩减形态（R_NET，裁定 5）

    FLY_SERIALIZE(net_id_, total_cap_reported_ff_, ground_cap_ff_,
                  coupling_cap_ff_, flags_)
};

// T2 逐块产物单分区切片：本块触及本分区的网条目（按 net id 升序）
class SPPartitionSlice {
public:
    CMPartitionId part_id_;
    CMVector<SPNetEntry> entries_;   // 按 net id 升序

    FLY_SERIALIZE(part_id_, entries_)
};

// 逐文件统计（SPSummary.files_ 条目 + SPStatsDelta 逐块片段；解析层计
// 数由 sp_convert_chunk 从 SPChunkResult 原样带入，换算层计数在本层累
// 加。跳过计数器与 SPMG 消息族一一对应，见 plan §7）
struct SPFileStats {
    CMString source_file_;
    // 电容单位换算系数（1 文件单位 = 多少 fF，*C_UNIT 档位 x 刻度——
    // 可追溯，裁定 7）
    double c_unit_ff_per_unit_ = 0.0;
    // —— 解析层计数（SPChunkResult 原样带入）——
    uint64_t net_count_ = 0;                    // 解析网数（收录）
    uint64_t rnet_count_ = 0;                   // 缩减网数（R_NET，裁定 5）
    uint64_t pnet_skip_count_ = 0;              // 物理网段跳过数（裁定 12）
    uint64_t broken_net_count_ = 0;             // 段级破损整网丢弃数
    uint64_t name_map_miss_count_ = 0;          // 映射索引未登记数（裁定 11）
    uint64_t dropped_record_count_ = 0;         // 未登记索引丢弃记录数
    uint64_t multi_value_net_count_ = 0;        // 多值网数（裁定 6）
    uint32_t value_count_max_ = 0;              // 收录网的最大值个数
    uint64_t value_index_fallback_count_ = 0;   // 取值下标回退数
    uint64_t cap_count_ = 0;                    // 电容记录条数（对地 + 耦合
                                                //——解析层 SPChunkResult 原
                                                // 样带入；阶段二补字段：
                                                // summary 逐文件统计的「电
                                                // 容记录数」断言面）
    uint64_t coupling_cap_count_ = 0;           // 耦合电容条数
    uint64_t res_count_ = 0;                    // 电阻记录条数（阶段二补字
                                                // 段——summary 逐文件统计
                                                // 的「电阻记录数」断言面）
    // —— 换算层计数（本层累加）——
    uint64_t hit_count_ = 0;                    // 至少路由到一个分区的网数
    uint64_t net_name_miss_count_ = 0;          // 网名未匹配跳过（SPMG::0001）
    uint64_t pg_net_skip_count_ = 0;            // pg 网跳过（SPMG::0003）
    uint64_t unrouted_net_count_ = 0;           // 未布线/无可挂连接位
                                                //（SPMG::0011；含排除端口
                                                // 条目后无连接位的网——
                                                // 端口不是实例，不进路由）
    uint64_t strip_miss_count_ = 0;             // strip_prefix 未命中
                                                //（SPMG::0002，含剥后余空）
    uint64_t unplaced_instance_count_ = 0;      // 未放置实例跳过（非端口的
                                                // 连接位实例查不到分区——
                                                // 端口条目不是实例，不计此
                                                // 处；汇总计数——本方案无对
                                                // 应消息）
    // 块任务失败数（流程层传入的 0 值占位字段，保留；本层绑定目标未命
    // 中的防御兜底也计此处——同 timing 先例）
    uint64_t failed_chunk_count_ = 0;
    // 切块数（该文件切块计划里的块数；纯头部文件为 0——第 H 节第 5
    // 条）：流程层的已知事实，由汇总任务在 sp_merge_summary 之后按文
    // 件清单填入（多块路径的可观测量——端到端测试断言「文件确实被切成
    // 多块」的依据）
    uint64_t chunk_count_ = 0;

    FLY_SERIALIZE(source_file_, c_unit_ff_per_unit_, net_count_, rnet_count_,
                  pnet_skip_count_, broken_net_count_, name_map_miss_count_,
                  dropped_record_count_, multi_value_net_count_,
                  value_count_max_, value_index_fallback_count_,
                  cap_count_, coupling_cap_count_, res_count_, hit_count_,
                  net_name_miss_count_, pg_net_skip_count_,
                  unrouted_net_count_, strip_miss_count_,
                  unplaced_instance_count_, failed_chunk_count_,
                  chunk_count_)
};

// 统计片段（T2 每块产出；T4 聚合进 SPSummary。逐文件条目通常单条——
// 同文件多块由 T4 按文件名归并）
struct SPStatsDelta {
    CMVector<SPFileStats> files_;

    FLY_SERIALIZE(files_)
};

// T2 逐块解析产物分区分片（temp；T3 每分区一合并任务按 pid 取用）
class SPEntrySlice {
public:
    // 本块所属文件序（编排侧分配；T3 跨文件合并的贡献来源位下标）
    uint32_t file_index_ = 0;
    // 本文件作用域序（编排侧分配：绑定目标相同的文件作用域相同——裁定
    // 8 的判定材料，本层只记录不解释）
    uint32_t scope_index_ = 0;
    CMUnorderedMap<CMPartitionId, SPPartitionSlice> partitions_;
    SPStatsDelta stats_;

    FLY_SERIALIZE(file_index_, scope_index_, partitions_, stats_)
};

// ── T3 落库形态（plan §5.2 网负载轻对象）────────────────────────────

// 单网网负载（首期消费者 power db 的全部所需；每分区自足一份——按连接
// 位分区复制，网跨区时每区各一份）
struct SPNetLoad {
    CMNetId net_id_{};
    double total_cap_reported_ff_ = 0.0;   // 行首自报总电容
    double ground_cap_ff_ = 0.0;           // 明细合计：对地电容
    double coupling_cap_ff_ = 0.0;         // 明细合计：耦合电容（全值）
    // 贡献来源位图：第 i 位 = 第 i 个输入文件（file_index）对该网有贡献
    //（互补相加可追溯；文件数超过 64 时只记前 64 个并计数）
    uint64_t source_mask_ = 0;
    uint8_t flags_ = 0;   // bit0 = 缩减形态（全部贡献都是缩减形态时才置位）

    FLY_SERIALIZE(net_id_, total_cap_reported_ff_, ground_cap_ff_,
                  coupling_cap_ff_, source_mask_, flags_)
};

// 分区网负载容器（PART_{xp}_{yp}.NET_LOAD 正式对象；T3 每分区一合并任
// 务唯一写定）。条目按 net id 升序（确定性 + find_net 二分查找）
class SPPartitionNetLoad {
public:
    CMPartitionId part_id_;
    CMVector<SPNetLoad> items_;   // 按 net id 升序

    // 单网点查（二分；未命中返回 nullptr）
    const SPNetLoad* find_net(CMNetId net_id) const;

    FLY_SERIALIZE(part_id_, items_)
};

// T3 每分区合并统计（出参；SPSummary 全局计数的来源）
struct SPMergeStats {
    // 同作用域的跨文件同网重合（保留首份——裁定 8，SPMG::0007）
    uint64_t same_scope_overlap_count_ = 0;
    // 跨作用域相加（参与的额外贡献条数——SPMG::0007 的互补面）
    uint64_t cross_scope_sum_count_ = 0;
    // 行首值与明细合计不一致（超过容差的网数与最大相对偏差——
    // SPMG::0013，只观测不改数值）
    uint64_t cap_mismatch_count_ = 0;
    double cap_mismatch_max_rel_ = 0.0;
    // 覆盖率统计（裁定 10；分母口径见文件头注释——本区实例连接对象 +
    // 已布线信号网 id 集合，2026-11-24 实施期修正）。统计口径（
    // 2026-11-24 评审裁定）：各项按「分区 × 网」在本区统计后逐分区求
    // 和——跨分区的网与它的连接实例在多个分区各计一次；比率可用，绝
    // 对数不做全局去重（需要全局绝对数时演进为汇总阶段按全局 id 去重）
    uint64_t routed_signal_net_count_ = 0;    // 本区连接里的已布线信号网数
    uint64_t covered_net_count_ = 0;          // 其中合并产物有条目的
    uint64_t uncovered_net_count_ = 0;        // 未覆盖信号网数
    uint64_t uncovered_instance_count_ = 0;   // 所连信号网含未覆盖网的实例数
    uint64_t zero_cap_instance_count_ = 0;    // 所连信号网全部已覆盖且全零
                                              // 电容的实例数
    // 贡献来源位图溢出观测（file_index ≥ 64、位图不置位的文件数——单
    // 次合并内按 file_index 去重；SPSummary 按「分区 × 文件」求和）
    uint64_t source_mask_overflow_count_ = 0;

    FLY_SERIALIZE(same_scope_overlap_count_, cross_scope_sum_count_,
                  cap_mismatch_count_, cap_mismatch_max_rel_,
                  routed_signal_net_count_, covered_net_count_,
                  uncovered_net_count_, uncovered_instance_count_,
                  zero_cap_instance_count_, source_mask_overflow_count_)
};

// ── T4 汇总正式对象（"summary"）────────────────────────────────────

// 汇总（T4 唯一写定）：逐来源文件统计 + 全局计数（覆盖率四项合计、缩
// 减网数、不一致观测、同作用域重合 / 跨作用域相加、绑定目标未命中文
// 件数）+ 内部单位基准声明（电容 fF、电阻 ohm——裁定 7）
class SPSummary {
public:
    CMVector<SPFileStats> files_;   // 按文件名归并（同文件多块计数累加）
    // 覆盖率统计（裁定 10；各分区 SPMergeStats 求和）。统计口径（
    // 2026-11-24 评审裁定）：各项按「分区 × 网」统计后逐分区求和——跨
    // 分区的网与它的连接实例在多个分区各计一次；比率可用，绝对数不做
    // 全局去重（需要全局绝对数时演进为汇总阶段按全局 id 去重）
    uint64_t routed_signal_net_count_ = 0;
    uint64_t covered_net_count_ = 0;
    uint64_t uncovered_net_count_ = 0;
    uint64_t uncovered_instance_count_ = 0;
    uint64_t zero_cap_instance_count_ = 0;
    // 缩减网数（Σ 逐文件 rnet_count_）
    uint64_t rnet_count_ = 0;
    // 行首值与明细合计不一致观测（SPMG::0013；计数求和、最大相对偏差
    // 取最大）
    uint64_t cap_mismatch_count_ = 0;
    double cap_mismatch_max_rel_ = 0.0;
    // 同作用域重合（保留首份）/ 跨作用域相加计数（裁定 8）
    uint64_t same_scope_overlap_count_ = 0;
    uint64_t cross_scope_sum_count_ = 0;
    // 贡献来源位图溢出的文件数（各分区 SPMergeStats 求和——「分区 ×
    // 文件」口径；单次合并内已按 file_index 去重）
    uint64_t source_mask_overflow_count_ = 0;
    // 绑定目标未命中被跳过的文件数（flow 侧判定传入，原样落字段）
    uint64_t invalid_binding_count_ = 0;
    // 多成员电气网数（2026-11-24 评审修正：设计级的数——含两个及以上
    // 成员的电气网数 = 设计库并查集对象的 class_count − dangling_count
    //（等价类总数减去悬空的单成员类数——class_count 含悬空 port 网的
    // 单成员类），整个设计一个数。flow 根任务读并查集对象后随参数传
    // 入，汇总任务原样落字段——不经合并统计聚合）
    uint64_t electrical_net_count_ = 0;
    // 内部单位基准声明（裁定 7：1 内部电容单位 = 1 fF、1 内部电阻单位
    // = 1 ohm——文件数值已按头档位换算，消费侧无需二次换算）
    double internal_cap_unit_ff_ = 1.0;
    double internal_res_unit_ohm_ = 1.0;

    FLY_SERIALIZE(files_, routed_signal_net_count_, covered_net_count_,
                  uncovered_net_count_, uncovered_instance_count_,
                  zero_cap_instance_count_, rnet_count_, cap_mismatch_count_,
                  cap_mismatch_max_rel_, same_scope_overlap_count_,
                  cross_scope_sum_count_, source_mask_overflow_count_,
                  invalid_binding_count_, electrical_net_count_,
                  internal_cap_unit_ff_, internal_res_unit_ohm_)
};

// ── T2 换算上下文（design db 快照组装；TMDesignContext 同构）────────

// design db 快照共享注入上下文（Python 编排侧快照对象逐个注入；全部
// CMSharedPtr 持有——生命周期由本类计数管理，业务层零裸指针）。两维度
// name mapper 于 set_design 时构造（经 aliasing shared_ptr 持 design 内
// 层级树，生命周期自持）；运行时构件（mapper）不序列化。本层只做网名
// 换算（比波形侧精简——无活动/settings 物化材料）。
class SPDesignContext {
public:
    SPDesignContext() = default;

    // design 快照（层级树 + cell/pin hasher）；构造两维度 mapper
    void set_design(CMSharedPtr<DSDesign> design);
    // mapper 重建（set_design 的重建半步——hasher 注入关系重挂）
    void rebuild_mappers();
    // 块 local 名空间伴生对象（DSBlockNames_<i>，def 序逐个注入两 mapper
    // + hasher 索引）
    void add_block_names(CMSharedPtr<const DSBlockNames> names);
    // id → partition 反向映射（INST 段表 + 全部非空段对象——分区路由唯
    // 一依赖）
    void set_inst_id_map(CMSharedPtr<DSIdPartitionIndex> index);
    void add_inst_segment(CMSharedPtr<DSIdPartitionSegment> segment);
    // 全局 pg 网 id 集（§2.2 pg 网跳过判定）
    void set_pg_nets(CMSharedPtr<DSPgNetSet> pg_nets);
    // 分区 NETS 对象（网连接表定位；键取对象 part_id_）
    void add_partition_nets(CMSharedPtr<DSPartitionNets> nets);
    // 分区实例连接对象（覆盖率统计材料——第 H 节第 4 条：本区连接位
    // 实例分母；对象无 part_id_ 字段，注入口显式携带 pid）
    void add_partition_inst_connections(
        CMPartitionId pid, CMSharedPtr<DSPartInstConnections> conns);
    // 跨块连接归并结果（design db 的 "net_union" 对象——电气网分组依
    // 据，2026-11-24 用户裁定；每分区合并任务注入）
    void set_net_union(CMSharedPtr<DSNetUnion> net_union);

    const DSDesign& design() const { return *design_; }
    const DSInstanceNameMapper& inst_mapper() const { return inst_mapper_; }
    const DSNetNameMapper& net_mapper() const { return net_mapper_; }

    // 实例全局 id → 分区 id（INST 段表 → 段对象；未命中/空洞返回无效
    // id——调用方按未放置计数）
    CMPartitionId locate_instance_partition(CMInstanceId inst_id) const;
    // 全部快照分区 NETS 对象（网条目遍历口——信号网 NETS 全量补全，任
    // 一分区副本都有该网完整连接表，同 timing/waveform 先例）
    const CMUnorderedMap<uint32_t, CMSharedPtr<DSPartitionNets>>&
    all_partition_nets() const {
        return part_nets_;
    }
    // 全部快照分区实例连接对象（覆盖率统计材料遍历口——按注入口 pid 索
    // 引）
    const CMUnorderedMap<uint32_t, CMSharedPtr<DSPartInstConnections>>&
    all_inst_connections() const {
        return inst_conns_;
    }
    const DSPgNetSet* pg_nets() const {
        return pg_nets_ == nullptr ? nullptr : pg_nets_.get();
    }
    // 电气网分组依据（跨块连接归并结果；未注入返回 nullptr——调用方按
    // 「不在表 = 单成员组」的平展常态处理）
    const DSNetUnion* net_union() const {
        return net_union_ == nullptr ? nullptr : net_union_.get();
    }
    // 注入对象共享拷贝（Python 观察口——引用计数共享，生命周期自持）
    CMSharedPtr<DSNetUnion> net_union_share() const { return net_union_; }
    // 块定义 instance/net hasher（按 block cell id；未注入返回空）
    CMSharedPtr<const DSInstanceNameHasher> inst_hasher_of(
        CMCellId block_cell_id) const;
    CMSharedPtr<const DSNetNameHasher> net_hasher_of(
        CMCellId block_cell_id) const;

private:
    // —— 快照数据本体（逐对象临时对象传输，ctx 由 T2 任务在 worker 端
    //    组装；hasher map 存非 const 形态——bitsery 序列化 shared 分支
    //    不支持 const 元素，对外观察口返回 const 化）——
    CMSharedPtr<DSDesign> design_;
    CMUnorderedMap<CMCellId, CMSharedPtr<DSInstanceNameHasher>> inst_hashers_;
    CMUnorderedMap<CMCellId, CMSharedPtr<DSNetNameHasher>> net_hashers_;
    CMSharedPtr<DSIdPartitionIndex> inst_index_;
    CMUnorderedMap<uint64_t, CMSharedPtr<DSIdPartitionSegment>> inst_segments_;
    CMSharedPtr<DSPgNetSet> pg_nets_;
    CMUnorderedMap<uint32_t, CMSharedPtr<DSPartitionNets>> part_nets_;
    CMUnorderedMap<uint32_t, CMSharedPtr<DSPartInstConnections>> inst_conns_;
    CMSharedPtr<DSNetUnion> net_union_;

    // —— 运行时构件（不序列化；set_design/rebuild_mappers 重建）——
    DSInstanceNameMapper inst_mapper_;
    DSNetNameMapper net_mapper_;
};

// ── T2 逐块换算 ─────────────────────────────────────────────────────

// 单块执行体（plan §6 T2 口）：对解析层产物（SPChunkResult——本块全部
// 网段）逐网：名字换算（块绑定三形态 + strip_prefix + 归一 + 转义容错，
// §2.2）→ 全局网 id → pg 判定（跳过 + 计数）→ 分区路由（分区 NETS 连
// 接位实例的 primary 分区枚举；网跨区时每区各存一份条目——每区自足，
// 同 waveform 复制范围机制；BLOCK_CELL 建库期复制对全部实例化各算一
// 次）→ 各分区 SPEntrySlice。
//
// 计数口径（逐 SPEF 网段）：名字未命中 → net_name_miss_count；pg →
// pg_net_skip_count；分区 NETS 无此网或无可挂连接条目（driver/receiver）
// → unrouted_net_count；连接位实例未放置 → unplaced_instance_count、
// 跳过该条目；条目全部未放置（无一产出）→ 另计 unrouted_net_count；
// 至少路由到一个分区 → hit_count。
//
// 绑定目标未命中（block_inst 路径 / block_cell 名在 design db 里找不
// 到——入口校验后环境漂移的防御场景）：同 timing 先例按块失败兜底，
// failed_chunk_count 计 1、网全部不处理、空分片照常产出（不 raise）。
SPEntrySlice sp_convert_chunk(const SPDesignContext& ctx,
                              const SPFileHeader& header,
                              const SPChunkResult& parsed, uint32_t file_index,
                              uint32_t scope_index, const SPFileBinding& binding,
                              const CMString& source_file);

// ── T3 每分区合并 ───────────────────────────────────────────────────

// 每分区合并（T3 每分区一任务）：对分区 pid，把各分片里属于它的网条目
// 按网 id 归组后合并（slices 为观察指针集，调用方按「文件序 × 块序」
// 排列——「保留首份」的判定依此序；同 tm_merge_partition 入参约定）：
//   - 同一作用域内出现多条（同一网被同作用域的多个文件描述）：保留首
//     份（按文件序、块序最先出现的），其余丢弃，计
//     same_scope_overlap_count（裁定 8）；
//   - 不同作用域的贡献：三个电容值相加、贡献来源位图按位或，计
//     cross_scope_sum_count（计数单位 = 参与的额外贡献条数）；缩减形态
//     标记 = 全部贡献都是缩减形态时才置位；
//   - 电气网分组合并（2026-11-24 用户裁定：flatten 后经层次端口逻辑相
//     连的各段网是同一张电气网）：单网编号内合并（前两条）之后，再把
//     同一张电气网里不同网编号（各段）的贡献相加——做法 = 先从全部
//     分片的全部条目（全部分区）算出「全局的逐网编号负载表」（同作用
//     域保留首份、跨作用域相加；同作用域的跨分区复制是同一贡献的分区
//     副本，保留首份无损），再按并查集组（上下文注入的 DSNetUnion）
//     求和。条目落点（2026-11-24 评审修正）：凡本区网表（分区 NETS 对
//     象——信号网全量补全副本）里有的信号网，只要它所在的电气网有负
//     载，本区就有这张电气网的条目（按成员网编号各存一份、数值同为一
//     张电气网的合计——下游按手里任意一段的网编号查到的都是整张网的
//     负载；成员网自己有没有被寄生文件描述不影响）。电源地网在 NETS_PG
//     对象、不在本表，天然排除。单成员组（平展常态）不进组求和，条目
//     的数值与分层合并逐位一致（浮点相加顺序不重排）；但补全条目那
//     一步不依赖并查集——平展设计里布线几何落在本区网表、连接位实例
//     全在别的分区的网在本区也会多出一条补全条目；
//   - 行首值与明细合计的一致性（只观测，不改数值）：对合并后非缩减形
//     态的网，差额 = |total − (ground + coupling)|、基准 =
//     max(total, ground + coupling)，差额 > max(1e-3 fF, 1e-4 × 基准)
//     时计 cap_mismatch_count 并更新最大相对偏差（差额 ÷ 基准；基准为
//     0 时相对偏差记 0）——SPMG::0013；
//   - 覆盖率统计（裁定 10；分母口径见文件头注释——材料 = 本区实例连
//     接对象 PART_{xp}_{yp}.INST_CONNECTIONS + 已布线信号网 id 集合
//    〔上下文全部分区 NETS 对象里出现过的网 id 的并集〕；键 0 整键排
//     除）；一张网「已覆盖」= 它所在的电气网有负载（组任一成员被描述
//     即在组求和表——不受「本区条目只含本区成员编号」限制）。本区实
//     例连接对象缺失时各项记 0，不报错。
//
// 结果按 net id 升序；没有分片时产出空对象（part_id_ 置位、条目为空）。
SPPartitionNetLoad sp_merge_partition(const SPDesignContext& ctx,
                                      const CMVector<const SPEntrySlice*>& slices,
                                      CMPartitionId pid, SPMergeStats& stats_out);

// ── T4 汇总 ─────────────────────────────────────────────────────────

// 汇总任务（T4）：全部块统计片段按文件名归并逐文件表（同文件多块：计
// 数累加、value_count_max 取最大、首条建档）+ 全局计数（覆盖率四项合
// 计 / 缩减网数 / 不一致观测 / 同作用域重合 / 跨作用域相加）+ 绑定目
// 标未命中被跳过的文件数（flow 侧判定传入）+ 多成员电气网数（设计级：
// flow 根任务读并查集对象的组数随参传入，原样落字段）+ 内部单位基准
// 声明。
SPSummary sp_merge_summary(const CMVector<const SPStatsDelta*>& deltas,
                           uint64_t invalid_binding_count,
                           const CMVector<SPMergeStats>& merge_stats,
                           uint64_t electrical_net_count);

}  // namespace fly
