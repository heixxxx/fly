// _fly_emir_spef.so — emir spef 模块（⑤ spef db）的 Python 绑定。导出
// 解析边界（EXSPFileHeader / EXSPNet / EXSPChunkResult / EXSPFileChunk /
// EXSPFileChunkPlan）+ 换算/合并落库形态（EXSPNetEntry /
// EXSPPartitionSlice / EXSPFileStats / EXSPStatsDelta / EXSPEntrySlice /
// EXSPNetLoad / EXSPPartitionNetLoad / EXSPMergeStats / EXSPSummary）+
// 绑定描述（EXSPFileBinding，同 EXWFFileBinding 形态）+
// EXSPDesignContext（design db 快照共享注入上下文；含覆盖率统计的分区
// 实例连接对象注入口——第 H 节第 4 条）+ 执行函数（sp_plan_file_chunks
// / sp_read_byte_range / sp_parse_* / sp_convert_chunk /
// sp_merge_partition / sp_merge_summary）。
// 类型纪律（DEVELOPMENT_GUIDELINES §16）：强类型 id / enum class 成员禁
// 止 def_ro 直绑（运行期 SystemError）——一律 READONLY_PROPERTY +
// `.value()` 桥，Python 边界保持 int；SPNetKind 在 Python 边界以 int 呈
// 现（0 = D_NET、1 = R_NET）。
// 统计载体（EXSPFileStats / EXSPStatsDelta / EXSPMergeStats）字段可写
//（FLY_EXPORT_ATTR）——流程侧组装面（切块数进逐文件统计〔第 H 节第 5
// 条〕的写入口）与测试构造需要；正式对象与解析产物保持只读。
#include <export/cpp/export_macros.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/spef/cpp/sp_parser.h>
#include <emir/spef/cpp/sp_partition.h>
#include <emir/spef/cpp/sp_types.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <cstdint>
#include <utility>

namespace nb = nanobind;

namespace {

// SPNetKind → int（0 = D_NET、1 = R_NET——Python 边界保持 int，§16）
int sp_net_kind_int(fly::SPNetKind kind) {
    return static_cast<int>(kind);
}

// SPNetLoad → debug 点查 dict 面的三电容 + 位图 + 标记（get_net_load
// 在 Python 层组装；此处不重复）

}  // namespace

