#pragma once

// =============================================================================
// timing db 落库形态 + 名字换算 + 分区合并（plan docs/emir/timing-db-plan.md
// §5.2/§5.3/§7/§8，2026-09-16 全裁定）。
//
// 三层结构（对齐 plan §6 直接任务链）：
//   TMChunkPlan       T1 切块扫描产物（temp）：逐文件头段公共前缀 + 字节
//                     区间切块表 + 绑定描述（§2 名字空间三形态：纯路径全
//                     层级名 / block_inst 块实例绑定 / block_cell 块定义
//                     绑定 + strip_prefix 段级前缀剥离）
//   TMEntrySlice      T2 逐块解析产物分区分片（temp）：本块所涉各分区的
//                     实例时序片段（时钟归属 = 块内 id）+ 统计片段（含
//                     本块时钟表——头段公共前缀保证块间一致）
//   TMClockRemap      时钟表合并任务产出（temp）：块内时钟 id → 最终表
//                     下标的逐文件重映射桥（评审 P1-1）
//   正式对象          T3/T4 产物：TMPartitionTiming（PART_{xp}_{yp}.TIMING，
//                     primary 恰一——时序为点数据无 extend 副本语义）+
//                     TMClockTable（"clocks"）+ TMSummary（"summary"）
//
// 名字换算（§7，解析边界一次完成，T2 任务内 C++）：消费 design db 快照
// （DSDesign 层级树 + cell/pin hasher、DSBlockNames_<i> 块 local 名空间、
// id_partition_map INST 段表与段对象、pg_nets 全局集、分区 NETS 对象）——
// 上下文 TMDesignContext 以 CMSharedPtr 共享注入（§16 业务层全禁裸指针；
// mapper 持树观察指针为 ds_make_name_mapper 既有先例形态，生命周期由
// design 共享计数保证）。
//
// id 体系：全部 CM 族强类型（emir_ids.h；hasher/mapper 底座裸值域豁免
// 边界两侧 CMXxxId{} / .value() 成对显式互转）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <common/types/cpp/flags_macro.h>
#include <container/cpp/container_aliases.h>
#include <emir/common/cpp/emir_ids.h>
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_id_map.h>
#include <emir/design/cpp/ds_name_mapper.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/timing/cpp/tm_types.h>

#include <cstdint>
#include <memory>

namespace fly {

// ── 落库形态（plan §5.2）────────────────────────────────────────────

// 逐引脚时序（id 化终态；值 = 解析边界 TMRange 原样搬运）
struct TMPinTiming {
    // 全局平铺 pin id（2026-09-16 裁定 3：pin 全局名字空间，键 = 裸 pin 名）
    CMPinId pin_id_;
    TMRange rise_arrival_;
    TMRange fall_arrival_;
    TMRange rise_slew_;
    TMRange fall_slew_;
    // rise_arrival/fall_arrival/rise_slew/fall_slew = 对应值存在；
    // constant = CONSTANT 常量条目（无窗口无翻转）；multi_source = 条目
    // 来自多于一个时钟源分组（自 TMNameTiming 搬运）
    CM_FLAGS(uint8_t, rise_arrival, fall_arrival, rise_slew, fall_slew,
             constant, multi_source)

    FLY_SERIALIZE(pin_id_, rise_arrival_, fall_arrival_, rise_slew_,
                  fall_slew_, flags_)
};

// 逐实例时序（时钟归属 + 引脚稀疏表；实例引脚少，紧凑 vector 优于 map）
struct TMInstanceTiming {
    // 时钟表键（哨兵 = CMClockId 默认 kInvalid = 无归属）
    CMClockId clock_id_;
    CMVector<TMPinTiming> pins_;

    // 按 pin id 线性查（实例引脚个位数~几十，线性即最快）
    const TMPinTiming* find_pin(CMPinId pin_id) const;

    FLY_SERIALIZE(clock_id_, pins_)
};

// 分区正式对象 PART_{xp}_{yp}.TIMING（T3 每分区一合并任务唯一写定；
// primary 恰一，无 extend 副本——时序为点数据，无空间延展语义）
class TMPartitionTiming {
public:
    CMPartitionId part_id_;
    // 键 = 实例全局 id
    CMUnorderedMap<CMInstanceId, TMInstanceTiming> items_;

