#pragma once

// =============================================================================
// waveform db 换算/路由/合并层（plan docs/emir/waveform-db-plan.md
// §2.1/§5.2/§5.3/§6/§7/§8，2026-10-12 全裁定；签名与结构形态对齐 timing
// db tm_partition 先例——TMDesignContext 跨 db 直读组装 / convert_chunk /
// merge_partition / merge_summary 同构）。
//
// 三层结构（对齐 plan §6 直接任务链，裁定 V 重排后口径）：
//   WFEntrySlice            T2 逐块解析产物分区分片（temp）：**纯事件分
//                           片**——本块触及各分区的波形片段（身份键 →
//                           逐位事件列表）+ 统计挂载表（(inst, pin) →
//                           身份键 + 位集）。无统计值、无等值过滤（裁定
//                           V-2：统计与过滤挪 T3 合并端）
//   正式对象                T3/T4 产物：WFPartitionWaveform（PART_WAVEFORM
//                           对象）+ WFPartitionActivity（PART_ACTIVITY 对
//                           象）+ WFSummary（"summary" 对象）
//
// 波形去重（第二轮裁定 4）：同一份翻转数据（同一身份键的值序列）分区
// 内只存一份 WFWaveform；身份键 = net id（§7 形态①——同网全部连接位
// 共享）或 (instance, pin)（形态②——单引脚独享）；引脚统计以
// waveform_id 引用同一份，哨兵 kNoWaveform = 无波形（settings-only 条
// 目 / VCD 模式零事件命中）。跨分区不共享（连接位分属多区时每区各存
// 本区一份，分区局部 id 空间互不相干——plan §5.2）。
//
// 名字换算（§7，T2 任务内 C++）：消费 design db 快照（DSDesign 层级树 +
// pin hasher、DSBlockNames_<i> 块 local 名空间、id_partition_map INST 段
// 表与段对象、pg_nets 全局集、分区 NETS 对象）——上下文 WFDesignContext
// 以 CMSharedPtr 共享注入（§16 业务层全禁裸指针；与 TMDesignContext 同
// 构的 waveform 自有 context 类，不改 timing 模块）。
//
// 根 scope 剥除：VCD var 全路径首段 = 设计名（DSHierTree design_name_，
// 2026-09-16 命名裁定 design db 层级路径不含设计名前缀）时剥去。
//
// 多文件合并语义（§2.1 已裁定，按文件绑定形态分派）：场景 a（纯路径文
// 件）= 事件按文件序偏移拼接（逐文件首末时间戳嗅探，各文件独立 [0,..]
// 形态则累计前文件时长作偏移）+ 统计直和；场景 b/混合（块绑定文件重
// 合）= 后文件整体覆盖先文件 + WFMG::0006 计数（与 lib/timing「保留首
// 份」相反，本库显式裁定后优先）。
//
// id 体系：全部 CM 族强类型（emir_ids.h）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <emir/common/cpp/emir_ids.h>
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_id_map.h>
#include <emir/design/cpp/ds_name_mapper.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/waveform/cpp/wf_types.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace fly {

// ── 波形身份键（§7 两形态；中间形态经分区分片传递，T3 合并的分组键）──

// 波形身份键：形态①（is_net_ = true）= net 全局 id——同网全部连接位共
// 享一份波形；形态②（is_net_ = false）= (instance, pin)——单引脚独享。
// 强类型 id 组合键（禁裸整型成员）。
struct WFWaveformKey {
    bool is_net_ = false;
    CMNetId net_id_{};
    CMInstanceId inst_id_{};
    CMPinId pin_id_{};

    bool operator==(const WFWaveformKey& o) const = default;

    FLY_SERIALIZE(is_net_, net_id_, inst_id_, pin_id_)
};

}  // namespace fly

template <>
struct std::hash<fly::WFWaveformKey> {
    size_t operator()(const fly::WFWaveformKey& k) const {
        const size_t h1 = std::hash<uint64_t>{}(k.net_id_.value());
        const size_t h2 = std::hash<uint64_t>{}(k.inst_id_.value());
        const size_t h3 = std::hash<uint32_t>{}(k.pin_id_.value());
        return k.is_net_ ? h1 ^ (h2 << 1) ^ (h3 << 2)
                         : (h1 << 3) ^ (h2 << 1) ^ h3;
    }
};