FLY_EXPORT_MODULE(_fly_emir_spef) {

// ── 解析边界（sp_types，temp）──

// 文件头（文件入口任务解析产物、逐块任务共享；单位换算系数/分隔符/
// 名字映射表——反序列化消费）
FLY_EXPORT_CLASS(fly::SPFileHeader, "EXSPFileHeader")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("source_file", &fly::SPFileHeader::source_file_)
    FLY_EXPORT_READONLY_ATTR("design_name", &fly::SPFileHeader::design_name_)
    FLY_EXPORT_READONLY_ATTR("version", &fly::SPFileHeader::version_)
    FLY_EXPORT_READONLY_ATTR("divider", &fly::SPFileHeader::divider_)
    FLY_EXPORT_READONLY_ATTR("delimiter", &fly::SPFileHeader::delimiter_)
    FLY_EXPORT_READONLY_ATTR("bus_left", &fly::SPFileHeader::bus_left_)
    FLY_EXPORT_READONLY_ATTR("bus_right", &fly::SPFileHeader::bus_right_)
    FLY_EXPORT_READONLY_ATTR("cap_scale_ff", &fly::SPFileHeader::cap_scale_ff_)
    FLY_EXPORT_READONLY_ATTR("res_scale_ohm",
                             &fly::SPFileHeader::res_scale_ohm_)
    FLY_EXPORT_READONLY_ATTR("time_unit_decl",
                             &fly::SPFileHeader::time_unit_decl_)
    FLY_EXPORT_READONLY_ATTR("cap_unit_decl",
                             &fly::SPFileHeader::cap_unit_decl_)
    FLY_EXPORT_READONLY_ATTR("res_unit_decl",
                             &fly::SPFileHeader::res_unit_decl_)
    FLY_EXPORT_READONLY_ATTR("ind_unit_decl",
                             &fly::SPFileHeader::ind_unit_decl_)
    FLY_EXPORT_READONLY_ATTR("design_flow", &fly::SPFileHeader::design_flow_)
    FLY_EXPORT_READONLY_ATTR("name_map", &fly::SPFileHeader::name_map_)
    FLY_EXPORT_READONLY_ATTR("power_nets", &fly::SPFileHeader::power_nets_)
    FLY_EXPORT_READONLY_ATTR("ground_nets", &fly::SPFileHeader::ground_nets_)
    FLY_EXPORT_READONLY_ATTR("name_map_dup_count",
                             &fly::SPFileHeader::name_map_dup_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPFileHeader);

// 单网段解析产物（聚合量，不保留逐条明细）
FLY_EXPORT_CLASS(fly::SPNet, "EXSPNet")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("name", &fly::SPNet::name_)
    FLY_EXPORT_READONLY_PROPERTY("kind", [](const fly::SPNet& n) {
        return sp_net_kind_int(n.kind_);
    })
    FLY_EXPORT_READONLY_ATTR("total_cap_reported_ff",
                             &fly::SPNet::total_cap_reported_ff_)
    FLY_EXPORT_READONLY_ATTR("ground_cap_ff", &fly::SPNet::ground_cap_ff_)
    FLY_EXPORT_READONLY_ATTR("coupling_cap_ff", &fly::SPNet::coupling_cap_ff_)
    FLY_EXPORT_READONLY_ATTR("value_count", &fly::SPNet::value_count_)
    FLY_EXPORT_READONLY_ATTR("cap_count", &fly::SPNet::cap_count_)
    FLY_EXPORT_READONLY_ATTR("coupling_cap_count",
                             &fly::SPNet::coupling_cap_count_)
    FLY_EXPORT_READONLY_ATTR("res_count", &fly::SPNet::res_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPNet);

// 一次块解析（或整文件解析）的产物：网表 + 全部统计计数（段级兜底的
// 观测面）
FLY_EXPORT_CLASS(fly::SPChunkResult, "EXSPChunkResult")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("nets", &fly::SPChunkResult::nets_)
    FLY_EXPORT_READONLY_ATTR("net_count", &fly::SPChunkResult::net_count_)
    FLY_EXPORT_READONLY_ATTR("dnet_count", &fly::SPChunkResult::dnet_count_)
    FLY_EXPORT_READONLY_ATTR("rnet_count", &fly::SPChunkResult::rnet_count_)
    FLY_EXPORT_READONLY_ATTR("pnet_skip_count",
                             &fly::SPChunkResult::pnet_skip_count_)
    FLY_EXPORT_READONLY_ATTR("broken_net_count",
                             &fly::SPChunkResult::broken_net_count_)
    FLY_EXPORT_READONLY_ATTR("name_map_miss_count",
                             &fly::SPChunkResult::name_map_miss_count_)
    FLY_EXPORT_READONLY_ATTR("dropped_record_count",
                             &fly::SPChunkResult::dropped_record_count_)
    FLY_EXPORT_READONLY_ATTR("multi_value_net_count",
                             &fly::SPChunkResult::multi_value_net_count_)
    FLY_EXPORT_READONLY_ATTR("value_count_max",
                             &fly::SPChunkResult::value_count_max_)
    FLY_EXPORT_READONLY_ATTR("value_index_fallback_count",
                             &fly::SPChunkResult::value_index_fallback_count_)
    FLY_EXPORT_READONLY_ATTR("cap_count", &fly::SPChunkResult::cap_count_)
    FLY_EXPORT_READONLY_ATTR("coupling_cap_count",
                             &fly::SPChunkResult::coupling_cap_count_)
    FLY_EXPORT_READONLY_ATTR("res_count", &fly::SPChunkResult::res_count_)
    FLY_EXPORT_READONLY_ATTR("external_ground_cap_count",
                             &fly::SPChunkResult::external_ground_cap_count_)
    FLY_EXPORT_READONLY_ATTR("external_coupling_cap_count",
                             &fly::SPChunkResult::external_coupling_cap_count_)
    FLY_EXPORT_READONLY_ATTR("conn_count", &fly::SPChunkResult::conn_count_)
    FLY_EXPORT_READONLY_ATTR("conn_pin_cap_obs_sum_ff",
                             &fly::SPChunkResult::conn_pin_cap_obs_sum_ff_)
    FLY_EXPORT_READONLY_ATTR("unknown_direction_count",
                             &fly::SPChunkResult::unknown_direction_count_)
    FLY_EXPORT_READONLY_ATTR("induc_count", &fly::SPChunkResult::induc_count_)
    FLY_EXPORT_READONLY_ATTR("stray_content_count",
                             &fly::SPChunkResult::stray_content_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPChunkResult);

// 单块切片（temp：字节区间）
FLY_EXPORT_CLASS(fly::SPFileChunk, "EXSPFileChunk")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("start", &fly::SPFileChunk::start_)
    FLY_EXPORT_READONLY_ATTR("end", &fly::SPFileChunk::end_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPFileChunk);

// 单文件切块计划（temp；T1 产物——文件头随计划一次产出，免二次整读）
FLY_EXPORT_CLASS(fly::SPFileChunkPlan, "EXSPFileChunkPlan")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("file_name", &fly::SPFileChunkPlan::file_name_)
    FLY_EXPORT_READONLY_ATTR("file_size", &fly::SPFileChunkPlan::file_size_)
    FLY_EXPORT_READONLY_ATTR("header_end", &fly::SPFileChunkPlan::header_end_)
    FLY_EXPORT_READONLY_ATTR("chunks", &fly::SPFileChunkPlan::chunks_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPFileChunkPlan);

// ── 统计载体（可写——流程组装面与测试构造；见文件头注释）──

// 逐文件统计（SPSummary.files_ 条目 + SPStatsDelta 逐块片段）。chunk_
// count 写入口 = 第 H 节第 5 条（汇总任务按文件清单填入）
FLY_EXPORT_CLASS(fly::SPFileStats, "EXSPFileStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("source_file", &fly::SPFileStats::source_file_)
    FLY_EXPORT_ATTR("c_unit_ff_per_unit",
                    &fly::SPFileStats::c_unit_ff_per_unit_)
    FLY_EXPORT_ATTR("net_count", &fly::SPFileStats::net_count_)
    FLY_EXPORT_ATTR("rnet_count", &fly::SPFileStats::rnet_count_)
    FLY_EXPORT_ATTR("pnet_skip_count", &fly::SPFileStats::pnet_skip_count_)
    FLY_EXPORT_ATTR("broken_net_count", &fly::SPFileStats::broken_net_count_)
    FLY_EXPORT_ATTR("name_map_miss_count",
                    &fly::SPFileStats::name_map_miss_count_)
    FLY_EXPORT_ATTR("dropped_record_count",
                    &fly::SPFileStats::dropped_record_count_)
    FLY_EXPORT_ATTR("multi_value_net_count",
                    &fly::SPFileStats::multi_value_net_count_)
    FLY_EXPORT_ATTR("value_count_max", &fly::SPFileStats::value_count_max_)
    FLY_EXPORT_ATTR("value_index_fallback_count",
                    &fly::SPFileStats::value_index_fallback_count_)
    FLY_EXPORT_ATTR("cap_count", &fly::SPFileStats::cap_count_)
    FLY_EXPORT_ATTR("coupling_cap_count",
                    &fly::SPFileStats::coupling_cap_count_)
    FLY_EXPORT_ATTR("res_count", &fly::SPFileStats::res_count_)
    FLY_EXPORT_ATTR("hit_count", &fly::SPFileStats::hit_count_)
    FLY_EXPORT_ATTR("net_name_miss_count",
                    &fly::SPFileStats::net_name_miss_count_)
    FLY_EXPORT_ATTR("pg_net_skip_count",
                    &fly::SPFileStats::pg_net_skip_count_)
    FLY_EXPORT_ATTR("unrouted_net_count",
                    &fly::SPFileStats::unrouted_net_count_)
    FLY_EXPORT_ATTR("strip_miss_count", &fly::SPFileStats::strip_miss_count_)
    FLY_EXPORT_ATTR("unplaced_instance_count",
                    &fly::SPFileStats::unplaced_instance_count_)
    FLY_EXPORT_ATTR("failed_chunk_count",
                    &fly::SPFileStats::failed_chunk_count_)
    FLY_EXPORT_ATTR("chunk_count", &fly::SPFileStats::chunk_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPFileStats);

// 统计片段（T2 每块产出；T4 聚合进 SPSummary。files 的 Python 视图是
// 拷贝——构造逐文件条目走 add_file）
FLY_EXPORT_CLASS(fly::SPStatsDelta, "EXSPStatsDelta")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("files", &fly::SPStatsDelta::files_)
    FLY_EXPORT_DEF("add_file", [](fly::SPStatsDelta& d,
                                  const fly::SPFileStats& f) {
        d.files_.push_back(f);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPStatsDelta);

// T3 每分区合并统计（sp_merge_partition 出参；SPSummary 全局计数的来
// 源。可写——流程侧仅读，测试构造非零替身需要）
FLY_EXPORT_CLASS(fly::SPMergeStats, "EXSPMergeStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("same_scope_overlap_count",
                    &fly::SPMergeStats::same_scope_overlap_count_)
    FLY_EXPORT_ATTR("cross_scope_sum_count",
                    &fly::SPMergeStats::cross_scope_sum_count_)
    FLY_EXPORT_ATTR("cap_mismatch_count",
                    &fly::SPMergeStats::cap_mismatch_count_)
    FLY_EXPORT_ATTR("cap_mismatch_max_rel",
                    &fly::SPMergeStats::cap_mismatch_max_rel_)
    FLY_EXPORT_ATTR("routed_signal_net_count",
                    &fly::SPMergeStats::routed_signal_net_count_)
    FLY_EXPORT_ATTR("covered_net_count",
                    &fly::SPMergeStats::covered_net_count_)
    FLY_EXPORT_ATTR("uncovered_net_count",
                    &fly::SPMergeStats::uncovered_net_count_)
    FLY_EXPORT_ATTR("uncovered_instance_count",
                    &fly::SPMergeStats::uncovered_instance_count_)
    FLY_EXPORT_ATTR("zero_cap_instance_count",
                    &fly::SPMergeStats::zero_cap_instance_count_)
    FLY_EXPORT_ATTR("source_mask_overflow_count",
                    &fly::SPMergeStats::source_mask_overflow_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPMergeStats);

// ── 换算/合并中间与落库形态 ──

// T2 逐块产物单网条目（强类型 net id 桥——§16 禁直绑）
FLY_EXPORT_CLASS(fly::SPNetEntry, "EXSPNetEntry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("net_id", [](const fly::SPNetEntry& e) {
        return e.net_id_.value();
    })
    FLY_EXPORT_READONLY_ATTR("total_cap_reported_ff",
                             &fly::SPNetEntry::total_cap_reported_ff_)
    FLY_EXPORT_READONLY_ATTR("ground_cap_ff", &fly::SPNetEntry::ground_cap_ff_)
    FLY_EXPORT_READONLY_ATTR("coupling_cap_ff",
                             &fly::SPNetEntry::coupling_cap_ff_)
    FLY_EXPORT_READONLY_ATTR("flags", &fly::SPNetEntry::flags_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPNetEntry);

// T2 逐块产物单分区切片
FLY_EXPORT_CLASS(fly::SPPartitionSlice, "EXSPPartitionSlice")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("part_id", [](const fly::SPPartitionSlice& s) {
        return s.part_id_.value();
    })
    FLY_EXPORT_READONLY_ATTR("entries", &fly::SPPartitionSlice::entries_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPPartitionSlice);

// T2 逐块解析产物分区分片（temp；T3 每分区一合并任务按 pid 取用。
// stats 可写——统计载体组装面（流程仅读，测试构造替身需要））
FLY_EXPORT_CLASS(fly::SPEntrySlice, "EXSPEntrySlice")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("file_index", &fly::SPEntrySlice::file_index_)
    FLY_EXPORT_READONLY_ATTR("scope_index", &fly::SPEntrySlice::scope_index_)
    // 本块触及分区 id 枚举（编排观测面；T3 合并直传对象不逐分区取）
    FLY_EXPORT_READONLY_PROPERTY("partition_ids",
                                 [](const fly::SPEntrySlice& s) {
        std::vector<uint32_t> ids;
        ids.reserve(s.partitions_.size());
        for (const auto& [pid, part] : s.partitions_) {
            ids.push_back(pid.value());
        }
        return ids;
    })
    FLY_EXPORT_ATTR("stats", &fly::SPEntrySlice::stats_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPEntrySlice);

// 单网网负载（正式对象 PART_{xp}_{yp}.NET_LOAD 条目）
FLY_EXPORT_CLASS(fly::SPNetLoad, "EXSPNetLoad")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("net_id", [](const fly::SPNetLoad& e) {
        return e.net_id_.value();
    })
    FLY_EXPORT_READONLY_ATTR("total_cap_reported_ff",
                             &fly::SPNetLoad::total_cap_reported_ff_)
    FLY_EXPORT_READONLY_ATTR("ground_cap_ff", &fly::SPNetLoad::ground_cap_ff_)
    FLY_EXPORT_READONLY_ATTR("coupling_cap_ff",
                             &fly::SPNetLoad::coupling_cap_ff_)
    FLY_EXPORT_READONLY_ATTR("source_mask", &fly::SPNetLoad::source_mask_)
    FLY_EXPORT_READONLY_ATTR("flags", &fly::SPNetLoad::flags_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPNetLoad);

// 分区网负载容器（PART_{xp}_{yp}.NET_LOAD 正式对象；条目按 net id 升
// 序——find_net 二分查找）
FLY_EXPORT_CLASS(fly::SPPartitionNetLoad, "EXSPPartitionNetLoad")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("part_id",
                                 [](const fly::SPPartitionNetLoad& t) {
        return t.part_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("size",
                                 [](const fly::SPPartitionNetLoad& t) {
        return static_cast<int>(t.items_.size());
    })
    // 单网点查（debug API 的整区加载后单表查；未命中返回 None）
    FLY_EXPORT_DEF("find_net", [](const fly::SPPartitionNetLoad& t,
                                  uint64_t net_id) {
        const fly::SPNetLoad* e = t.find_net(fly::CMNetId{net_id});
        if (e == nullptr) {
            return std::optional<fly::SPNetLoad>();
        }
        return std::optional<fly::SPNetLoad>(*e);
    })
    // 逐网条目枚举（debug 面遍历；拷贝出）
    FLY_EXPORT_READONLY_PROPERTY("items",
                                 [](const fly::SPPartitionNetLoad& t) {
        return t.items_;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPPartitionNetLoad);

// 汇总（"summary" 正式对象）
FLY_EXPORT_CLASS(fly::SPSummary, "EXSPSummary")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("files", &fly::SPSummary::files_)
    FLY_EXPORT_READONLY_ATTR("routed_signal_net_count",
                             &fly::SPSummary::routed_signal_net_count_)
    FLY_EXPORT_READONLY_ATTR("covered_net_count",
                             &fly::SPSummary::covered_net_count_)
    FLY_EXPORT_READONLY_ATTR("uncovered_net_count",
                             &fly::SPSummary::uncovered_net_count_)
    FLY_EXPORT_READONLY_ATTR("uncovered_instance_count",
                             &fly::SPSummary::uncovered_instance_count_)
    FLY_EXPORT_READONLY_ATTR("zero_cap_instance_count",
                             &fly::SPSummary::zero_cap_instance_count_)
    FLY_EXPORT_READONLY_ATTR("rnet_count", &fly::SPSummary::rnet_count_)
    FLY_EXPORT_READONLY_ATTR("cap_mismatch_count",
                             &fly::SPSummary::cap_mismatch_count_)
    FLY_EXPORT_READONLY_ATTR("cap_mismatch_max_rel",
                             &fly::SPSummary::cap_mismatch_max_rel_)
    FLY_EXPORT_READONLY_ATTR("same_scope_overlap_count",
                             &fly::SPSummary::same_scope_overlap_count_)
    FLY_EXPORT_READONLY_ATTR("cross_scope_sum_count",
                             &fly::SPSummary::cross_scope_sum_count_)
    FLY_EXPORT_READONLY_ATTR("source_mask_overflow_count",
                             &fly::SPSummary::source_mask_overflow_count_)
    FLY_EXPORT_READONLY_ATTR("invalid_binding_count",
                             &fly::SPSummary::invalid_binding_count_)
    FLY_EXPORT_READONLY_ATTR("internal_cap_unit_ff",
                             &fly::SPSummary::internal_cap_unit_ff_)
    FLY_EXPORT_READONLY_ATTR("internal_res_unit_ohm",
                             &fly::SPSummary::internal_res_unit_ohm_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::SPSummary);

// ── T2 换算上下文（design db 快照注入；EXWFDesignContext 同构）──

FLY_EXPORT_CLASS(fly::SPDesignContext, "EXSPDesignContext")
    FLY_EXPORT_INIT()
    FLY_EXPORT_DEF("set_design",
                   [](fly::SPDesignContext& c,
                      std::shared_ptr<fly::DSDesign> design) {
        c.set_design(std::move(design));
    })
    FLY_EXPORT_DEF("add_block_names",
                   [](fly::SPDesignContext& c,
                      std::shared_ptr<const fly::DSBlockNames> names) {
        c.add_block_names(std::move(names));
    })
    FLY_EXPORT_DEF("set_inst_id_map",
                   [](fly::SPDesignContext& c,
                      std::shared_ptr<fly::DSIdPartitionIndex> index) {
        c.set_inst_id_map(std::move(index));
    })
    FLY_EXPORT_DEF("add_inst_segment",
                   [](fly::SPDesignContext& c,
                      std::shared_ptr<fly::DSIdPartitionSegment> seg) {
        c.add_inst_segment(std::move(seg));
    })
    FLY_EXPORT_DEF("set_pg_nets",
                   [](fly::SPDesignContext& c,
                      std::shared_ptr<fly::DSPgNetSet> pg_nets) {
        c.set_pg_nets(std::move(pg_nets));
    })
    FLY_EXPORT_DEF("add_partition_nets",
                   [](fly::SPDesignContext& c,
                      std::shared_ptr<fly::DSPartitionNets> nets) {
        c.add_partition_nets(std::move(nets));
    })
    // 分区实例连接对象注入口（覆盖率统计材料——第 H 节第 4 条：对象
    // 无 part_id_ 字段，显式携带 pid）
    FLY_EXPORT_DEF("add_partition_inst_connections",
                   [](fly::SPDesignContext& c, uint32_t pid,
                      std::shared_ptr<fly::DSPartInstConnections> conns) {
        c.add_partition_inst_connections(fly::CMPartitionId{pid},
                                         std::move(conns));
    });

// 绑定描述（三形态 + strip_prefix 的 C++ 侧；字段可写 + 随任务参数传
// 输）。kind = SPFileBindingKind 枚举定型存储——枚举成员禁直绑（§16
// 类型纪律），property 桥 Python 边界保持 int（0/1/2，值域校验）
FLY_EXPORT_CLASS(fly::SPFileBinding, "EXSPFileBinding")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("kind", [](const fly::SPFileBinding& b) {
        return static_cast<int>(b.kind);
    })
    FLY_EXPORT_DEF("set_kind", [](fly::SPFileBinding& b, int kind) {
        if (kind < 0 ||
            kind > static_cast<int>(fly::SPFileBindingKind::BLOCK_CELL)) {
            throw nb::value_error(
                "EXSPFileBinding.kind must be 0 (path) / 1 (block_inst) / "
                "2 (block_cell)");
        }
        b.kind = static_cast<fly::SPFileBindingKind>(kind);
    })
    FLY_EXPORT_ATTR("block_inst", &fly::SPFileBinding::block_inst)
    FLY_EXPORT_ATTR("block_cell", &fly::SPFileBinding::block_cell)
    FLY_EXPORT_ATTR("strip_prefix", &fly::SPFileBinding::strip_prefix);

// ── 执行函数（plan §6 任务链的 C++ 底座）──

// 头段解析（内存文本；文件级语法错误抛 RuntimeError——文件形态错在文
// 件入口任务失败透出）
FLY_EXPORT_FUNCTION("sp_parse_header_text",
                    [](const CMString& text, const CMString& source_name) {
    return fly::sp_parse_header_text(text, source_name);
});

// 文件头解析（整读；不可读 / gzip 魔数 / 文件级错误抛 RuntimeError）
FLY_EXPORT_FUNCTION("sp_parse_header_file", [](const CMString& path) {
    return fly::sp_parse_header_file(path);
});

// T1：逐文件切块（三参 C++ 接口的绑定层二元组——返回 (切块计划, 文件
// 头)：一次扫描同时产出切块计划与文件头〔免二次整读大文件〕；文件不
// 可读 / gzip 魔数 / 文件级语法错误〔缺单位、非法单位词等〕抛
// RuntimeError）
FLY_EXPORT_FUNCTION("sp_plan_file_chunks",
                    [](const CMString& path, uint64_t chunk_size) {
    fly::SPFileHeader header;
    auto plan = fly::sp_plan_file_chunks(path, chunk_size, header);
    return nb::make_tuple(nb::cast(std::move(plan)),
                          nb::cast(std::move(header)));
});

// 字节区间读取（C1：块任务把切块计划的字节区间读成文本；越界/不可读
// 抛 RuntimeError）
FLY_EXPORT_FUNCTION("sp_read_byte_range",
                    [](const CMString& path, uint64_t start, uint64_t end) {
    return fly::sp_read_byte_range(path, start, end);
});

// 逐块解析（块文本 + 文件头；段级破损跳过 + 计数不抛出）
FLY_EXPORT_FUNCTION("sp_parse_chunk_text",
                    [](const CMString& text, const fly::SPFileHeader& header,
                       uint32_t value_index, bool value_index_given,
                       const CMString& source_name) {
    return fly::sp_parse_chunk_text(text, header, value_index,
                                    value_index_given, source_name);
});

// 整份内存文本解析（头部产物随二元组返回）
FLY_EXPORT_FUNCTION("sp_parse_spef_text",
                    [](const CMString& text, uint32_t value_index,
                       bool value_index_given, const CMString& source_name) {
    fly::SPFileHeader header;
    auto result =
        fly::sp_parse_spef_text(text, header, value_index, value_index_given,
                                source_name);
    return nb::make_tuple(nb::cast(std::move(result)),
                          nb::cast(std::move(header)));
});

// 整文件解析（头部产物随二元组返回）
FLY_EXPORT_FUNCTION("sp_parse_spef_file",
                    [](const CMString& path, uint32_t value_index,
                       bool value_index_given) {
    fly::SPFileHeader header;
    auto result = fly::sp_parse_spef_file(path, header, value_index,
                                          value_index_given);
    return nb::make_tuple(nb::cast(std::move(result)),
                          nb::cast(std::move(header)));
});

// T2：逐块换算（bind 写法——FLY_EXPORT_DEF 仅类内，模块级函数用此形
// 态；名字换算 + 分区路由 → 各分区 SPEntrySlice）
m.def("sp_convert_chunk",
      [](const fly::SPDesignContext& ctx, const fly::SPFileHeader& header,
         const fly::SPChunkResult& parsed, uint32_t file_index,
         uint32_t scope_index, const fly::SPFileBinding& binding,
         const CMString& source_file) {
          return fly::sp_convert_chunk(ctx, header, parsed, file_index,
                                       scope_index, binding, source_file);
      });

// T3：每分区合并（ctx = design db 快照上下文——覆盖率统计材料〔本区实
// 例连接对象 + 已布线信号网 id 集合，第 H 节第 4 条〕；slices =
// EXSPEntrySlice 列表，序 = 文件序 × 块序；返回 (分区网负载容器, 合并
// 统计) 二元组）
m.def("sp_merge_partition",
      [](const fly::SPDesignContext& ctx, nb::list slices, uint32_t pid) {
          fly::CMVector<const fly::SPEntrySlice*> ptrs;
          for (nb::handle item : slices) {
              ptrs.push_back(&nb::cast<const fly::SPEntrySlice&>(item));
          }
          fly::SPMergeStats stats;
          auto obj = fly::sp_merge_partition(ctx, ptrs,
                                             fly::CMPartitionId{pid},
                                             stats);
          return nb::make_tuple(nb::cast(std::move(obj)),
                                nb::cast(std::move(stats)));
      });

// T4：summary 聚合（deltas = EXSPStatsDelta 列表；merge_stats =
// EXSPMergeStats 列表；invalid_binding_count = 绑定目标未命中被跳过的
// 文件数——flow 侧判定传入；chunk_count 由流程层在返回后按文件清单填
// 入〔第 H 节第 5 条〕）
m.def("sp_merge_summary",
      [](nb::list deltas, uint64_t invalid_binding_count,
         nb::list merge_stats) {
          fly::CMVector<const fly::SPStatsDelta*> delta_ptrs;
          for (nb::handle item : deltas) {
              delta_ptrs.push_back(
                  &nb::cast<const fly::SPStatsDelta&>(item));
          }
          fly::CMVector<fly::SPMergeStats> stats;
          for (nb::handle item : merge_stats) {
              stats.push_back(
                  nb::cast<const fly::SPMergeStats&>(item));
          }
          return fly::sp_merge_summary(delta_ptrs, invalid_binding_count,
                                       stats);
      });

}  // FLY_EXPORT_MODULE
