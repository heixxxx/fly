#include <emir/design/cpp/ds_id_map.h>

#include <algorithm>

namespace fly {

// merge：多分区片段 → 全空间分段（2026-09-13 裁定：按 id 排序线性分段，
// 空洞段跳过——段对象只对非空段落盘；同 id 多片段取首个，依赖 S9 primary
// 恰一不变式、不做全空间重复校验）。id 参与排序/分段为纯数值运算，取
// 机器值域（.value()）计算——段对象不存 id，两维度共用。
template <typename IdT>
DSIdPartitionMapResult ds_merge_id_partition_slices(
    const CMVector<CMSharedPtr<const DSIdPartitionSliceT<IdT>>>& slices) {
    DSIdPartitionMapResult out;

    // 条目收集（(id, pid) 对；排序期中间形态）
    CMVector<std::pair<uint64_t, CMPartitionId>> entries;
    for (const CMSharedPtr<const DSIdPartitionSliceT<IdT>>& slice : slices) {
        if (slice == nullptr) {
            continue;  // 防御（空片段放行，dev-rules §7）
        }
        for (size_t i = 0; i < slice->ids_.size(); ++i) {
            entries.emplace_back(slice->ids_[i].value(), slice->pids_[i]);
        }
    }
    if (entries.empty()) {
        return out;  // 空映射（无分区数据）——空段表 + 空段集
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.first != b.first ? a.first < b.first
                                            : a.second < b.second;
              });

    // 线性分段：id >> 段位数同段；段内定长数组回填（空洞 kIdMapNoPartition）
    auto fill_segment = [&](uint64_t seg_start, size_t begin, size_t end) {
        DSIdPartitionSegment seg;
        seg.id_start_ = seg_start;
        seg.pids_.assign(static_cast<size_t>(kIdMapSegmentSize),
                         CMPartitionId{kIdMapNoPartition});
        for (size_t i = begin; i < end; ++i) {
            const size_t slot =
                static_cast<size_t>(entries[i].first - seg_start);
            if (!seg.pids_[slot].is_valid()) {
                seg.pids_[slot] = entries[i].second;  // 首个生效（不变式）
            }
        }
        out.index.id_starts_.push_back(seg_start);
        out.segments.push_back(std::move(seg));
    };

    size_t begin = 0;
    while (begin < entries.size()) {
        const uint64_t seg_start =
            (entries[begin].first >> kIdMapSegmentBits) << kIdMapSegmentBits;
        size_t end = begin + 1;
        while (end < entries.size() &&
               (entries[end].first >> kIdMapSegmentBits) ==
                   (seg_start >> kIdMapSegmentBits)) {
            ++end;
        }
        fill_segment(seg_start, begin, end);
        begin = end;
    }
    return out;
}

// 显式实例化（业务唯一实例化组：INST/NET 两维度别名，评审 B-6）
template DSIdPartitionMapResult ds_merge_id_partition_slices<CMInstanceId>(
    const CMVector<CMSharedPtr<const DSIdPartitionSliceT<CMInstanceId>>>&);
template DSIdPartitionMapResult ds_merge_id_partition_slices<CMNetId>(
    const CMVector<CMSharedPtr<const DSIdPartitionSliceT<CMNetId>>>&);

// 提取（INST 表）：分区产物 → 本区片段。instance 维度取 primary 副本
//（每对象恰一 primary，补记①）。
DSInstIdPartitionSlice ds_collect_inst_id_slice(
    const DSPartInstances& instances, CMPartitionId partition_id) {
    DSInstIdPartitionSlice out;
    for (const auto& [gid, inst] : instances.items_) {
        if (inst.is_primary()) {
            out.add(gid, partition_id);
        }
    }
    return out;
}

// 提取（NET 表）：分区产物 → 本区片段。net 维度取 GEOMETRY +
// GEOMETRY_PG 两对象键集并集（2026-09-14 拆分裁定：网副本落点分侧两对
// 象——跟随 net 副本口径不变，信号侧键 0 = OBS 桶专属位照常入片段）。
DSNetIdPartitionSlice ds_collect_net_id_slice(
    const DSPartitionGeometry& geometry,
    const DSPartitionGeometry& geometry_pg, CMPartitionId partition_id) {
    DSNetIdPartitionSlice out;
    for (const auto& [gid, _] : geometry.nets_) {
        (void)_;
        out.add(gid, partition_id);
    }
    for (const auto& [gid, _] : geometry_pg.nets_) {
        (void)_;
        out.add(gid, partition_id);
    }
    return out;
}

}  // namespace fly
