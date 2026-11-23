// _fly_emir_waveform.so — emir waveform 模块（⑧ waveform db）的 Python
// 绑定。导出落库形态（EXWFPartitionWaveform / EXWFPartitionActivity /
// EXWFSummary 正式对象）+ 中间形态（EXWFEntrySlice / EXWFVarTable /
// EXWFFileChunkPlan / EXWFStatsDelta / EXWFWaveformKey 等，flow 内部
// 用）+ 执行函数（wf_plan_file_chunks / wf_convert_chunk /
// wf_merge_partition / wf_merge_summary / wf_materialize_activity /
// wf_empty_partition_waveform）+ EXWFDesignContext（design db 快照共享
// 注入上下文）+ wf_apply_overrides（混合模式覆盖，裁定 IV-2）。
// 裁定 V 重构批次适配：EXWFChunkSnapshot 绑定已删（WFChunkSnapshot 类
// 型随块首快照删除而消失——plan §5.1/裁定 V-3）；wf_merge_partition 增
// ctx 首参（§7.3 四级回落判定材料）。
// 类型纪律（DEVELOPMENT_GUIDELINES §16）：强类型 id / enum class 成员禁止
// def_ro 直绑（运行期 SystemError）——一律 READONLY_PROPERTY + `.value()`
// 桥，Python 边界保持 int；WFValue 四态在 Python 边界以 "0"/"1"/"x"/"z"
// 字符呈现（plan §3.3 debug 面口径）。
#include <export/cpp/export_macros.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/waveform/cpp/wf_parser.h>
#include <emir/waveform/cpp/wf_partition.h>
#include <emir/waveform/cpp/wf_types.h>

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

// WFValue → debug 面四态字符（plan §3.3：value_str ∈ "0"/"1"/"x"/"z"）
const char* wf_value_str(fly::WFValue v) {
    switch (v) {
        case fly::WFValue::ZERO:
            return "0";
        case fly::WFValue::ONE:
            return "1";
        case fly::WFValue::X:
            return "x";
        case fly::WFValue::Z:
        default:
            return "z";
    }
}

nb::object waveform_events(const fly::WFWaveform& w) {
    nb::list out;
    for (const fly::WFEvent& e : w.events_) {
        out.append(nb::make_tuple(e.time_fs_, wf_value_str(e.value_)));
    }
    return nb::cast(std::move(out));
}

// WFPinActivity 只读 summary 口（对齐 TMPinTiming.summary 先例：强类型 id
// 桥 + 四态时长/沿计数元组）
nb::object pin_activity_summary(const fly::WFPinActivity& p) {
    return nb::make_tuple(p.pin_id_.value(), p.waveform_id_, p.t0_fs_,
                          p.t1_fs_, p.tx_fs_, p.tz_fs_, p.rise_count_,
                          p.fall_count_);
}

}  // namespace

