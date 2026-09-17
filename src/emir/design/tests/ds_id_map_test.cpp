// id → partition 反向映射单测（2026-09-13 debug 定位裁定；2026-09-14 拆
// 分裁定 NET 片段源 = 两几何对象键集并集）：
//   1. 提取：分区产物 → 本区片段（inst 维度 = primary 副本 id 集——非
//      primary extend 副本不入；net 维度 = GEOMETRY + GEOMETRY_PG 两对象
//      键集并集——跟随 net 副本口径）；
//   2. merge：多片段条目排序线性分段——段表升序、段内 pids 直接索引、
//      空洞 kIdMapNoPartition、跨段 id 分属两段、中间空洞段不落段表；
//   3. 同 id 多片段取首个（依赖 S9 primary 恰一不变式的确定性防御——
//      不做全空间重复校验，红线）；
//   4. 段查询边界（id_start / 段末 / 越界 / 段长未配置）与段表二分；
//   5. 序列化往返（片段 / 段 / 段表）。
// 期望值全部按实现口径手工推导，锁定行为。
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_id_map.h>
#include <emir/design/cpp/ds_types.h>

#include <gtest/gtest.h>

#include "borrow_view.h"

#include <cstdint>

namespace {

using namespace fly;

// —— 提取：inst 维度只收 primary、net 维度取两几何对象键集并集 ————————

TEST(DSIdMapTest, CollectSlicePrimaryInstancesAndGeometryNets) {
    DSPartInstances instances;
    DSInstance primary_inst;
    primary_inst.set_primary();
    instances.items_[CMInstanceId{7}] = std::move(primary_inst);
    DSInstance extend_inst;  // extend 副本（非 primary）——不入片段
    instances.items_[CMInstanceId{8}] = std::move(extend_inst);

    DSPartitionGeometry geometry;
    geometry.add_entry(CMNetId{3}, DSGeomEntry{});   // net 副本（含 OBS 桶键）
    geometry.add_entry(CMNetId{3}, DSGeomEntry{});   // 同键多条目只入一次片段
    DSGeomEntry obs;                        // 键 0 纯 OBS 桶——键 0 仍入
    obs.set_obs();
    geometry.add_entry(CMNetId{0}, std::move(obs));
    DSPartitionGeometry geometry_pg;        // pg 侧对象（2026-09-14 拆分）
    geometry_pg.add_entry(CMNetId{9}, DSGeomEntry{});  // pg 网副本键入并集

    const DSInstIdPartitionSlice inst_slice =
        ds_collect_inst_id_slice(instances, CMPartitionId{2});
    ASSERT_EQ(inst_slice.size(), 1u);
    EXPECT_EQ(inst_slice.ids_[0].value(), 7u);
    EXPECT_EQ(inst_slice.pids_[0].value(), 2u);

    // net 维度 = GEOMETRY + GEOMETRY_PG 两对象键集并集（拆分裁定）
    const DSNetIdPartitionSlice net_slice =
        ds_collect_net_id_slice(geometry, geometry_pg, CMPartitionId{2});
    ASSERT_EQ(net_slice.size(), 3u);
    EXPECT_EQ(net_slice.ids_[0].value(), 0u);
    EXPECT_EQ(net_slice.ids_[1].value(), 3u);
    EXPECT_EQ(net_slice.ids_[2].value(), 9u);
    EXPECT_EQ(net_slice.pids_[0], 2u);
    EXPECT_EQ(net_slice.pids_[1], 2u);
    EXPECT_EQ(net_slice.pids_[2], 2u);
}

TEST(DSIdMapTest, CollectSliceEmptyProducts) {
    DSPartInstances instances;
    DSPartitionGeometry geometry;
    const DSInstIdPartitionSlice s =
        ds_collect_inst_id_slice(instances, CMPartitionId{0});
    EXPECT_EQ(s.size(), 0u);
}

// —— merge：分段 / 空洞段跳过 / 段内索引 ——————————————————————————

TEST(DSIdMapTest, MergeSegmentsSkipsHoleSegments) {
    // 片段 A（分区 1）：id 5、id 2^20 + 3（跨段）；片段 B（分区 0）：
    // id 9。段 0 = [0, 2^20)、段 1 = [2^20, 2^21)——两段、段 0 内空洞
    // （id 6/7/8 无条目）保持 kIdMapNoPartition
    DSInstIdPartitionSlice a;
    a.add(CMInstanceId{5}, CMPartitionId{1});
    a.add(CMInstanceId{kIdMapSegmentSize + 3}, CMPartitionId{1});
    DSInstIdPartitionSlice b;
    b.add(CMInstanceId{9}, CMPartitionId{0});

    const CMVector<CMSharedPtr<const DSInstIdPartitionSlice>> slices{test::borrow(a), test::borrow(b)};
    const DSIdPartitionMapResult result = ds_merge_id_partition_slices<CMInstanceId>(slices);

    // 段表：两个非空段起始升序（中间无空洞段——连续占段）
    ASSERT_EQ(result.index.size(), 2u);
    EXPECT_EQ(result.index.id_starts_[0], 0u);
    EXPECT_EQ(result.index.id_starts_[1], kIdMapSegmentSize);
    EXPECT_TRUE(result.index.has_segment(5));
    EXPECT_TRUE(result.index.has_segment(kIdMapSegmentSize + 3));
    EXPECT_FALSE(result.index.has_segment(kIdMapSegmentSize * 2));

    // 段 0：直接索引（pids[id]），空洞哨兵
    ASSERT_EQ(result.segments.size(), 2u);
    EXPECT_EQ(result.segments[0].get_id_start(), 0u);
    ASSERT_EQ(result.segments[0].size(), kIdMapSegmentSize);
    EXPECT_EQ(result.segments[0].partition_of(5), 1u);
    EXPECT_EQ(result.segments[0].partition_of(9), 0u);
    EXPECT_EQ(result.segments[0].partition_of(6), kIdMapNoPartition);
    // 段 1：起始与跨段条目
    EXPECT_EQ(result.segments[1].get_id_start(), kIdMapSegmentSize);
    EXPECT_EQ(result.segments[1].partition_of(kIdMapSegmentSize + 3), 1u);
    EXPECT_EQ(result.segments[1].partition_of(3), kIdMapNoPartition);
}

TEST(DSIdMapTest, MergeDuplicateIdTakesFirst) {
    // 同 id 多片段（S9 primary 恰一不变式下正常数据不触发；确定性防御 =
    // 排序后首个生效——条目按 (id, pid) 升序，即 pid 最小者胜）
    DSInstIdPartitionSlice a;
    a.add(CMInstanceId{4}, CMPartitionId{1});
    DSInstIdPartitionSlice b;
    b.add(CMInstanceId{4}, CMPartitionId{0});
    const CMVector<CMSharedPtr<const DSInstIdPartitionSlice>> slices{test::borrow(a), test::borrow(b)};
    const DSIdPartitionMapResult result = ds_merge_id_partition_slices<CMInstanceId>(slices);
    ASSERT_EQ(result.segments.size(), 1u);
    EXPECT_EQ(result.segments[0].partition_of(4), 0u);
}

TEST(DSIdMapTest, MergeEmptyInputYieldsEmptyMap) {
    DSInstIdPartitionSlice a;
    const CMVector<CMSharedPtr<const DSInstIdPartitionSlice>> slices{test::borrow(a), nullptr};
    const DSIdPartitionMapResult result = ds_merge_id_partition_slices<CMInstanceId>(slices);
    EXPECT_EQ(result.index.size(), 0u);
    EXPECT_EQ(result.segments.size(), 0u);
    EXPECT_FALSE(result.index.has_segment(0));
}

// —— 段查询边界与段表二分 ————————————————————————————————————

TEST(DSIdMapTest, SegmentQueryBoundaries) {
    DSIdPartitionSegment seg;
    seg.id_start_ = kIdMapSegmentSize * 3;
    seg.pids_.assign(static_cast<size_t>(kIdMapSegmentSize),
                     CMPartitionId{kIdMapNoPartition});
    seg.pids_[0] = CMPartitionId{4};                                        // 段首
    seg.pids_[seg.pids_.size() - 1] = CMPartitionId{5};                     // 段末
    EXPECT_EQ(seg.partition_of(kIdMapSegmentSize * 3), 4u);
    EXPECT_EQ(seg.partition_of(kIdMapSegmentSize * 4 - 1), 5u);
    EXPECT_EQ(seg.partition_of(kIdMapSegmentSize * 3 - 1),
              kIdMapNoPartition);                            // 段前越界
    EXPECT_EQ(seg.partition_of(kIdMapSegmentSize * 4),
              kIdMapNoPartition);                            // 段后越界

    // 未配置段长（空段对象防御形态）
    DSIdPartitionSegment empty_seg;
    empty_seg.id_start_ = 0;
    EXPECT_EQ(empty_seg.partition_of(0), kIdMapNoPartition);
}

// —— 序列化往返 ————————————————————————————————————————————

TEST(DSIdMapTest, SerializeRoundTrip) {
    DSInstIdPartitionSlice slice;
    slice.add(CMInstanceId{12}, CMPartitionId{3});
    slice.add(CMInstanceId{kIdMapSegmentSize + 1}, CMPartitionId{0});

    CMString slice_blob;
    FLY_ENCODE(slice, slice_blob);
    DSInstIdPartitionSlice slice_back;
    FLY_DECODE(slice_blob, DSInstIdPartitionSlice, slice_back);
    ASSERT_EQ(slice_back.size(), 2u);
    EXPECT_EQ(slice_back.ids_[0].value(), 12u);
    EXPECT_EQ(slice_back.pids_[1], 0u);

    const CMVector<CMSharedPtr<const DSInstIdPartitionSlice>> slices{test::borrow(slice_back)};
    const DSIdPartitionMapResult result = ds_merge_id_partition_slices<CMInstanceId>(slices);

    CMString index_blob;
    FLY_ENCODE(result.index, index_blob);
    DSIdPartitionIndex index_back;
    FLY_DECODE(index_blob, DSIdPartitionIndex, index_back);
    ASSERT_EQ(index_back.size(), 2u);
    EXPECT_TRUE(index_back.has_segment(12));
    EXPECT_EQ(index_back.find_segment_start(kIdMapSegmentSize + 1),
              kIdMapSegmentSize);
    EXPECT_EQ(index_back.find_segment_start(kIdMapSegmentSize * 5),
              DSIdPartitionIndex::kIdMapNoSegment);

    CMString seg_blob;
    FLY_ENCODE(result.segments[0], seg_blob);
    DSIdPartitionSegment seg_back;
    FLY_DECODE(seg_blob, DSIdPartitionSegment, seg_back);
    ASSERT_EQ(seg_back.size(), kIdMapSegmentSize);
    EXPECT_EQ(seg_back.partition_of(12), 3u);
    EXPECT_EQ(seg_back.partition_of(13), kIdMapNoPartition);
}

}  // namespace