namespace fly {

// 身份键全序（T3 waveform_id 分配序 = 本序）：形态①（net）先行按
// net_id_ 升序；形态②（inst pin）后行按 (inst_id_, pin_id_) 字典序升
// 序。waveform_id 是持久化引用键，分配序必须与分片 map 迭代序解耦——
// 否则 id 标号随编译器/标准库漂移，跨构建产物不可复现。
inline bool wf_waveform_key_less(const WFWaveformKey& a,
                                 const WFWaveformKey& b) {
    if (a.is_net_ != b.is_net_) {
        return a.is_net_;  // net 形态先行
    }
    if (a.is_net_) {
        return a.net_id_.value() < b.net_id_.value();
    }
    if (a.inst_id_.value() != b.inst_id_.value()) {
        return a.inst_id_.value() < b.inst_id_.value();
    }
    return a.pin_id_.value() < b.pin_id_.value();
}

// 无波形哨兵（WFPinActivity.waveform_id_；plan §5.2 裁定值）
inline constexpr uint32_t kWfNoWaveform = UINT32_MAX;

// ── 落库形态（plan §5.2）────────────────────────────────────────────

// 单条波形：一份去重的事件序列（时间升序；向量拆位后仅变化位有条目；
// 同刻多事件 = 向量多位同刻变化——时间非严格单调）
struct WFWaveform {
    uint32_t waveform_id_ = 0;   // 分区局部 id（T3 合并时分配；= 容器数组
                                 // 下标；分配序 = 身份键全序
                                 // wf_waveform_key_less——跨构建可复现）
    uint64_t t_start_fs_ = 0;    // 首事件时刻（零事件波形 = 0）
    uint64_t t_end_fs_ = 0;      // 末事件时刻
    CMVector<WFEvent> events_;

    FLY_SERIALIZE(waveform_id_, t_start_fs_, t_end_fs_, events_)
};

// 分区波形容器（PART_{xp}_{yp}.WAVEFORM 正式对象；T3 每分区一合并任务
// 唯一写定）。波形 id = 数组下标（O(1) 直索引）；settings-only 模式 =
// 空数组。
class WFPartitionWaveform {
public:
    CMPartitionId part_id_;
    CMVector<WFWaveform> waveforms_;

    size_t waveform_count() const { return waveforms_.size(); }

    FLY_SERIALIZE(part_id_, waveforms_)
};

// 逐引脚活动：统计原始量 + 波形引用（引替复制——同网连接位统计值同源
// 相同，不引波形反查）。rise/fall 严格口径 = 仅 0→1 / 1→0，x/z 参与的
// 转换不计（第二轮裁定 7）。
struct WFPinActivity {
    CMPinId pin_id_;
    uint32_t waveform_id_ = kWfNoWaveform;   // 引用本区 WFPartitionWaveform
                                             //（哨兵 = 无波形）
    uint64_t t0_fs_ = 0;
    uint64_t t1_fs_ = 0;
    uint64_t tx_fs_ = 0;
    uint64_t tz_fs_ = 0;
    uint64_t rise_count_ = 0;
    uint64_t fall_count_ = 0;

    FLY_SERIALIZE(pin_id_, waveform_id_, t0_fs_, t1_fs_, tx_fs_, tz_fs_,
                  rise_count_, fall_count_)
};

// 逐实例活动（引脚稀疏表；形态对齐 TMInstanceTiming——实例引脚少，
// 紧凑 vector 优于 map）
struct WFInstanceActivity {
    CMVector<WFPinActivity> pins_;

    // 按 pin id 线性查（实例引脚个位数~几十，线性即最快）
    const WFPinActivity* find_pin(CMPinId pin_id) const;

    FLY_SERIALIZE(pins_)
};

// 分区统计容器（PART_{xp}_{yp}.ACTIVITY 正式对象；键 = 实例全局 id）
class WFPartitionActivity {
public:
    CMPartitionId part_id_;
    CMUnorderedMap<CMInstanceId, WFInstanceActivity> items_;

    size_t size() const { return items_.size(); }