    size_t size() const { return items_.size(); }

    FLY_SERIALIZE(part_id_, items_)
};

// 时钟表（"clocks" 正式对象；跨文件按名合并保留首份，顶层文件定义优先
// ——T4）。下标即 clock id（CMClockId 值域）
class TMClockTable {
public:
    // 条目（自解析边界 TMClock 搬运；时刻 ns）
    struct Entry {
        CMString name_;
        double period_ = 0.0;
        double posedge_ = 0.0;
        double negedge_ = 0.0;

        FLY_SERIALIZE(name_, period_, posedge_, negedge_)
    };

    CMVector<Entry> clocks_;

    // 按名查（未命中返回 nullptr）
    const Entry* find(const CMString& name) const;

    FLY_SERIALIZE(clocks_)
};

// ── 统计（plan §5.2 summary + §9 消息族计数源；C++ 侧计数器，消息由
//    flow 侧读汇总一次发出——dev-rules §7 兜底语义）───────────────────

// 逐文件统计（来源可追溯，dev-rules §7；TMSummary.files_ 条目）
struct TMFileStats {
    CMString source_file_;
    // 文件头 TIME_SCALE 声明（原值；入库值已统一换算 ns）
    double time_scale_sec_ = 1e-9;
    // 解析条目数（含 constant）
    uint64_t entry_count_ = 0;
    // 成功换算入库条目数（挂到 ≥1 (实例, pin)）
    uint64_t hit_count_ = 0;
    // 实例名未匹配跳过（TIMG::0001；网名未匹配并入本族文案，见
    // net_name_miss_count_）
    uint64_t skipped_instance_count_ = 0;
    // 网名未在 design db 命中（summary 字段；消息并入 TIMG::0001 汇总）
    uint64_t net_name_miss_count_ = 0;
    // 引脚名未匹配跳过（TIMG::0002）
    uint64_t skipped_pin_count_ = 0;
    // 网条目无驱动/悬空跳过（TIMG::0003；含仅端口网）
    uint64_t dangling_net_count_ = 0;
    // 未放置实例跳过（TIMG::0008——design db 无 primary 分区副本）
    uint64_t unplaced_instance_count_ = 0;
    // strip_prefix 未命中跳过（TIMG::0010，含剥后余空）
    uint64_t strip_miss_count_ = 0;
    // 块语法破损跳过数（本文件的字节块；单块失败兜底——空分片照常产出）
    uint64_t failed_chunk_count_ = 0;

    FLY_SERIALIZE(source_file_, time_scale_sec_, entry_count_, hit_count_,
                  skipped_instance_count_, net_name_miss_count_,
                  skipped_pin_count_, dangling_net_count_,
                  unplaced_instance_count_, strip_miss_count_,
                  failed_chunk_count_)
};

// 统计片段（T2 每块产出；T4 聚合进 TMSummary。plan §5.3 统计片段）
struct TMStatsDelta {
    // 本块所属文件序（编排侧分配，T4 跨块按文件聚合的分组键）
    uint32_t file_index_ = 0;
    // 本块解析产物时钟表（TMClock 直通——时刻已 ns；T4 按文件→跨文件
    // 合并，顶层文件定义优先）
    CMVector<TMClock> clocks_;

    // —— 逐文件计数（通常单条；同文件多块由 T4 按文件名合并）——
    CMVector<TMFileStats> files_;

