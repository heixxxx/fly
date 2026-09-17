// _fly_emir_timing.so — emir timing 模块（⑦ timing db）的 Python 绑定。
// 导出落库形态（EXTMPartitionTiming / EXTMClockTable / EXTMSummary 正式
// 对象 + EXTMChunkPlan / EXTMEntrySlice / EXTMClockRemap 中间对象，
// FLY_EXPORT_SERIALIZE_PICKLE 支持 pickle 落 db）+ T1/T2/T3/T4 执行函数
// + EXTMDesignContext（design db 快照共享注入上下文）。
// 类型纪律（DEVELOPMENT_GUIDELINES §16）：强类型 id / enum class 成员禁止
// def_ro 直绑（运行期 SystemError）——一律 READONLY_PROPERTY + `.value()`
// 桥，Python 边界保持 int。
#include <export/cpp/export_macros.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/timing/cpp/tm_parser.h>
#include <emir/timing/cpp/tm_partition.h>
#include <emir/timing/cpp/tm_types.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <utility>

namespace nb = nanobind;

namespace {

// TMPinTiming 只读面（id 桥 + 值范围对四元组 + flags 位）
nb::object pin_timing_summary(const fly::TMPinTiming& p) {
    return nb::make_tuple(
        p.pin_id_.value(),
        nb::make_tuple(p.rise_arrival_.min_, p.rise_arrival_.max_),
        nb::make_tuple(p.fall_arrival_.min_, p.fall_arrival_.max_),
        nb::make_tuple(p.rise_slew_.min_, p.rise_slew_.max_),
        nb::make_tuple(p.fall_slew_.min_, p.fall_slew_.max_),
        p.is_rise_arrival(), p.is_fall_arrival(), p.is_rise_slew(),
        p.is_fall_slew(), p.is_constant(), p.is_multi_source());
}

}  // namespace