    FLY_SERIALIZE(part_id_, items_)
};

// ── 统计（plan §5.2 summary + §9 消息族计数源；C++ 侧计数器，消息由
//    flow 侧读汇总一次发出——dev-rules §7 兜底语义）───────────────────

// 逐文件统计（来源可追溯，dev-rules §7；WFSummary.files_ 条目。跳过计
// 数器与 WFMG 消息族一一对应，见 plan §9）
struct WFFileStats {
    CMString source_file_;
    double timescale_sec_ = 1e-9;    // 头声明原值换算秒
    uint64_t t_start_fs_ = 0;        // 文件时间范围（多文件偏移对齐依据；
                                     // 块级分片 = 块内首末时间戳，T4 归并
                                     // 取首块起点/末块终点）
    uint64_t t_end_fs_ = 0;
    uint64_t event_count_ = 0;       // 拆位后事件总数（解析级，含未命中）
    uint64_t hit_count_ = 0;         // 换算命中条目数（var 挂到 ≥1 (inst,
                                     // pin)）
    // net 名未匹配跳过（WFMG::0001——形态①未命中即计）
    uint64_t net_name_miss_count_ = 0;
    // 实例/引脚名未匹配跳过（WFMG::0002——形态②未命中；双未命中条目
    // 0001/0002 各计一次）
    uint64_t inst_pin_miss_count_ = 0;
    // 网条目无连接位/悬空（WFMG::0003；含 design db NETS 无此网）
    uint64_t dangling_net_count_ = 0;
    // pg 网条目跳过（§7.5 防御口径）
    uint64_t pg_net_skip_count_ = 0;
    // 未放置实例跳过（WFMG::0008——design db 无 primary 分区副本）
    uint64_t unplaced_instance_count_ = 0;
    // strip_prefix 未命中跳过（WFMG::0010，含剥后余空）
    uint64_t strip_miss_count_ = 0;
    // 不支持构造观测（WFMG::0012——real/event 值行；声明级计数在文件入
    // 口解析侧）
    uint64_t unsupported_count_ = 0;
    // 向量位宽失配观测（WFMG::0013——按 var 计，不 fatal）
    uint64_t width_mismatch_count_ = 0;
    // 块语法破损跳过数（本文件的字节块；单块失败兜底——空分片照常产出）
    uint64_t failed_chunk_count_ = 0;

    FLY_SERIALIZE(source_file_, timescale_sec_, t_start_fs_, t_end_fs_,
                  event_count_, hit_count_, net_name_miss_count_,
                  inst_pin_miss_count_, dangling_net_count_,
                  pg_net_skip_count_, unplaced_instance_count_,
                  strip_miss_count_, unsupported_count_,
                  width_mismatch_count_, failed_chunk_count_)
};

// 统计片段（T2 每块产出；T4 聚合进 WFSummary。plan §5.3 统计 delta）
struct WFStatsDelta {
    // 逐文件计数（通常单条；同文件多块由 T4 按文件名归并）
    CMVector<WFFileStats> files_;

    // —— 全局计数（T4 直和聚合）——
    // 多驱动网观测（网 driver 位连接条目 > 1，按换算命中次观测）
    uint64_t multi_driver_net_count_ = 0;
    // x/z 态事件观测（含 $dumpoff 合成置位；plan §5.2 全局「x/z 观测」）
    uint64_t x_z_event_count_ = 0;

    FLY_SERIALIZE(files_, multi_driver_net_count_, x_z_event_count_)
};

// settings 元数据（plan §4/§5.2：default 不逐引脚物化——作为元数据随
// summary 落库，消费侧展开；x_z_handling = settings 原文透传
//（denominator_include / exclude / hold_previous））
struct WFSettingsMeta {
    double default_toggle_rate_ = 0.0;          // 次/秒（rise+fall 合计）
    double default_static_probability_ = 0.5;   // 逻辑 1 概率
    double duration_sec_ = 1.0;                 // 统计窗口时长（秒）
    CMString x_z_handling_ = "denominator_include";