    // —— 全局计数（T4 直和聚合）——
    // 多驱动网数（2026-09-16 裁定 4：时序值挂全部 driver 位条目）
    uint64_t multi_driver_net_count_ = 0;
    // pg 网条目跳过（§7.5：时序无 pg 语义）
    uint64_t pg_net_skip_count_ = 0;
    // CONSTANT 覆盖条目数
    uint64_t const_entry_count_ = 0;
    // 非常量且四值全无条目数（NO_TW 覆盖观测）
    uint64_t no_window_count_ = 0;
    // 弃收计数（自解析产物聚合，plan §5.2）
    uint64_t dropped_source_res_count_ = 0;
    uint64_t dropped_slack_count_ = 0;
    // CAUSED_BY 引用未登记 WAVEFORM 时钟名的条目分组数（自解析产物聚合
    // ——评审 P1-1：修复前该计数未入片段/汇总，时钟归属丢失不可见）
    uint64_t missing_clock_count_ = 0;
    // C/D 结尾标记观测（不入库仅观测）
    uint64_t cd_flag_c_count_ = 0;
    uint64_t cd_flag_d_count_ = 0;

    FLY_SERIALIZE(file_index_, clocks_, files_, multi_driver_net_count_,
                  pg_net_skip_count_, const_entry_count_, no_window_count_,
                  dropped_source_res_count_, dropped_slack_count_,
                  missing_clock_count_, cd_flag_c_count_, cd_flag_d_count_)
};

// 汇总正式对象（"summary"；T4 唯一写定）：逐来源文件统计 + 全局计数 +
// 跨文件冲突（TIMG 消息族 §9 的计数源）
class TMSummary {
public:
    // 来源文件清单（逐文件条目数/命中数/各类跳过，可追溯）
    CMVector<TMFileStats> files_;
    // 全局条目/命中总数
    uint64_t total_entry_count_ = 0;
    uint64_t total_hit_count_ = 0;
    // 名字未匹配家族（0001 实例名 + 网名并入其文案 / 0002 引脚名）
    uint64_t skipped_instance_count_ = 0;
    uint64_t net_name_miss_count_ = 0;
    uint64_t skipped_pin_count_ = 0;
    // 悬空/无驱动网（0003）
    uint64_t dangling_net_count_ = 0;
    // 未放置实例（0008）
    uint64_t unplaced_instance_count_ = 0;
    // strip_prefix 未命中（0010）
    uint64_t strip_miss_count_ = 0;
    // 跨文件同名条目冲突（0006；保留首份——T3 合并计数）
    uint64_t cross_file_conflict_count_ = 0;
    // 时钟名跨文件周期/沿不一致（0007；保留首份——T4 合并计数）
    uint64_t clock_conflict_count_ = 0;
    // 多驱动网（裁定 4）
    uint64_t multi_driver_net_count_ = 0;
    // pg 网条目跳过（§7.5）
    uint64_t pg_net_skip_count_ = 0;
    // 常量 / 无窗口覆盖
    uint64_t const_entry_count_ = 0;
    uint64_t no_window_count_ = 0;
    // 弃收与 C/D 观测
    uint64_t dropped_source_res_count_ = 0;
    uint64_t dropped_slack_count_ = 0;
    // CAUSED_BY 引用未登记 WAVEFORM 时钟名的条目分组数（0001 家族之外的
    // 解析级兜底观测——评审 P1-1 补聚合，时钟归属丢失可追溯）
    uint64_t missing_clock_count_ = 0;
    uint64_t cd_flag_c_count_ = 0;
    uint64_t cd_flag_d_count_ = 0;
    // 块语法破损总数（全部文件全块失败 → flow 侧 TIMG::0009 fatal）
    uint64_t failed_chunk_count_ = 0;