FLY_EXPORT_MODULE(_fly_emir_waveform) {

// ── 解析边界（wf_types）——WFVcdFile 经 wf_plan_file_chunks 内部消化，
//    不上 Python 面（同 timing TMTimingFile 先例）──

// 单块切片（temp；裁定 V-3 块首快照已删——字节区间 + 时间范围）
FLY_EXPORT_CLASS(fly::WFFileChunk, "EXWFFileChunk")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("start", &fly::WFFileChunk::start_)
    FLY_EXPORT_READONLY_ATTR("end", &fly::WFFileChunk::end_)
    FLY_EXPORT_READONLY_ATTR("t_start_fs", &fly::WFFileChunk::t_start_fs_)
    FLY_EXPORT_READONLY_ATTR("t_end_fs", &fly::WFFileChunk::t_end_fs_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFFileChunk);

// 单文件切块计划（temp；T1 产物——档位随头部解析随计划产出，随任务
// 参数传递）
FLY_EXPORT_CLASS(fly::WFFileChunkPlan, "EXWFFileChunkPlan")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("file_name", &fly::WFFileChunkPlan::file_name_)
    FLY_EXPORT_READONLY_ATTR("file_size", &fly::WFFileChunkPlan::file_size_)
    FLY_EXPORT_READONLY_ATTR("timescale_fs",
                             &fly::WFFileChunkPlan::timescale_fs_)
    FLY_EXPORT_READONLY_ATTR("timescale_sec",
                             &fly::WFFileChunkPlan::timescale_sec_)
    FLY_EXPORT_READONLY_ATTR("header_start",
                             &fly::WFFileChunkPlan::header_start_)
    FLY_EXPORT_READONLY_ATTR("header_end", &fly::WFFileChunkPlan::header_end_)
    FLY_EXPORT_READONLY_ATTR("chunks", &fly::WFFileChunkPlan::chunks_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFFileChunkPlan);

// 单文件 var 表（temp；文件入口任务产出、逐块任务共享——反序列化消费前
// 必须调 rebuild_indexes 重建运行时索引）
FLY_EXPORT_CLASS(fly::WFVarTable, "EXWFVarTable")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("vars", &fly::WFVarTable::vars_)
    FLY_EXPORT_READONLY_ATTR("track_ids", &fly::WFVarTable::track_ids_)
    FLY_EXPORT_READONLY_ATTR("track_widths", &fly::WFVarTable::track_widths_)
    FLY_EXPORT_DEF("rebuild_indexes",
                   [](fly::WFVarTable& t) { t.rebuild_indexes(); })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFVarTable);

