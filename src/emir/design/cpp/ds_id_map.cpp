#include <emir/design/cpp/ds_id_map.h>

namespace fly {

// merge：多分区片段 → 全空间分段（2026-09-13 裁定：按 id 排序线性分段，
// 空洞段跳过——段对象只对非空段落盘；同 id 多片段取首个，依赖 S9 primary
// 恰一不变式、不做全空间重复校验）。
DSIdPartitionMapResult ds_merge_id_partition_slices(
    const CMVector<const DSIdPartitionSlice*>& slices) {
    DSIdPartitionMapResult out;

    // 条目收集（(id, pid) 对；排序期中间形态）
    CMVector<std::pair<uint64_t, uint32_t>> entries;
    for (const DSIdPartitionSlice* slice : slices) {
        if (slice == nullptr) {
            continue;  // 防御（空片段放行，dev-rules §7）
        }
        for (size_t i = 0; i < slice->ids_.size(); ++i) {
            entries.emplace_back(slice->ids_[i], slice->pids_[i]);
        }
    }
    if (entries.empty()) {
        return out;  // 空映射（无分区数据）——空段表 + 空段集
    }
    std::sort(entries.begin(), entries.end());

    // 线性分段：id >> 段位数同段；段内定长数组回填（空洞 kIdMapNoPartition）
    auto fill_segment = [&](uint64_t seg_start, size_t begin, size_t end) {
        DSIdPartitionSegment seg;
        seg.id_start_ = seg_start;
        seg.pids_.assign(static_cast<size_t>(kIdMapSegmentSize),
                         kIdMapNoPartition);
        for (size_t i = begin; i < end; ++i) {
            const size_t slot =
                static_cast<size_t>(entries[i].first - seg_start);
            if (seg.pids_[slot] == kIdMapNoPartition) {
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

// 提取：分区产物 → 本区片段。instance 维度取 primary 副本（每对象恰一
// primary，补记①）；net 维度取 geometry 键集（跟随 net 副本口径——
// NET_CONNECTIONS 与 use map 均以几何命中分区为落点）。
DSIdPartitionSlice ds_collect_partition_id_slice(
    const DSPartInstances& instances, const DSPartitionGeometry& geometry,
    bool instance_kind, uint32_t partition_id) {
    DSIdPartitionSlice out;
    if (instance_kind) {
        for (const auto& [gid, inst] : instances.items_) {
            if (inst.is_primary()) {
                out.add(gid, partition_id);
            }
        }
    } else {
        for (const auto& [gid, _] : geometry.nets_) {
            (void)_;
            out.add(gid, partition_id);
        }
    }
    return out;
}

}  // namespace fly