    FLY_SERIALIZE(files_, total_entry_count_, total_hit_count_,
                  skipped_instance_count_, net_name_miss_count_,
                  skipped_pin_count_, dangling_net_count_,
                  unplaced_instance_count_, strip_miss_count_,
                  cross_file_conflict_count_, clock_conflict_count_,
                  multi_driver_net_count_, pg_net_skip_count_,
                  const_entry_count_, no_window_count_,
                  dropped_source_res_count_, dropped_slack_count_,
                  missing_clock_count_, cd_flag_c_count_, cd_flag_d_count_,
                  failed_chunk_count_)
};

// ── 中间形态（plan §5.3 temp 对象，freeze 清理）─────────────────────

// 单文件切块表（块区间 = [chunk_starts_[i],
// i+1 < n ? chunk_starts_[i+1] : file_size_)——文件去掉 (TIMING_WINDOWS
// 外皮后的顶层子构造序列，T2 解析时重包外皮，块自包含）。绑定描述不经
// 本表传递（评审 P3-5：绑定信息以 flow 参数直达 T2——T1 只做字节切块，
// 四个 binding 字段从不填充，已删）。
//
// 头段公共前缀（评审 P1-1 修复，2026-09-17）：首个 CAUSED_BY（数据构
// 造）之前的全部顶层构造 = 文件头（HEADER + WAVEFORM 段），单独切为
// [prefix_start_, prefix_end_)，T2 组装每块文本时拼在块前——同文件各
// 块解析出的时钟表逐字节一致，块内时钟 id 编号域统一（否则 WAVEFORM 只
// 落首块，非首块 CAUSED_BY 引用的时钟名块内未登记，时钟归属静默丢失）。
// 无数据构造时 prefix = 全部闭合构造（单空块）；流级破损（无闭合构造）
// prefix 置空、块包全文本交解析器判破损兜底。
struct TMFileChunkPlan {
    CMString file_name_;
    uint64_t file_size_ = 0;
    // 头段公共前缀字节区间（[prefix_start_, prefix_end_)；空表 = 无头段）
    uint64_t prefix_start_ = 0;
    uint64_t prefix_end_ = 0;
    // 块起点/终点偏移（平行数组；块 i = [chunk_starts_[i], chunk_ends_[i])
    // ——起点升序，块界 = 顶层子构造边界（行/构造对齐）。**末块终点 =
    // 最后顶层子构造终点**（文件尾 ')' 外皮不入块——T2 重包外皮语义）；
    // 无顶层子构造（空 TIMING_WINDOWS）= 单空块 [0, 0)
    CMVector<uint64_t> chunk_starts_;
    CMVector<uint64_t> chunk_ends_;

    FLY_SERIALIZE(file_name_, file_size_, prefix_start_, prefix_end_,
                  chunk_starts_, chunk_ends_)
};

// T1 切块清单（temp；master 侧单任务产出）
class TMChunkPlan {
public:
    CMVector<TMFileChunkPlan> files_;

    FLY_SERIALIZE(files_)
};

// 逐块解析产物分区分片（temp；T2 每块一对象）
class TMEntrySlice {
public:
    // 本块触及分区 → 实例时序片段（键 = 分区 id）
    CMUnorderedMap<CMPartitionId, TMPartitionTiming> partitions_;
    // 统计片段（含本块时钟表）
    TMStatsDelta stats_;

    FLY_SERIALIZE(partitions_, stats_)
};

// 时钟 id 重映射表（temp；时钟表合并任务产出、T3 每分区合并消费——
// 评审 P1-1：块内时钟 id（TMTimingFile::clock_index_ 局部编号）与最终
// TMClockTable 下标（跨文件按优先级重排）是两个编号空间，中间必须经本
// 表桥接，否则条目时钟归属指错时钟）。按文件序；file_maps_[file_index]
// [块内 id] = 最终表下标（块内 id 越界/文件号越界由消费方按无归属哨兵
// 兜底——正常流程由同批 slices 产出，不触发）
class TMClockRemap {
public:
    CMVector<CMVector<uint32_t>> file_maps_;

    FLY_SERIALIZE(file_maps_)
};

// ── T1 切块扫描 ─────────────────────────────────────────────────────

// 逐文件切块：按字节流扫描顶层构造括号深度边界（引号内不计深度），
// alpha chunk_size_mb 字节界后的首个顶层子构造终点收口。首块起于首个
// 顶层子构造、末块终于最后顶层子构造终点（(TIMING_WINDOWS 外皮与文件
// 尾 ')' 不入块——T2 解析时重包）。文件不可读抛 std::runtime_error
//（dev-rules §7 raise 第一类）。
TMFileChunkPlan tm_plan_file_chunks(const CMString& path,
                                    uint64_t chunk_size);

// ── T2 换算上下文（design db 快照组装）─────────────────────────────

// design db 快照共享注入上下文（Python 编排侧快照对象逐个注入；全部
// CMSharedPtr 持有——生命周期由本类计数管理，业务层零裸指针）。两维度
// name mapper 于 set_design 时构造（持 design 内层级树观察指针——
// ds_make_name_mapper 既有先例形态；design_ 共享计数保生命周期）。
// 组装发生在 worker 任务内（master 把 design db 快照对象逐个写入本 db
// 临时对象——全部已有序列化类型；T2 任务 read_object 后经 export 绑定
// 的 setter 组装 ctx，mapper 构建为纯内存毫秒级操作）。
class TMDesignContext {
public:
    TMDesignContext() = default;

