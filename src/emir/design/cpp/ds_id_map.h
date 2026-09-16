#pragma once

// =============================================================================
// id → partition 反向映射（2026-09-13 裁定：debug 点查定位——按 id 直接
// 索引的 CMVector<uint32_t>，值 = partition id（复用 DSSubPartition
// .partition_id_），按 id 区间分段落盘按需加载）。
//
// 数据组织（inst/net 各一张全空间映射）：
//   DSIdPartitionSlice    分区片段（S9 每分区合并任务写本区片段的临时对
//                         象）：本区 primary inst id 集 / net 副本 id 集
//                         + 本区 pid（平行数组，merge 前无序）。
//   DSIdPartitionSegment  段正式对象（id_partition_map.{INST,NET}.S{k}）：
//                         段起始 id + 定长 pids 数组（下标 = id − 段起始，
//                         值 = partition id；kNoPartition = 空洞）。
//   DSIdPartitionIndex    段表轻对象（id_partition_map.{INST,NET}）：非
//                         空段起始 id 升序表——查询先读段表（轻），命中
//                         再按需加载段对象（段粒度 2^20 id ≈ 4 MiB/段，
//                         空洞段省略存储）。
//
// merge（ds_merge_id_partition_slices）：多分区片段条目收集 → 按 id 排
// 序 → 线性分段（id >> kIdMapSegmentBits 同段）→ 段内定长数组回填。
// 依赖 S9 primary 恰一不变式（每 id 至多一个片段携带），不做全空间重复
// 校验（红线）；重复 id 取首个（防御性，正常数据不触发）。
//
// 消费形态：debug 读库 API（ds_db.py DesignDb 方法）按 id 定位分区 →
// 整分区对象按需加载（进程内 LRU）；下游电阻提取等分区视图消费不受本
// 映射影响（映射是 debug 定位专用辅助索引，不承载分区数据本体）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <emir/common/cpp/emir_ids.h>
#include <emir/design/cpp/ds_flatten.h>

#include <algorithm>
#include <cstdint>

namespace fly {

// 段粒度（2^20 id/段；段对象 = 定长 pids 数组 uint32 × 2^20 ≈ 4 MiB）
inline constexpr uint64_t kIdMapSegmentBits = 20;
inline constexpr uint64_t kIdMapSegmentSize = uint64_t{1} << kIdMapSegmentBits;
// 段内空洞哨兵（partition id 合法值域 = 分区表下标，远小于哨兵值；与
// CMPartitionId 默认哨兵同值口径）
inline constexpr uint32_t kIdMapNoPartition = CMPartitionId::kInvalid;

// 分区片段（S9 每分区合并任务的临时产物；freeze 前由映射汇总任务合并
// 后清理）——本区 (id, partition id) 对的平行数组
class DSIdPartitionSlice {
public:
    // 本区收录的 id 集（inst 片段 = primary 副本 global id；net 片段 =
    // net 副本 global id）+ 对齐的 partition id（= 本区 pid，恒同值——
    // 平行数组形态保持与 merge 输入契约显式可见）
    CMVector<uint64_t> ids_;
    CMVector<CMPartitionId> pids_;

    void add(uint64_t id, CMPartitionId pid) {
        ids_.push_back(id);
        pids_.push_back(pid);
    }
    size_t size() const { return ids_.size(); }

    FLY_SERIALIZE(ids_, pids_)
};

// 段正式对象（id_partition_map.{INST,NET}.S{k}，k = id_start >> 20）：
// 按段 id 直接索引的 partition id 数组
class DSIdPartitionSegment {
public:
    // 段起始 id（= 段号 × 段粒度）
    uint64_t id_start_ = 0;
    // 定长 partition id 数组（长度恒 = kIdMapSegmentSize；下标 = id −
    // id_start_；kIdMapNoPartition = 空洞）。**消费禁令**：数组按整段
    // 序列化（bitsery 变长编码下全空段也占空间——空洞段以「不在段表」
    // 表达，本对象只在段表登记后落盘）
    CMVector<CMPartitionId> pids_;

    CM_PROPERTY(id_start)

    size_t size() const { return pids_.size(); }
    // id → partition id（未配置段长/空洞/越界 = kIdMapNoPartition）
    CMPartitionId partition_of(uint64_t id) const {
        if (id < id_start_ || id - id_start_ >= pids_.size()) {
            return CMPartitionId{kIdMapNoPartition};
        }
        return pids_[static_cast<size_t>(id - id_start_)];
    }

    FLY_SERIALIZE(id_start_, pids_)
};

// 段表轻对象（id_partition_map.{INST,NET}）：非空段起始 id 升序表。
// 查询路径 = 二分定位段 → 按需读段对象（debug API 的 LRU 缓存粒度）
class DSIdPartitionIndex {
public:
    // 非空段起始 id 升序（段号 = 起始 id >> kIdMapSegmentBits）
    CMVector<uint64_t> id_starts_;

    size_t size() const { return id_starts_.size(); }
    // 段是否存在（二分；存在 = 该段至少一个非空洞条目）
    bool has_segment(uint64_t id) const {
        return find_segment_start(id) != kIdMapNoSegment;
    }
    // id 所在段起始 id（无此段 = kIdMapNoSegment）
    static constexpr uint64_t kIdMapNoSegment = UINT64_MAX;
    uint64_t find_segment_start(uint64_t id) const {
        const uint64_t start = (id >> kIdMapSegmentBits) << kIdMapSegmentBits;
        const auto it = std::lower_bound(id_starts_.begin(), id_starts_.end(),
                                         start);
        if (it != id_starts_.end() && *it == start) {
            return start;
        }
        return kIdMapNoSegment;
    }

    FLY_SERIALIZE(id_starts_)
};

// merge 产物：段表 + 段集（segments 按 id_start 升序；流程侧逐段写
// 正式对象、段表最后写定——段表在即全部段对象已在，freeze 挂段表）
struct DSIdPartitionMapResult {
    DSIdPartitionIndex index;
    CMVector<DSIdPartitionSegment> segments;
};

// merge：多分区片段 → 全空间分段（空洞段跳过）。slices 为观察指针集
// （借引用不拷贝；同 ds_build_hier_tree 入参约定）。
DSIdPartitionMapResult ds_merge_id_partition_slices(
    const CMVector<const DSIdPartitionSlice*>& slices);

// 提取：分区产物 → 本区片段（instance_kind = true 取 primary instance
// 副本 id 集；false 取 net 副本 id 集——GEOMETRY + GEOMETRY_PG 两对象键
// 集并集，2026-09-14 拆分裁定：网副本落点分侧两对象，切片结构不变）。
// partition_id = 本区 pid。
DSIdPartitionSlice ds_collect_partition_id_slice(
    const DSPartInstances& instances, const DSPartitionGeometry& geometry,
    const DSPartitionGeometry& geometry_pg, bool instance_kind,
    CMPartitionId partition_id);

}  // namespace fly