    FLY_SERIALIZE(default_toggle_rate_, default_static_probability_,
                  duration_sec_, x_z_handling_)
};

// 汇总正式对象（"summary"；T4 唯一写定）：逐来源文件统计 + 全局计数 +
// settings 元数据
class WFSummary {
public:
    CMVector<WFFileStats> files_;
    // 全局 DURATION（Σ文件时长——率/概率归一化唯一基准）
    uint64_t total_duration_fs_ = 0;
    uint64_t total_event_count_ = 0;
    uint64_t total_hit_count_ = 0;
    // 名字未匹配家族（0001 / 0002）
    uint64_t net_name_miss_count_ = 0;
    uint64_t inst_pin_miss_count_ = 0;
    uint64_t dangling_net_count_ = 0;             // 0003
    uint64_t pg_net_skip_count_ = 0;
    uint64_t unplaced_instance_count_ = 0;        // 0008
    uint64_t strip_miss_count_ = 0;               // 0010
    uint64_t unsupported_count_ = 0;              // 0012
    uint64_t width_mismatch_count_ = 0;           // 0013
    // 波形数（去重后，Σ分区波形实体数——T3 侧计数传入）
    uint64_t waveform_count_ = 0;
    // 多文件重合覆盖计数（WFMG::0006——场景 b/混合后文件覆盖，T3 传入；
    // 计数单位 = 被覆盖的波形身份实体）
    uint64_t overlap_override_count_ = 0;
    // 场景 a 时间轴观测（WFMG::0007——偏移对齐后仍非单调的事件数，T3 传入）
    uint64_t time_anomaly_count_ = 0;
    uint64_t x_z_event_count_ = 0;
    uint64_t multi_driver_net_count_ = 0;
    // 块语法破损总数（全部文件全块失败 → flow 侧 WFMG::0009 fatal）
    uint64_t failed_chunk_count_ = 0;
    // 绑定目标未命中被跳过的文件数（flow 侧判定传入，merge 侧原样落字段
    // ——同 tm_merge_summary 先例）
    uint64_t invalid_binding_count_ = 0;
    // 混合模式覆盖条目数（裁定 IV-2：VCD + settings.activity.overrides
    // 并存——T3 每分区合并产物上被覆盖/新建的 (inst, pin) 统计条目总数，
    // T3 侧计数传入）
    uint64_t override_entry_count_ = 0;
    // settings 元数据（消费侧展开依据）
    double default_toggle_rate_ = 0.0;
    double default_static_probability_ = 0.5;
    double duration_sec_ = 1.0;
    CMString x_z_handling_;

    FLY_SERIALIZE(files_, total_duration_fs_, total_event_count_,
                  total_hit_count_, net_name_miss_count_,
                  inst_pin_miss_count_, dangling_net_count_,
                  pg_net_skip_count_, unplaced_instance_count_,
                  strip_miss_count_, unsupported_count_,
                  width_mismatch_count_, waveform_count_,
                  overlap_override_count_, time_anomaly_count_,
                  x_z_event_count_, multi_driver_net_count_,
                  failed_chunk_count_, invalid_binding_count_,
                  override_entry_count_,
                  default_toggle_rate_, default_static_probability_,
                  duration_sec_, x_z_handling_)
};

// ── 中间形态（plan §5.3 temp 对象，链上任务自清理）─────────────────

// 统计挂载项（T2 每块产出；T3 据四级回落定最终波形引用后计算统计值——
// 裁定 V-2：T2 无统计职责，条目只携带身份归组材料）：(inst, pin) 挂载
// 的波形身份键 + 位集（统计 = 位集各位于键轨迹统计的直和）。
struct WFStatsItem {
    CMInstanceId inst_id_{};
    CMPinId pin_id_{};
    WFWaveformKey waveform_key_{};
    CMVector<uint32_t> bits_;   // 本挂载归属键轨迹的位下标集

    FLY_SERIALIZE(inst_id_, pin_id_, waveform_key_, bits_)
};

// T2 逐块产物单分区切片：本块触及本分区的波形片段（身份键 → 逐位事件
// 列表，各位时间升序——**未经等值过滤**，过滤在 T3）+ 统计挂载表。同
// 块内同 (inst, pin) 重复挂载 = 别名双挂防御，保留首份；同身份键重复
// 波形片段 = 多 var 共轨（同 var 逐位并入/位名分别命中），按位归并拼
// 接。
class WFPartitionSlice {
public:
    CMPartitionId part_id_;
    // 键 → 逐位事件列表（外层下标 = 位序，与挂载 bits_ 对齐；列表内时
    // 间升序）
    CMUnorderedMap<WFWaveformKey, CMVector<CMVector<WFEvent>>> waveforms_;
    // 键 = inst << 32 | pin（组合键值域论证同 tm_partition 先例——
    // instance id 实际量级 10⁹ << 2^32）
    CMUnorderedMap<uint64_t, WFStatsItem> stats_;