    // design 快照（层级树 + cell/pin hasher + 分区表）；构造两维度 mapper
    void set_design(CMSharedPtr<DSDesign> design);
    // mapper 重建（set_design 的重建半步——hasher 注入关系重挂）
    void rebuild_mappers();
    // 块 local 名空间伴生对象（def 序逐个注入两 mapper + 块 hasher 索引）
    void add_block_names(CMSharedPtr<const DSBlockNames> names);
    // id → partition 反向映射（INST 段表 + 全部非空段对象——分区路由唯
    // 一依赖；NET 维度不参与 T2 路由，评审 P2-2 清理不注入）
    void set_inst_id_map(CMSharedPtr<DSIdPartitionIndex> index);
    void add_inst_segment(CMSharedPtr<DSIdPartitionSegment> segment);
    // 全局 pg 网 id 集（§7.5 pg 条目跳过判定）
    void set_pg_nets(CMSharedPtr<DSPgNetSet> pg_nets);
    // 分区 NETS 对象（网条目 driver 位定位；键取对象 part_id_）
    void add_partition_nets(CMSharedPtr<DSPartitionNets> nets);

    const DSDesign& design() const { return *design_; }
    const DSInstanceNameMapper& inst_mapper() const { return inst_mapper_; }
    const DSNetNameMapper& net_mapper() const { return net_mapper_; }

    // 实例全局 id → 分区 id（INST 段表 → 段对象；未命中/空洞返回默认
    // 哨兵——调用方按未放置计数）
    CMPartitionId locate_instance_partition(CMInstanceId inst_id) const;
    // 分区 NETS 对象观察（未快照该分区返回空 shared——调用方按悬空计数）
    CMSharedPtr<DSPartitionNets> partition_nets(CMPartitionId pid) const;
    // 全部快照分区 NETS 对象（网条目遍历口——信号网 NETS 全量补全，任一
    // 分区副本都有该网完整连接表；T2 逐对象查首个命中）
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
    CMUnorderedMap<uint32_t, CMSharedPtr<DSInstanceNameHasher>> inst_hashers_;
    CMUnorderedMap<uint32_t, CMSharedPtr<DSNetNameHasher>> net_hashers_;
    CMSharedPtr<DSIdPartitionIndex> inst_index_;
    CMUnorderedMap<uint64_t, CMSharedPtr<DSIdPartitionSegment>> inst_segments_;
    CMSharedPtr<DSPgNetSet> pg_nets_;
    CMUnorderedMap<uint32_t, CMSharedPtr<DSPartitionNets>> part_nets_;