FLY_EXPORT_MODULE(_fly_emir_timing) {

// ── 解析边界（tm_types）——TMTimingFile 经 tm_convert_chunk 内部消化，
//    不上 Python 面；TMClock 随 TMStatsDelta.clocks_ 呈现 ──

FLY_EXPORT_CLASS(fly::TMClock, "EXTMClock")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("name", &fly::TMClock::name_)
    FLY_EXPORT_READONLY_ATTR("period", &fly::TMClock::period_)
    FLY_EXPORT_READONLY_ATTR("posedge", &fly::TMClock::posedge_)
    FLY_EXPORT_READONLY_ATTR("negedge", &fly::TMClock::negedge_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMClock);

// ── T1 切块计划（temp 对象）──

FLY_EXPORT_CLASS(fly::TMFileChunkPlan, "EXTMFileChunkPlan")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("file_name", &fly::TMFileChunkPlan::file_name_)
    FLY_EXPORT_READONLY_ATTR("file_size", &fly::TMFileChunkPlan::file_size_)
    // 头段公共前缀字节区间（评审 P1-1：T2 每块拼 prefix——块间时钟表一致）
    FLY_EXPORT_READONLY_ATTR("prefix_start", &fly::TMFileChunkPlan::prefix_start_)
    FLY_EXPORT_READONLY_ATTR("prefix_end", &fly::TMFileChunkPlan::prefix_end_)
    FLY_EXPORT_READONLY_ATTR("chunk_starts", &fly::TMFileChunkPlan::chunk_starts_)
    FLY_EXPORT_READONLY_ATTR("chunk_ends", &fly::TMFileChunkPlan::chunk_ends_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMFileChunkPlan);

FLY_EXPORT_CLASS(fly::TMChunkPlan, "EXTMChunkPlan")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("files", &fly::TMChunkPlan::files_)
    // 构建期追加口（def_ro 的 files 访问返回拷贝，append 不回写 C++ 侧
    // ——T1 master 侧组装经此口）
    FLY_EXPORT_DEF("add_file", [](fly::TMChunkPlan& p,
                                  fly::TMFileChunkPlan f) {
        p.files_.push_back(std::move(f));
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMChunkPlan);

// ── 统计（逐文件 + 片段 + 汇总）──

FLY_EXPORT_CLASS(fly::TMFileStats, "EXTMFileStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("source_file", &fly::TMFileStats::source_file_)
    FLY_EXPORT_READONLY_ATTR("time_scale_sec", &fly::TMFileStats::time_scale_sec_)
    FLY_EXPORT_READONLY_ATTR("entry_count", &fly::TMFileStats::entry_count_)
    FLY_EXPORT_READONLY_ATTR("hit_count", &fly::TMFileStats::hit_count_)
    FLY_EXPORT_READONLY_ATTR("skipped_instance_count",
                             &fly::TMFileStats::skipped_instance_count_)
    FLY_EXPORT_READONLY_ATTR("net_name_miss_count",
                             &fly::TMFileStats::net_name_miss_count_)
    FLY_EXPORT_READONLY_ATTR("skipped_pin_count",
                             &fly::TMFileStats::skipped_pin_count_)
    FLY_EXPORT_READONLY_ATTR("dangling_net_count",
                             &fly::TMFileStats::dangling_net_count_)
    FLY_EXPORT_READONLY_ATTR("unplaced_instance_count",
                             &fly::TMFileStats::unplaced_instance_count_)
    FLY_EXPORT_READONLY_ATTR("strip_miss_count",
                             &fly::TMFileStats::strip_miss_count_)
    FLY_EXPORT_READONLY_ATTR("failed_chunk_count",
                             &fly::TMFileStats::failed_chunk_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMFileStats);

FLY_EXPORT_CLASS(fly::TMStatsDelta, "EXTMStatsDelta")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("file_index", &fly::TMStatsDelta::file_index_)
    FLY_EXPORT_READONLY_ATTR("clocks", &fly::TMStatsDelta::clocks_)
    FLY_EXPORT_READONLY_ATTR("files", &fly::TMStatsDelta::files_)
    FLY_EXPORT_READONLY_ATTR("multi_driver_net_count",
                             &fly::TMStatsDelta::multi_driver_net_count_)
    FLY_EXPORT_READONLY_ATTR("pg_net_skip_count",
                             &fly::TMStatsDelta::pg_net_skip_count_)
    FLY_EXPORT_READONLY_ATTR("const_entry_count",
                             &fly::TMStatsDelta::const_entry_count_)
    FLY_EXPORT_READONLY_ATTR("no_window_count",
                             &fly::TMStatsDelta::no_window_count_)
    FLY_EXPORT_READONLY_ATTR("dropped_source_res_count",
                             &fly::TMStatsDelta::dropped_source_res_count_)
    FLY_EXPORT_READONLY_ATTR("dropped_slack_count",
                             &fly::TMStatsDelta::dropped_slack_count_)
    FLY_EXPORT_READONLY_ATTR("missing_clock_count",
                             &fly::TMStatsDelta::missing_clock_count_)
    FLY_EXPORT_READONLY_ATTR("cd_flag_c_count",
                             &fly::TMStatsDelta::cd_flag_c_count_)
    FLY_EXPORT_READONLY_ATTR("cd_flag_d_count",
                             &fly::TMStatsDelta::cd_flag_d_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMStatsDelta);

FLY_EXPORT_CLASS(fly::TMSummary, "EXTMSummary")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("files", &fly::TMSummary::files_)
    FLY_EXPORT_READONLY_ATTR("total_entry_count",
                             &fly::TMSummary::total_entry_count_)
    FLY_EXPORT_READONLY_ATTR("total_hit_count",
                             &fly::TMSummary::total_hit_count_)
    FLY_EXPORT_READONLY_ATTR("skipped_instance_count",
                             &fly::TMSummary::skipped_instance_count_)
    FLY_EXPORT_READONLY_ATTR("net_name_miss_count",
                             &fly::TMSummary::net_name_miss_count_)
    FLY_EXPORT_READONLY_ATTR("skipped_pin_count",
                             &fly::TMSummary::skipped_pin_count_)
    FLY_EXPORT_READONLY_ATTR("dangling_net_count",
                             &fly::TMSummary::dangling_net_count_)
    FLY_EXPORT_READONLY_ATTR("unplaced_instance_count",
                             &fly::TMSummary::unplaced_instance_count_)
    FLY_EXPORT_READONLY_ATTR("strip_miss_count",
                             &fly::TMSummary::strip_miss_count_)
    FLY_EXPORT_READONLY_ATTR("cross_file_conflict_count",
                             &fly::TMSummary::cross_file_conflict_count_)
    FLY_EXPORT_READONLY_ATTR("clock_conflict_count",
                             &fly::TMSummary::clock_conflict_count_)
    FLY_EXPORT_READONLY_ATTR("multi_driver_net_count",
                             &fly::TMSummary::multi_driver_net_count_)
    FLY_EXPORT_READONLY_ATTR("pg_net_skip_count",
                             &fly::TMSummary::pg_net_skip_count_)
    FLY_EXPORT_READONLY_ATTR("const_entry_count",
                             &fly::TMSummary::const_entry_count_)
    FLY_EXPORT_READONLY_ATTR("no_window_count",
                             &fly::TMSummary::no_window_count_)
    FLY_EXPORT_READONLY_ATTR("dropped_source_res_count",
                             &fly::TMSummary::dropped_source_res_count_)
    FLY_EXPORT_READONLY_ATTR("dropped_slack_count",
                             &fly::TMSummary::dropped_slack_count_)
    FLY_EXPORT_READONLY_ATTR("missing_clock_count",
                             &fly::TMSummary::missing_clock_count_)
    FLY_EXPORT_READONLY_ATTR("cd_flag_c_count",
                             &fly::TMSummary::cd_flag_c_count_)
    FLY_EXPORT_READONLY_ATTR("cd_flag_d_count",
                             &fly::TMSummary::cd_flag_d_count_)
    FLY_EXPORT_READONLY_ATTR("failed_chunk_count",
                             &fly::TMSummary::failed_chunk_count_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMSummary);

// ── 落库形态（正式对象）──

FLY_EXPORT_CLASS(fly::TMPinTiming, "EXTMPinTiming")
    FLY_EXPORT_INIT()
    // 强类型 id 桥（§16 类型纪律：禁止 def_ro 直绑 StrongIdT）
    FLY_EXPORT_READONLY_PROPERTY("pin_id", [](const fly::TMPinTiming& p) {
        return p.pin_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("summary", [](const fly::TMPinTiming& p) {
        return pin_timing_summary(p);
    })
    FLY_EXPORT_READONLY_PROPERTY("is_constant", [](const fly::TMPinTiming& p) {
        return p.is_constant();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_multi_source",
                                 [](const fly::TMPinTiming& p) {
        return p.is_multi_source();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMPinTiming);

FLY_EXPORT_CLASS(fly::TMInstanceTiming, "EXTMInstanceTiming")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("clock_id",
                                 [](const fly::TMInstanceTiming& t) {
        return t.clock_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("pins", [](const fly::TMInstanceTiming& t) {
        return t.pins_;  // 拷贝出（实例引脚个位数~几十，拷贝成本可忽略）
    })
    // 单 pin 点查（未命中返回 None）
    FLY_EXPORT_DEF("find_pin", [](const fly::TMInstanceTiming& t,
                                  uint32_t pin_id) {
        const fly::TMPinTiming* p = t.find_pin(fly::CMPinId{pin_id});
        if (p == nullptr) {
            return std::optional<fly::TMPinTiming>();
        }
        return std::optional<fly::TMPinTiming>(*p);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMInstanceTiming);

FLY_EXPORT_CLASS(fly::TMPartitionTiming, "EXTMPartitionTiming")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("part_id",
                                 [](const fly::TMPartitionTiming& t) {
        return t.part_id_.value();
    })
    FLY_EXPORT_READONLY_PROPERTY("size",
                                 [](const fly::TMPartitionTiming& t) {
        return static_cast<int>(t.size());
    })
    // 单实例点查（debug API 的整区加载后单表查；未命中返回 None）
    FLY_EXPORT_DEF("instance_at", [](const fly::TMPartitionTiming& t,
                                     uint64_t inst_id) {
        const auto it = t.items_.find(fly::CMInstanceId{inst_id});
        if (it == t.items_.end()) {
            return std::optional<fly::TMInstanceTiming>();
        }
        return std::optional<fly::TMInstanceTiming>(it->second);
    })
    // 实例 id 枚举（debug 面遍历；id 边界 int 交换）
    FLY_EXPORT_READONLY_PROPERTY("instance_ids",
                                 [](const fly::TMPartitionTiming& t) {
        std::vector<uint64_t> ids;
        ids.reserve(t.items_.size());
        for (const auto& [inst_id, timing] : t.items_) {
            ids.push_back(inst_id.value());
        }
        return ids;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMPartitionTiming);

// 时钟表条目（内部 struct 独立绑定名）
FLY_EXPORT_CLASS(fly::TMClockTable::Entry, "EXTMClockEntry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("name", &fly::TMClockTable::Entry::name_)
    FLY_EXPORT_READONLY_ATTR("period", &fly::TMClockTable::Entry::period_)
    FLY_EXPORT_READONLY_ATTR("posedge", &fly::TMClockTable::Entry::posedge_)
    FLY_EXPORT_READONLY_ATTR("negedge", &fly::TMClockTable::Entry::negedge_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMClockTable::Entry);

FLY_EXPORT_CLASS(fly::TMClockTable, "EXTMClockTable")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("size", [](const fly::TMClockTable& t) {
        return static_cast<int>(t.clocks_.size());
    })
    // 构建期追加口（T4 任务组装文件内块序时钟表用；字段四元组直填）
    FLY_EXPORT_DEF("add_clock", [](fly::TMClockTable& t, const CMString& name,
                                   double period, double posedge,
                                   double negedge) {
        fly::TMClockTable::Entry e;
        e.name_ = name;
        e.period_ = period;
        e.posedge_ = posedge;
        e.negedge_ = negedge;
        t.clocks_.push_back(std::move(e));
    })
    FLY_EXPORT_DEF("entry_at", [](const fly::TMClockTable& t, uint32_t i) {
        return t.clocks_.at(i);
    })
    FLY_EXPORT_DEF("find", [](const fly::TMClockTable& t,
                              const CMString& name) {
        const fly::TMClockTable::Entry* e = t.find(name);
        if (e == nullptr) {
            return std::optional<fly::TMClockTable::Entry>();
        }
        return std::optional<fly::TMClockTable::Entry>(*e);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMClockTable);

// ── T2 分片（temp 对象）──

// 时钟 id 重映射表（时钟表合并任务产出 → T3 每分区合并消费；评审 P1-1）
FLY_EXPORT_CLASS(fly::TMClockRemap, "EXTMClockRemap")
    FLY_EXPORT_INIT()
    // [file_index][块内 id] = 最终时钟表下标
    FLY_EXPORT_READONLY_ATTR("file_maps", &fly::TMClockRemap::file_maps_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMClockRemap);

FLY_EXPORT_CLASS(fly::TMEntrySlice, "EXTMEntrySlice")
    FLY_EXPORT_INIT()
    // 本块触及分区 id 枚举（编排观测面；T3 合并直传对象不逐分区取）
    FLY_EXPORT_READONLY_PROPERTY("partition_ids",
                                 [](const fly::TMEntrySlice& s) {
        std::vector<uint32_t> ids;
        ids.reserve(s.partitions_.size());
        for (const auto& [pid, part] : s.partitions_) {
            ids.push_back(pid.value());
        }
        return ids;
    })
    FLY_EXPORT_READONLY_ATTR("stats", &fly::TMEntrySlice::stats_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::TMEntrySlice);

// ── T2 换算上下文（design db 快照注入）──

FLY_EXPORT_CLASS(fly::TMDesignContext, "EXTMDesignContext")
    FLY_EXPORT_INIT()
    FLY_EXPORT_DEF("set_design",
                   [](fly::TMDesignContext& c,
                      std::shared_ptr<fly::DSDesign> design) {
        c.set_design(std::move(design));
    })
    FLY_EXPORT_DEF("add_block_names",
                   [](fly::TMDesignContext& c,
                      std::shared_ptr<const fly::DSBlockNames> names) {
        c.add_block_names(std::move(names));
    })
    FLY_EXPORT_DEF("set_inst_id_map",
                   [](fly::TMDesignContext& c,
                      std::shared_ptr<fly::DSIdPartitionIndex> index) {
        c.set_inst_id_map(std::move(index));
    })
    FLY_EXPORT_DEF("add_inst_segment",
                   [](fly::TMDesignContext& c,
                      std::shared_ptr<fly::DSIdPartitionSegment> seg) {
        c.add_inst_segment(std::move(seg));
    })
    FLY_EXPORT_DEF("set_pg_nets",
                   [](fly::TMDesignContext& c,
                      std::shared_ptr<fly::DSPgNetSet> pg_nets) {
        c.set_pg_nets(std::move(pg_nets));
    })
    FLY_EXPORT_DEF("add_partition_nets",
                   [](fly::TMDesignContext& c,
                      std::shared_ptr<fly::DSPartitionNets> nets) {
        c.add_partition_nets(std::move(nets));
    });

// 绑定描述（§2 形态描述符的 C++ 侧；字段可写 + 随任务参数传输）。kind
// = TMFileBindingKind 枚举定型存储（评审 P3-5）——枚举成员禁直绑（§16
// 类型纪律），property 桥 Python 边界保持 int（0/1/2，值域校验）
FLY_EXPORT_CLASS(fly::TMFileBinding, "EXTMFileBinding")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("kind", [](const fly::TMFileBinding& b) {
        return static_cast<int>(b.kind);
    })
    FLY_EXPORT_DEF("set_kind", [](fly::TMFileBinding& b, int kind) {
        if (kind < 0 ||
            kind > static_cast<int>(fly::TMFileBindingKind::BLOCK_CELL)) {
            throw nb::value_error(
                "EXTMFileBinding.kind must be 0 (path) / 1 (block_inst) / "
                "2 (block_cell)");
        }
        b.kind = static_cast<fly::TMFileBindingKind>(kind);
    })
    FLY_EXPORT_ATTR("block_inst", &fly::TMFileBinding::block_inst)
    FLY_EXPORT_ATTR("block_cell", &fly::TMFileBinding::block_cell)
    FLY_EXPORT_ATTR("strip_prefix", &fly::TMFileBinding::strip_prefix);

// ── T1/T2/T3/T4 执行函数 ──

// T1：逐文件切块（文件不可读抛 RuntimeError——入口校验已拦，运行期兜底）
FLY_EXPORT_FUNCTION("tm_plan_file_chunks",
                    [](const CMString& path, uint64_t chunk_size) {
    return fly::tm_plan_file_chunks(path, chunk_size);
});

// T2：逐块换算（bind 写法——FLY_EXPORT_DEF 仅类内，模块级函数用此形态；
// prefix 区间 = 文件头段公共前缀，评审 P1-1）
m.def("tm_convert_chunk",
      [](const fly::TMDesignContext& ctx, const CMString& path,
         uint64_t prefix_start, uint64_t prefix_end,
         uint64_t chunk_start, uint64_t chunk_end,
         const fly::TMFileBinding& binding, uint32_t file_index) {
          return fly::tm_convert_chunk(ctx, path, prefix_start, prefix_end,
                                       chunk_start, chunk_end, binding,
                                       file_index);
      });

// T3：每分区合并（slices = EXTMEntrySlice 列表 + EXTMClockRemap——时钟
// 归属重写为最终表 id；返回 (分区对象, 冲突数)）
m.def("tm_merge_partition",
      [](nb::list slices, const fly::TMClockRemap& remap, uint32_t pid) {
          fly::CMVector<const fly::TMEntrySlice*> ptrs;
          for (nb::handle item : slices) {
              ptrs.push_back(&nb::cast<const fly::TMEntrySlice&>(item));
          }
          uint64_t conflicts = 0;
          auto part = fly::tm_merge_partition(
              ptrs, remap, fly::CMPartitionId{pid}, conflicts);
          return nb::make_tuple(nb::cast(std::move(part)), conflicts);
      });

// T4 ①：summary 聚合（deltas = EXTMStatsDelta 列表）
m.def("tm_merge_summary",
      [](nb::list deltas, uint64_t cross_file_conflict_count,
         uint64_t clock_conflict_count) {
          fly::CMVector<const fly::TMStatsDelta*> ptrs;
          for (nb::handle item : deltas) {
              ptrs.push_back(&nb::cast<const fly::TMStatsDelta&>(item));
          }
          return fly::tm_merge_summary(ptrs, cross_file_conflict_count,
                                       clock_conflict_count);
      });

// T4 ②：时钟表跨文件合并（file_clocks = [(kind, EXTMClockTable)] 列表；
// 返回 (时钟表, 冲突数, EXTMClockRemap) 三元组——remap 为 T3 时钟归属
// 重写的桥，评审 P1-1）
m.def("tm_merge_clocks",
      [](nb::list file_clocks) {
          fly::CMVector<std::pair<int, fly::TMClockTable>> clocks;
          for (nb::handle item : file_clocks) {
              const auto pair = nb::cast<std::pair<int, fly::TMClockTable>>(item);
              clocks.emplace_back(pair.first, std::move(pair.second));
          }
          fly::TMClockRemap remap;
          uint64_t conflicts = 0;
          auto table = fly::tm_merge_clocks(clocks, remap, conflicts);
          return nb::make_tuple(nb::cast(std::move(table)), conflicts,
                                nb::cast(std::move(remap)));
      });

}  // FLY_EXPORT_MODULE