    FLY_SERIALIZE(part_id_, waveforms_, stats_)
};

// T2 逐块解析产物分区分片（temp；T3 每分区一合并任务按 pid 取用）
class WFEntrySlice {
public:
    // 本块所属文件序（编排侧分配；T3 跨文件合并语义 / T4 按文件归并的
    // 分组键）
    uint32_t file_index_ = 0;
    // 本文件绑定形态（§2.1 场景 a/b 分派依据——建库入口已知，不经运行时
    // 猜测）
    uint32_t binding_kind_ = 0;   // WFFileBindingKind 值域（枚举定型存储，
                                  // 32 位字段承载 3 值域）
    CMUnorderedMap<CMPartitionId, WFPartitionSlice> partitions_;
    WFStatsDelta stats_;

    FLY_SERIALIZE(file_index_, binding_kind_, partitions_, stats_)
};

// ── 文件绑定形态（§2 输入三形态；TMFileBinding 同构的 waveform 侧副
//    本——不引 timing 模块头，worker 端 Python 侧构建后直传 C++，无序
//    列化需求）────────────────────────────────────────────────────────

enum class WFFileBindingKind : uint8_t {
    PATH = 0,        // 纯路径（条目名 = 全层级路径；§2.1 场景 a）
    BLOCK_INST = 1,  // block_inst（条目名 = 该块实例内局部名）
    BLOCK_CELL = 2,  // block_cell（条目名 = 该 cell 定义内局部名，定义级
                     // 对全部实例化成立——建库期复制）
};

struct WFFileBinding {
    WFFileBindingKind kind = WFFileBindingKind::PATH;
    CMString block_inst;
    CMString block_cell;
    CMString strip_prefix;
};

// settings 逐实体覆盖（plan §4 settings.activity.overrides 条目；键 =
// inst pin 层级路径名或 net 名——net 名经 §7 形态①推导展开）
struct WFActivityOverride {
    CMString name_;
    double toggle_rate_ = 0.0;           // 次/秒（rise+fall 合计）
    double static_probability_ = 0.5;    // 逻辑 1 概率
    double rise_fraction_ = 0.5;         // 上升沿占比

    FLY_SERIALIZE(name_, toggle_rate_, static_probability_, rise_fraction_)
};

// ── T2 换算上下文（design db 快照组装；TMDesignContext 同构）────────

// design db 快照共享注入上下文（Python 编排侧快照对象逐个注入；全部
// CMSharedPtr 持有——生命周期由本类计数管理，业务层零裸指针）。两维度
// name mapper 于 set_design 时构造（经 aliasing shared_ptr 持 design 内
// 层级树——评审 B-5a 同款，生命周期自持）。
class WFDesignContext {
public:
    WFDesignContext() = default;

    // design 快照（层级树 + cell/pin hasher + 分区表）；构造两维度 mapper
    void set_design(CMSharedPtr<DSDesign> design);
    // mapper 重建（set_design 的重建半步——hasher 注入关系重挂）
    void rebuild_mappers();
    // 块 local 名空间伴生对象（def 序逐个注入两 mapper + 块 hasher 索引）
    void add_block_names(CMSharedPtr<const DSBlockNames> names);
    // id → partition 反向映射（INST 段表 + 全部非空段对象——分区路由唯
    // 一依赖）
    void set_inst_id_map(CMSharedPtr<DSIdPartitionIndex> index);
    void add_inst_segment(CMSharedPtr<DSIdPartitionSegment> segment);
    // 全局 pg 网 id 集（§7.5 pg 条目跳过判定）
    void set_pg_nets(CMSharedPtr<DSPgNetSet> pg_nets);
    // 分区 NETS 对象（网条目连接位定位；键取对象 part_id_）
    void add_partition_nets(CMSharedPtr<DSPartitionNets> nets);

    const DSDesign& design() const { return *design_; }
    // 设计名（根 scope 剥除判定基准——DSHierTree design_name_）
    CMString design_name() const {
        return design_->get_hier_tree().get_design_name();
    }
    const DSInstanceNameMapper& inst_mapper() const { return inst_mapper_; }
    const DSNetNameMapper& net_mapper() const { return net_mapper_; }