// 波形身份键（§7 两形态；T2 分片内传递，debug/测试观察面）
FLY_EXPORT_CLASS(fly::WFWaveformKey, "EXWFWaveformKey")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("is_net",
                                 [](const fly::WFWaveformKey& k) {
        return k.is_net_;
    })
    FLY_EXPORT_READONLY_PROPERTY("net_id",
                                 [](const fly::WFWaveformKey& k) {
        return k.net_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("inst_id",
                                 [](const fly::WFWaveformKey& k) {
        return k.inst_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("pin_id",
                                 [](const fly::WFWaveformKey& k) {
        return k.pin_id_.value();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFWaveformKey);

// ── 统计（逐文件 + 片段 + 汇总）──

FLY_EXPORT_CLASS(fly::WFFileStats, "EXWFFileStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("source_file", &fly::WFFileStats::source_file_)
    FLY_EXPORT_READONLY_ATTR("timescale_sec",
                             &fly::WFFileStats::timescale_sec_)
    FLY_EXPORT_READONLY_ATTR("t_start_fs", &fly::WFFileStats::t_start_fs_)
    FLY_EXPORT_READONLY_ATTR("t_end_fs", &fly::WFFileStats::t_end_fs_)
    FLY_EXPORT_READONLY_ATTR("event_count", &fly::WFFileStats::event_count_)
    FLY_EXPORT_READONLY_ATTR("hit_count", &fly::WFFileStats::hit_count_)
    FLY_EXPORT_READONLY_ATTR("net_name_miss_count",
                             &fly::WFFileStats::net_name_miss_count_)
    FLY_EXPORT_READONLY_ATTR("inst_pin_miss_count",
                             &fly::WFFileStats::inst_pin_miss_count_)
    FLY_EXPORT_READONLY_ATTR("dangling_net_count",
                             &fly::WFFileStats::dangling_net_count_)
    FLY_EXPORT_READONLY_ATTR("pg_net_skip_count",
                             &fly::WFFileStats::pg_net_skip_count_)
    FLY_EXPORT_READONLY_ATTR("unplaced_instance_count",
                             &fly::WFFileStats::unplaced_instance_count_)
    FLY_EXPORT_READONLY_ATTR("strip_miss_count",
                             &fly::WFFileStats::strip_miss_count_)
    FLY_EXPORT_READONLY_ATTR("unsupported_count",
                             &fly::WFFileStats::unsupported_count_)
    FLY_EXPORT_READONLY_ATTR("width_mismatch_count",
                             &fly::WFFileStats::width_mismatch_count_)
    FLY_EXPORT_READONLY_ATTR("failed_chunk_count",
                             &fly::WFFileStats::failed_chunk_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFFileStats);

FLY_EXPORT_CLASS(fly::WFStatsDelta, "EXWFStatsDelta")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("files", &fly::WFStatsDelta::files_)
    FLY_EXPORT_READONLY_ATTR("multi_driver_net_count",
                             &fly::WFStatsDelta::multi_driver_net_count_)
    FLY_EXPORT_READONLY_ATTR("x_z_event_count",
                             &fly::WFStatsDelta::x_z_event_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFStatsDelta);

FLY_EXPORT_CLASS(fly::WFSummary, "EXWFSummary")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("files", &fly::WFSummary::files_)
    FLY_EXPORT_READONLY_ATTR("total_duration_fs",
                             &fly::WFSummary::total_duration_fs_)
    FLY_EXPORT_READONLY_ATTR("total_event_count",
                             &fly::WFSummary::total_event_count_)
    FLY_EXPORT_READONLY_ATTR("total_hit_count",
                             &fly::WFSummary::total_hit_count_)
    FLY_EXPORT_READONLY_ATTR("net_name_miss_count",
                             &fly::WFSummary::net_name_miss_count_)
    FLY_EXPORT_READONLY_ATTR("inst_pin_miss_count",
                             &fly::WFSummary::inst_pin_miss_count_)
    FLY_EXPORT_READONLY_ATTR("dangling_net_count",
                             &fly::WFSummary::dangling_net_count_)
    FLY_EXPORT_READONLY_ATTR("pg_net_skip_count",
                             &fly::WFSummary::pg_net_skip_count_)
    FLY_EXPORT_READONLY_ATTR("unplaced_instance_count",
                             &fly::WFSummary::unplaced_instance_count_)
    FLY_EXPORT_READONLY_ATTR("strip_miss_count",
                             &fly::WFSummary::strip_miss_count_)
    FLY_EXPORT_READONLY_ATTR("unsupported_count",
                             &fly::WFSummary::unsupported_count_)
    FLY_EXPORT_READONLY_ATTR("width_mismatch_count",
                             &fly::WFSummary::width_mismatch_count_)
    FLY_EXPORT_READONLY_ATTR("waveform_count",
                             &fly::WFSummary::waveform_count_)
    FLY_EXPORT_READONLY_ATTR("overlap_override_count",
                             &fly::WFSummary::overlap_override_count_)
    FLY_EXPORT_READONLY_ATTR("time_anomaly_count",
                             &fly::WFSummary::time_anomaly_count_)
    FLY_EXPORT_READONLY_ATTR("x_z_event_count",
                             &fly::WFSummary::x_z_event_count_)
    FLY_EXPORT_READONLY_ATTR("multi_driver_net_count",
                             &fly::WFSummary::multi_driver_net_count_)
    FLY_EXPORT_READONLY_ATTR("failed_chunk_count",
                             &fly::WFSummary::failed_chunk_count_)
    FLY_EXPORT_READONLY_ATTR("invalid_binding_count",
                             &fly::WFSummary::invalid_binding_count_)
    // 混合模式覆盖条目数（裁定 IV-2——T3 侧计数传入）
    FLY_EXPORT_READONLY_ATTR("override_entry_count",
                             &fly::WFSummary::override_entry_count_)
    // settings 元数据（§4：default 不物化——消费侧展开依据）
    FLY_EXPORT_READONLY_ATTR("default_toggle_rate",
                             &fly::WFSummary::default_toggle_rate_)
    FLY_EXPORT_READONLY_ATTR("default_static_probability",
                             &fly::WFSummary::default_static_probability_)
    FLY_EXPORT_READONLY_ATTR("duration_sec", &fly::WFSummary::duration_sec_)
    FLY_EXPORT_READONLY_ATTR("x_z_handling", &fly::WFSummary::x_z_handling_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFSummary);

// settings 元数据载体（flow 侧构造——T4 汇总入参；可写属性随任务参数
// 组装，§4 键形到 C++ 侧的唯一落点）
FLY_EXPORT_CLASS(fly::WFSettingsMeta, "EXWFSettingsMeta")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("default_toggle_rate",
                    &fly::WFSettingsMeta::default_toggle_rate_)
    FLY_EXPORT_ATTR("default_static_probability",
                    &fly::WFSettingsMeta::default_static_probability_)
    FLY_EXPORT_ATTR("duration_sec", &fly::WFSettingsMeta::duration_sec_)
    FLY_EXPORT_ATTR("x_z_handling", &fly::WFSettingsMeta::x_z_handling_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFSettingsMeta);

// settings 逐实体覆盖条目（§4 overrides 值；settings-only 物化任务组装）
FLY_EXPORT_CLASS(fly::WFActivityOverride, "EXWFActivityOverride")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("name", &fly::WFActivityOverride::name_)
    FLY_EXPORT_ATTR("toggle_rate", &fly::WFActivityOverride::toggle_rate_)
    FLY_EXPORT_ATTR("static_probability",
                    &fly::WFActivityOverride::static_probability_)
    FLY_EXPORT_ATTR("rise_fraction",
                    &fly::WFActivityOverride::rise_fraction_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFActivityOverride);

// ── 落库形态（正式对象）──

FLY_EXPORT_CLASS(fly::WFWaveform, "EXWFWaveform")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("waveform_id", &fly::WFWaveform::waveform_id_)
    FLY_EXPORT_READONLY_ATTR("t_start", &fly::WFWaveform::t_start_fs_)
    FLY_EXPORT_READONLY_ATTR("t_end", &fly::WFWaveform::t_end_fs_)
    FLY_EXPORT_READONLY_PROPERTY("events",
                                 [](const fly::WFWaveform& w) {
        return waveform_events(w);
    })
    FLY_EXPORT_READONLY_PROPERTY("size",
                                 [](const fly::WFWaveform& w) {
        return static_cast<int>(w.events_.size());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFWaveform);

FLY_EXPORT_CLASS(fly::WFPinActivity, "EXWFPinActivity")
    FLY_EXPORT_INIT()
    // 强类型 id 桥（§16 类型纪律：禁止 def_ro 直绑 StrongIdT）
    FLY_EXPORT_READONLY_PROPERTY("pin_id",
                                 [](const fly::WFPinActivity& p) {
        return p.pin_id_.value();
    })
    FLY_EXPORT_READONLY_ATTR("waveform_id", &fly::WFPinActivity::waveform_id_)
    FLY_EXPORT_READONLY_ATTR("t0", &fly::WFPinActivity::t0_fs_)
    FLY_EXPORT_READONLY_ATTR("t1", &fly::WFPinActivity::t1_fs_)
    FLY_EXPORT_READONLY_ATTR("tx", &fly::WFPinActivity::tx_fs_)
    FLY_EXPORT_READONLY_ATTR("tz", &fly::WFPinActivity::tz_fs_)
    FLY_EXPORT_READONLY_ATTR("rise_count", &fly::WFPinActivity::rise_count_)
    FLY_EXPORT_READONLY_ATTR("fall_count", &fly::WFPinActivity::fall_count_)
    FLY_EXPORT_READONLY_PROPERTY("summary",
                                 [](const fly::WFPinActivity& p) {
        return pin_activity_summary(p);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFPinActivity);

FLY_EXPORT_CLASS(fly::WFInstanceActivity, "EXWFInstanceActivity")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("pins",
                                 [](const fly::WFInstanceActivity& t) {
        return t.pins_;  // 拷贝出（实例引脚个位数~几十，拷贝成本可忽略）
    })
    // 单 pin 点查（未命中返回 None）
    FLY_EXPORT_DEF("find_pin", [](const fly::WFInstanceActivity& t,
                                  uint32_t pin_id) {
        const fly::WFPinActivity* p = t.find_pin(fly::CMPinId{pin_id});
        if (p == nullptr) {
            return std::optional<fly::WFPinActivity>();
        }
        return std::optional<fly::WFPinActivity>(*p);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFInstanceActivity);

FLY_EXPORT_CLASS(fly::WFPartitionActivity, "EXWFPartitionActivity")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("part_id",
                                 [](const fly::WFPartitionActivity& t) {
        return t.part_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("size",
                                 [](const fly::WFPartitionActivity& t) {
        return static_cast<int>(t.size());
    })
    // 单实例点查（debug API 的整区加载后单表查；未命中返回 None）
    FLY_EXPORT_DEF("instance_at", [](const fly::WFPartitionActivity& t,
                                     uint64_t inst_id) {
        const auto it = t.items_.find(fly::CMInstanceId{inst_id});
        if (it == t.items_.end()) {
            return std::optional<fly::WFInstanceActivity>();
        }
        return std::optional<fly::WFInstanceActivity>(it->second);
    })
    // 实例 id 枚举（debug 面遍历；id 边界 int 交换）
    FLY_EXPORT_READONLY_PROPERTY("instance_ids",
                                 [](const fly::WFPartitionActivity& t) {
        std::vector<uint64_t> ids;
        ids.reserve(t.items_.size());
        for (const auto& [inst_id, activity] : t.items_) {
            ids.push_back(inst_id.value());
        }
        return ids;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFPartitionActivity);

FLY_EXPORT_CLASS(fly::WFPartitionWaveform, "EXWFPartitionWaveform")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("part_id",
                                 [](const fly::WFPartitionWaveform& t) {
        return t.part_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("waveform_count",
                                 [](const fly::WFPartitionWaveform& t) {
        return static_cast<int>(t.waveform_count());
    })
    // 单波形点查（id = 容器数组下标；越界 ValueError——at 语义）
    FLY_EXPORT_DEF("waveform_at", [](const fly::WFPartitionWaveform& t,
                                     uint32_t waveform_id) {
        return t.waveforms_.at(waveform_id);
    })
    // 波形实体枚举（debug 面遍历；拷贝出）
    FLY_EXPORT_READONLY_PROPERTY("waveforms",
                                 [](const fly::WFPartitionWaveform& t) {
        return t.waveforms_;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFPartitionWaveform);

// ── T2 分片（temp 对象）──

FLY_EXPORT_CLASS(fly::WFEntrySlice, "EXWFEntrySlice")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("file_index", &fly::WFEntrySlice::file_index_)
    FLY_EXPORT_READONLY_ATTR("binding_kind", &fly::WFEntrySlice::binding_kind_)
    // 本块触及分区 id 枚举（编排观测面；T3 合并直传对象不逐分区取）
    FLY_EXPORT_READONLY_PROPERTY("partition_ids",
                                 [](const fly::WFEntrySlice& s) {
        std::vector<uint32_t> ids;
        ids.reserve(s.partitions_.size());
        for (const auto& [pid, part] : s.partitions_) {
            ids.push_back(pid.value());
        }
        return ids;
    })
    FLY_EXPORT_READONLY_ATTR("stats", &fly::WFEntrySlice::stats_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::WFEntrySlice);

// ── T2 换算上下文（design db 快照注入）──

FLY_EXPORT_CLASS(fly::WFDesignContext, "EXWFDesignContext")
    FLY_EXPORT_INIT()
    FLY_EXPORT_DEF("set_design",
                   [](fly::WFDesignContext& c,
                      std::shared_ptr<fly::DSDesign> design) {
        c.set_design(std::move(design));
    })
    FLY_EXPORT_DEF("add_block_names",
                   [](fly::WFDesignContext& c,
                      std::shared_ptr<const fly::DSBlockNames> names) {
        c.add_block_names(std::move(names));
    })
    FLY_EXPORT_DEF("set_inst_id_map",
                   [](fly::WFDesignContext& c,
                      std::shared_ptr<fly::DSIdPartitionIndex> index) {
        c.set_inst_id_map(std::move(index));
    })
    FLY_EXPORT_DEF("add_inst_segment",
                   [](fly::WFDesignContext& c,
                      std::shared_ptr<fly::DSIdPartitionSegment> seg) {
        c.add_inst_segment(std::move(seg));
    })
    FLY_EXPORT_DEF("set_pg_nets",
                   [](fly::WFDesignContext& c,
                      std::shared_ptr<fly::DSPgNetSet> pg_nets) {
        c.set_pg_nets(std::move(pg_nets));
    })
    FLY_EXPORT_DEF("add_partition_nets",
                   [](fly::WFDesignContext& c,
                      std::shared_ptr<fly::DSPartitionNets> nets) {
        c.add_partition_nets(std::move(nets));
    });

// 绑定描述（§2 形态描述符的 C++ 侧；字段可写 + 随任务参数传输）。kind
// = WFFileBindingKind 枚举定型存储——枚举成员禁直绑（§16 类型纪律），
// property 桥 Python 边界保持 int（0/1/2，值域校验）
FLY_EXPORT_CLASS(fly::WFFileBinding, "EXWFFileBinding")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("kind",
                                 [](const fly::WFFileBinding& b) {
        return static_cast<int>(b.kind);
    })
    FLY_EXPORT_DEF("set_kind", [](fly::WFFileBinding& b, int kind) {
        if (kind < 0 ||
            kind > static_cast<int>(fly::WFFileBindingKind::BLOCK_CELL)) {
            throw nb::value_error(
                "EXWFFileBinding.kind must be 0 (path) / 1 (block_inst) / "
                "2 (block_cell)");
        }
        b.kind = static_cast<fly::WFFileBindingKind>(kind);
    })
    FLY_EXPORT_ATTR("block_inst", &fly::WFFileBinding::block_inst)
    FLY_EXPORT_ATTR("block_cell", &fly::WFFileBinding::block_cell)
    FLY_EXPORT_ATTR("strip_prefix", &fly::WFFileBinding::strip_prefix);

// ── 执行函数（plan §6 任务链的 C++ 底座）──

// T1：逐文件切块（出参 var 表——返回 (切块计划, var 表) 二元组；文件不
// 可读/gzip 魔数/流级语法错误〔含缺失 $timescale〕抛 RuntimeError——
// 文件形态错在文件入口任务失败透出；档位随切块计划 timescale_fs/
// timescale_sec 下发）
FLY_EXPORT_FUNCTION("wf_plan_file_chunks",
                    [](const CMString& path, uint64_t chunk_size) {
    fly::WFVarTable vars;
    auto plan = fly::wf_plan_file_chunks(path, chunk_size, vars);
    return nb::make_tuple(nb::cast(std::move(plan)),
                          nb::cast(std::move(vars)));
});

// T2：逐块换算（bind 写法——FLY_EXPORT_DEF 仅类内，模块级函数用此形态；
// vars 须经 rebuild_indexes 重建运行时索引——反序列化不携带运行时 map）
m.def("wf_convert_chunk",
      [](const fly::WFDesignContext& ctx, const CMString& path,
         const fly::WFFileChunk& chunk, const fly::WFVarTable& vars,
         uint64_t timescale_fs, const fly::WFFileBinding& binding,
         uint32_t file_index) {
          return fly::wf_convert_chunk(ctx, path, chunk, vars, timescale_fs,
                                       binding, file_index);
      });

// T3：每分区合并（ctx = design db 快照上下文——§7.3 四级回落判定材料；
// slices = EXWFEntrySlice 列表，序 = 文件序 × 块序；返回 (波形容器, 统
// 计容器, overlap_override_count, time_anomaly_count) 四元组）
m.def("wf_merge_partition",
      [](const fly::WFDesignContext& ctx, nb::list slices, uint32_t pid) {
          fly::CMVector<const fly::WFEntrySlice*> ptrs;
          for (nb::handle item : slices) {
              ptrs.push_back(&nb::cast<const fly::WFEntrySlice&>(item));
          }
          fly::WFPartitionWaveform waveform;
          fly::WFPartitionActivity activity;
          uint64_t overlap = 0;
          uint64_t anomaly = 0;
          fly::wf_merge_partition(ctx, ptrs, fly::CMPartitionId{pid},
                                  waveform, activity, overlap, anomaly);
          return nb::make_tuple(nb::cast(std::move(waveform)),
                                nb::cast(std::move(activity)), overlap,
                                anomaly);
      });

// T4：summary 聚合（deltas = EXWFStatsDelta 列表；waveform/overlap/
// anomaly/override_entry = T3 侧合计；invalid_binding_count = 绑定目标
// 未命中被跳过的文件数——flow 侧判定传入；settings = 元数据）
m.def("wf_merge_summary",
      [](nb::list deltas, uint64_t waveform_count,
         uint64_t overlap_override_count, uint64_t time_anomaly_count,
         uint64_t invalid_binding_count, uint64_t override_entry_count,
         const fly::WFSettingsMeta& settings) {
          fly::CMVector<const fly::WFStatsDelta*> ptrs;
          for (nb::handle item : deltas) {
              ptrs.push_back(&nb::cast<const fly::WFStatsDelta&>(item));
          }
          return fly::wf_merge_summary(ptrs, waveform_count,
                                       overlap_override_count,
                                       time_anomaly_count,
                                       invalid_binding_count,
                                       override_entry_count, settings);
      });

// 混合模式覆盖应用（裁定 IV-2）：T3 合并产物统计容器上替换/新建命中条
// 目（换算公式同 settings-only；waveform_id 引用保留）；inst_ids =
// design db 本分区实例 id 集（成员过滤）；返回 (覆盖条目数, 本区命中条
// 目名列表) 二元组（未解析的权威判定在 flow 侧聚合）
m.def("wf_apply_overrides",
      [](const fly::WFDesignContext& ctx, uint32_t pid, nb::list inst_ids,
         fly::WFPartitionActivity& activity, nb::list overrides,
         uint64_t duration_fs) {
          fly::CMVector<fly::CMInstanceId> ids;
          for (nb::handle item : inst_ids) {
              ids.push_back(fly::CMInstanceId{nb::cast<uint64_t>(item)});
          }
          fly::CMVector<fly::WFActivityOverride> ovs;
          for (nb::handle item : overrides) {
              ovs.push_back(
                  nb::cast<const fly::WFActivityOverride&>(item));
          }
          fly::CMVector<CMString> applied;
          const uint64_t count = fly::wf_apply_overrides(
              ctx, fly::CMPartitionId{pid}, ids, activity, ovs,
              duration_fs, applied);
          return nb::make_tuple(count, nb::cast(std::move(applied)));
      });

// settings-only 物化（plan §6 settings-only 模式）：inst_ids = design db
// 本分区实例 id 集（INSTANCES 对象枚举）；overrides = EXWFActivityOverride
// 列表；返回 (统计容器, 未解析条目数, 本区物化成功条目名列表) 三元组
//（未解析的权威判定在 flow 侧聚合——全部分区都未物化的条目才计）
m.def("wf_materialize_activity",
      [](const fly::WFDesignContext& ctx, uint32_t pid, nb::list inst_ids,
         nb::list overrides, uint64_t duration_fs) {
          fly::CMVector<fly::CMInstanceId> ids;
          for (nb::handle item : inst_ids) {
              ids.push_back(fly::CMInstanceId{nb::cast<uint64_t>(item)});
          }
          fly::CMVector<fly::WFActivityOverride> ovs;
          for (nb::handle item : overrides) {
              ovs.push_back(
                  nb::cast<const fly::WFActivityOverride&>(item));
          }
          uint64_t unresolved = 0;
          fly::CMVector<CMString> resolved;
          auto activity = fly::wf_materialize_activity(
              ctx, fly::CMPartitionId{pid}, ids, ovs, duration_fs,
              unresolved, resolved);
          return nb::make_tuple(nb::cast(std::move(activity)), unresolved,
                                nb::cast(std::move(resolved)));
      });

// settings-only 空波形容器构造口（每分区照常产出空对象——消费接口恒定）
FLY_EXPORT_FUNCTION("wf_empty_partition_waveform", [](uint32_t pid) {
    return fly::wf_empty_partition_waveform(fly::CMPartitionId{pid});
});

}  // FLY_EXPORT_MODULE