    // —— 运行时构件（不序列化；set_design/rebuild_mappers 重建）——
    DSInstanceNameMapper inst_mapper_;
    DSNetNameMapper net_mapper_;
};

// 绑定形态（§2 三形态；TMFileBinding::kind 的类型——枚举定型存储，
// 评审 P3-5。对象本身不序列化——worker 端 Python 侧构建后直传 C++）
enum class TMFileBindingKind : uint8_t {
    PATH = 0,        // 纯路径（条目名 = 全层级路径）
    BLOCK_INST = 1,  // block_inst（条目名 = 该块实例内局部名）
    BLOCK_CELL = 2,  // block_cell（条目名 = 该 cell 定义内局部名，定义级
                     // 时序对全部实例成立——建库期复制）
};

// 绑定描述（§2/plan §7；export 面 EXTMFileBinding 的 C++ 侧。worker 端
// Python 侧构建后直传 C++，不经任务参数传输——无序列化需求）
struct TMFileBinding {
    TMFileBindingKind kind = TMFileBindingKind::PATH;
    CMString block_inst;
    CMString block_cell;
    CMString strip_prefix;
};

// ── T2 逐块换算 ─────────────────────────────────────────────────────

// 单块执行体：读文件头段公共前缀 [prefix_start, prefix_end)（HEADER +
// WAVEFORM 段——块间时钟表一致的保证，评审 P1-1）与块字节区间
// [chunk_start, chunk_end)（顶层子构造序列），重包 (TIMING_WINDOWS 外皮
// 解析（复用 tm_parse_twf_text；单块语法破损 → 空分片 + failed_chunk 计
// 数，依赖链保持满足——plan §6 单块失败兜底），逐条目名字换算 → 分区路
// 由 → 分片产出。块绑定形态（kind 1/2）在此解析定位（绑定目标未命中 =
// 防御场景——入口校验已拦，按块失败兜底计数）。
TMEntrySlice tm_convert_chunk(const TMDesignContext& ctx,
                              const CMString& path, uint64_t prefix_start,
                              uint64_t prefix_end, uint64_t chunk_start,
                              uint64_t chunk_end,
                              const TMFileBinding& binding,
                              uint32_t file_index);

// ── T3 / T4 合并 ────────────────────────────────────────────────────

// 时钟 id 域重映射（块内 id → 最终表 id；review P1-1 的桥）。文件号或块
// 内 id 越界（正常流程不发生——remap 由同批 slices 产出）返回默认哨兵。
CMClockId tm_remap_clock_id(const TMClockRemap& remap, uint32_t file_index,
                            CMClockId chunk_clock_id);

// 每分区合并（T3 每分区一任务）：收集各块分片中本分区的片段 → 时钟归属
// 经 remap 重写为最终表 id → merge → 正式对象。冲突判定按来源文件区分
//（TIMG::0006 = **跨文件**同名条目；同文件跨块的 NET 条目与其驱动 PIN
// 条目同指 (实例, pin) 是合法同源形态——静默保留首份，不计冲突）：同
// (实例, pin) 首见来源文件号记录于合并过程局部表（不序列化），再现时来
// 源相同 = 静默丢弃、不同 = conflict 出参累加（flow 侧经 T4 聚合入
// summary）。slices 为观察指针集（借引用不拷贝，同
// ds_merge_id_partition_slices 入参约定）。
TMPartitionTiming tm_merge_partition(
    const CMVector<const TMEntrySlice*>& slices, const TMClockRemap& remap,
    CMPartitionId pid, uint64_t& conflict_count);

// 汇总任务（T4）①：统计聚合——全部块统计片段直和 + 按文件归并逐文件表
// （同文件多块：条目/计数累加、文件名取首块）+ T3 分区侧跨文件冲突与
// 时钟表侧差异计数入表（TIMG::0006 / 0007）。
TMSummary tm_merge_summary(const CMVector<const TMStatsDelta*>& deltas,
                           uint64_t cross_file_conflict_count,
                           uint64_t clock_conflict_count);

// 时钟表跨文件合并（独立任务——评审 P1-1：提前至 T3 之前执行，产出最
// 终 clocks 表 + per-file remap 桥）：同文件跨块先归并（同名保留首份），
// 再按文件优先级合并：顶层文件（无绑定、纯路径 kind 0）定义优先，其余
// （块绑定文件）按文件序首份兜底（2026-09-16 裁定 5）。任何周期/沿时刻
// 差异计数出参累加（TIMG::0007，保留首份不 raise）。入参 = 每文件
// (绑定形态 int 0/1/2——flow 归一化文件的 kind, 时钟表)（clocks 已按
// 文件归并的块序表）；remap 出参按
// 文件序给出 [file_index][块内 id] → 最终表下标。
TMClockTable tm_merge_clocks(
    const CMVector<std::pair<int, TMClockTable>>& file_clocks,
    TMClockRemap& remap, uint64_t& conflict_count);

}  // namespace fly