    // 实例全局 id → 分区 id（INST 段表 → 段对象；未命中/空洞返回默认
    // 哨兵——调用方按未放置计数）
    CMPartitionId locate_instance_partition(CMInstanceId inst_id) const;
    // 全部快照分区 NETS 对象（网条目遍历口——信号网 NETS 全量补全，任一
    // 分区副本都有该网完整连接表；T2 逐对象查首个命中，同 timing 先例）
    const CMUnorderedMap<uint32_t, CMSharedPtr<DSPartitionNets>>&
    all_partition_nets() const {
        return part_nets_;
    }
    const DSPgNetSet* pg_nets() const {
        return pg_nets_ == nullptr ? nullptr : pg_nets_.get();
    }
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

    // —— 运行时构件（不序列化；set_design/rebuild_mappers 重建）——
    DSInstanceNameMapper inst_mapper_;
    DSNetNameMapper net_mapper_;
};

// ── T2 逐块换算 ─────────────────────────────────────────────────────

// 单块执行体（plan §6 T2 口，裁定 V 重排后）：读块字节区间
// [chunk.start_, chunk.end_) 流式扫描事件记录（**纯事件**——不过滤值等
// 价重复行、不做四态时长统计，均挪 T3；词法与解析器共用 wf_types 公共
// 单点）→ 名字换算（§7 两形态 + §7.2 轨道身份归并 + 向量位级匹配 + 块
// 绑定三形态 + strip_prefix + 根 scope 剥除）→ 分区路由（INST 段表逐实
// 例 primary 分区；**引脚身份波形按 §8 复制范围**路由——自身实例分区 +
// 所连网全部连接位实例分区，裁定 V-5）→ 各分区 WFEntrySlice（纯事件分
// 片：逐位事件 + 统计挂载表）。
//
// 轨道身份归并（§7.2，裁定 V-1）：同一 id 码轨道多名中有网名命中（形态
// ①）时整轨道统一立网身份（一份波形实体挂该网全部连接位）；轨道内其它
// 名字（实例引脚名）不另立身份、统计挂网身份引用。各文件换算时局部成
// 立（各文件 var 表独立），跨文件同一波形身份键由 T3 天然归并。
//
// 单块失败兜底（plan §6 范式 (b)）：块文本不可读 → 空分片 +
// failed_chunk 计数照常产出（依赖链保持满足）。绑定目标未命中 = 入口
// 校验后环境漂移的防御场景，同款兜底。
WFEntrySlice wf_convert_chunk(const WFDesignContext& ctx,
                              const CMString& path, const WFFileChunk& chunk,
                              const WFVarTable& vars, uint64_t timescale_fs,
                              const WFFileBinding& binding,
                              uint32_t file_index);

// ── T3 / T4 合并 ────────────────────────────────────────────────────

// 每分区合并（T3 每分区一任务，裁定 V-2 五步重排）：
//   ① 波形拼接：同身份键分片按文件序/时间序逐位拼接（场景 a 偏移对齐
//      §2.1）或后文件整体覆盖（场景 b/混合 §2.1）→ 完整逐位事件序列；
//   ② 等值过滤：完整序列上统一丢弃与前值相同的重复行（首事件无前值不
//      过滤——README 特征 4/5 幂等口径）；
//   ③ 统计计算：窗口 = 贡献文件偏移对齐后的时间范围并集（块界概念于统
//      计中不存在——多块拼接 == 整读单块）；序列初值 = 轨道初态 X
//     （$dumpvars 初值行已是首事件；无初值行的未转储初值段归 X——README
//      xz.vcd n_seed TX 口径），前导段 + 相邻事件间隔 + 尾段覆盖全程；
//      rise/fall 仅 0→1/1→0（裁定 7）；
//   ④ 四级回落解析（§7.3，裁定 V-4）：本区每引脚定最终波形引用——引脚
//      自身轨道 → 所连网轨道 → 同网 output 引脚轨道（多个取连接表条目
//      序第一个）→ 连接表第一个有波形引脚（不区分方向）；四级全无 =
//      kNoWaveform 哨兵（统计全零）。统计随最终引用波形计算；
//   ⑤ waveform_id 分配（身份键全序 wf_waveform_key_less——P1-3 跨构建
//      可复现，= 容器数组下标）+ 统计绑最终引用。
//
// 出参 overlap_override_count = WFMG::0006 计数（被覆盖的波形身份实体
// 数）；time_anomaly_count = WFMG::0007 计数（偏移对齐后仍非单调的事
// 件数）。slices 为观察指针集（借引用不拷贝，调用方按文件序 × 块序排
// 列——同 tm_merge_partition 入参约定）。ctx = design db 快照上下文（④
// 的 NETS 连接表判定材料——本地可解）。
void wf_merge_partition(const WFDesignContext& ctx,
                        const CMVector<const WFEntrySlice*>& slices,
                        CMPartitionId pid, WFPartitionWaveform& waveform_out,
                        WFPartitionActivity& activity_out,
                        uint64_t& overlap_override_count,
                        uint64_t& time_anomaly_count);

// 汇总任务（T4）：全部块统计片段直和 + 按文件名归并逐文件表（同文件多
// 块：计数累加、首块定名/档位/起点、末块终点）+ 全局 DURATION（Σ文件
// 时长）+ T3 侧计数（波形数/覆盖/时间轴观测/混合模式覆盖条目数）+ 绑定
// 目标未命中被跳过的文件数（flow 侧判定传入）+ settings 元数据。
WFSummary wf_merge_summary(const CMVector<const WFStatsDelta*>& deltas,
                           uint64_t waveform_count,
                           uint64_t overlap_override_count,
                           uint64_t time_anomaly_count,
                           uint64_t invalid_binding_count,
                           uint64_t override_entry_count,
                           const WFSettingsMeta& settings);

// ── settings-only 物化（plan §6 settings-only 模式）─────────────────

// 空波形容器构造口（settings-only 模式每分区照常产出空对象——消费接口
// 恒定，plan §8）
WFPartitionWaveform wf_empty_partition_waveform(CMPartitionId pid);

// 统计容器物化（settings-only 主输入，plan §4 物化口径）：overrides 显
// 式条目换算为 (inst, pin) 统计条目——net 名覆盖经形态①推导展开（该网
// 全部连接位各得一条）、inst pin 层级路径名经形态②直配；仅物化本分区
// 实例枚举内的条目（inst_ids = design db 本分区 primary 实例 id 集）。
// 波形引用全哨兵（kWfNoWaveform）；default 不物化（summary 元数据承载
// ——裁定 6）。
// 换算公式（plan §4）：T1 = p × D、T0 = (1−p) × D、rise = rate × D ×
// rise_fraction、fall = rate × D × (1−rise_fraction)、TX = TZ = 0
//（D = duration_fs 换算秒；非整值四舍五入取整）。
// 出参 unresolved_count = 本分区名字未解析的覆盖条目数（逐区观测——
// 未解析的权威判定在 flow 侧：全部分区都未物化的条目才计，WFMG::0001
// 按聚合口径发出）；出参 resolved_names = 本区物化成功的条目名（与
// overrides 同序，无重复——settings overrides 键唯一），聚合判定的
// 分区侧事实源。pg 网覆盖条目按未解析计（pg 无活动语义）。
WFPartitionActivity wf_materialize_activity(
    const WFDesignContext& ctx, CMPartitionId pid,
    const CMVector<CMInstanceId>& inst_ids,
    const CMVector<WFActivityOverride>& overrides, uint64_t duration_fs,
    uint64_t& unresolved_count, CMVector<CMString>& resolved_names);

// 混合模式覆盖应用（plan §4 混合模式段，裁定 IV-2：VCD 输入 +
// settings.activity.overrides 并存）：T3 每分区合并产物（统计容器）上，
// 对命中 overrides 的 (inst, pin) 条目**替换统计值**（换算公式同
// wf_materialize_activity：T1 = p·D 等；**waveform_id 引用保留**——事件
// 是实测数据，统计走用户权威值）；条目不存在时按物化口径新建（哨兵
// waveform_id，仅限 inst_ids 分区成员落点——与 settings-only 一致）。
// default 不经此口（VCD 模式不生效，仅 summary 元数据）。
// 返回覆盖条目数（被替换 + 新建的 (inst, pin) 条目总数）；出参
// applied_names = 本分区命中 ≥1 条目的覆盖条目名（未解析的权威判定在
// flow 侧聚合——全部分区都未命中的条目才计 WFMG::0001，同 settings-only
// 聚合口径）。
uint64_t wf_apply_overrides(const WFDesignContext& ctx, CMPartitionId pid,
                            const CMVector<CMInstanceId>& inst_ids,
                            WFPartitionActivity& activity,
                            const CMVector<WFActivityOverride>& overrides,
                            uint64_t duration_fs,
                            CMVector<CMString>& applied_names);

}  // namespace fly
