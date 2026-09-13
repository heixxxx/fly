#include <emir/design/cpp/ds_verify.h>

#include <message/cpp/message_macros.h>

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fly {

namespace {

// 升序去重（素材集序列化确定性）
void sort_unique(CMVector<uint64_t>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}


// 并查集自洽校验（损坏类）：两层不变式（root_of_[root] 恒自映射）+
// 双向一致（members_of_ 的每个成员映射回该 root、root 键自映射、成员列
// 表总数 = root_of_ 规模——每成员恰挂一类）。
CMString ds_check_net_union(const DSNetUnion& u) {
    for (const auto& [member, root] : u.root_of_) {
        (void)member;
        const auto it = u.root_of_.find(root);
        if (it == u.root_of_.end() || it->second != root) {
            return "root_of_ two-layer invariant broken: root " +
                   std::to_string(root) + " is not self-mapped";
        }
    }
    size_t listed = 0;
    for (const auto& [root, members] : u.members_of_) {
        const auto it = u.root_of_.find(root);
        if (it == u.root_of_.end() || it->second != root) {
            return "members_of_ key " + std::to_string(root) +
                   " is not a root in root_of_";
        }
        for (const uint64_t id : members) {
            const auto mit = u.root_of_.find(id);
            if (mit == u.root_of_.end() || mit->second != root) {
                return "members_of_ entry " + std::to_string(id) +
                       " does not map back to root " + std::to_string(root);
            }
        }
        listed += members.size();
    }
    if (listed != u.root_of_.size()) {
        return "members_of_ lists " + std::to_string(listed) +
               " member(s) but root_of_ has " +
               std::to_string(u.root_of_.size());
    }
    return CMString{};
}

// 单 hasher 双向闭环全查（for_each (name, id) → get_name(id) == name）；
// 首例定位到 label，坏例计数累计
template <typename Hasher>
void check_hasher_closure(const Hasher& hasher, const CMString& label,
                          size_t& bad_total, CMString& first_bad) {
    hasher.for_each([&](const CMString& name, auto id) {
        if (hasher.get_name(id) != name) {
            if (bad_total == 0) {
                first_bad = label + "['" + name + "']";
            }
            ++bad_total;
        }
    });
}

// namemap 双向一致校验（损坏类）：cell/pin/via cell/layer 四全局 hasher
// 全查（量小）+ 每伴生 instance/net hasher 全查（per-DEF 名字表）。
CMString ds_check_name_maps(const DSDesign& design, const DSStack& stack,
                            const CMVector<const DSBlockNames*>& names) {
    size_t bad = 0;
    CMString first;
    check_hasher_closure(design.cell_names_, "cell", bad, first);
    check_hasher_closure(design.pin_names_, "pin", bad, first);
    check_hasher_closure(design.via_cell_names_, "via_cell", bad, first);
    check_hasher_closure(stack.layer_names_, "layer", bad, first);
    for (const DSBlockNames* n : names) {
        const CMString at = "@" + n->block_name_;
        check_hasher_closure(*n->instance_names_, "instance" + at, bad, first);
        check_hasher_closure(*n->net_names_, "net" + at, bad, first);
    }
    if (bad != 0) {
        return "name hasher bidirectional closure broken: " +
               std::to_string(bad) + " mismatched entries (first: " + first +
               ")";
    }
    return CMString{};
}

}  // namespace

// 分区网格无缝覆盖校验（损坏类）：core 矩形的行列网格性质——切线集一
// 致（每列 x 边界一致、每行 y 边界一致）+ (xp, yp) 网格坐标完整恰一次 +
// 外沿与切线对齐 global_density 格网覆盖域（与 S8 分区 core 同源——格边
// 界吸附切分）。非空返回 = 覆盖损坏描述；空串 = 通过。
CMString ds_check_partition_coverage(
    const CMVector<DSSubPartition>& partitions,
    const DSDensityGrid& global) {
    const bool density_configured =
        global.cols_ > 0 && global.rows_ > 0 && global.bin_width_ > 0 &&
        global.bin_height_ > 0;
    if (partitions.empty()) {
        if (density_configured) {
            return "partition table is empty but global density grid is "
                   "configured (" + std::to_string(global.cols_) + "x" +
                   std::to_string(global.rows_) + " bins)";
        }
        return CMString{};  // 无 DEF 建库：空表 + 未配置格网放行
    }
    if (!density_configured) {
        return "partition table has " + std::to_string(partitions.size()) +
               " entries but global density grid is unconfigured";
    }

    // —— 区间性质校验（review 2026-09-13 修复：原「满网格 + xp/yp 网格
    //    坐标」校验与 S8 空段机制冲突——prefix_cuts 负载集中时多切线
    //    同值产生空段，产出侧跳过零格段且 xp_ 保留原网格坐标，属方案内
    //    合法形态却被误判损坏。改为等价的区间性质：全部 core 在域内 +
    //    两两不重叠（半开区间，共享边界不算）+ Σ 面积 = 域面积 ⇔ 无缝
    //    无隙覆盖（矩形面积可加性）。xp_/yp_ 仅保留唯一性）——
    const int64_t dom_x_low = global.origin_x_;
    const int64_t dom_y_low = global.origin_y_;
    const int64_t dom_x_high =
        dom_x_low + static_cast<int64_t>(global.cols_) * global.bin_width_;
    const int64_t dom_y_high =
        dom_y_low + static_cast<int64_t>(global.rows_) * global.bin_height_;
    const int64_t dom_area = (dom_x_high - dom_x_low) *
                             (dom_y_high - dom_y_low);

    struct CoreRect {
        int64_t xl, yl, xh, yh;
    };
    CMVector<CoreRect> cores;
    cores.reserve(partitions.size());
    std::set<std::pair<uint32_t, uint32_t>> occupied;
    int64_t total_area = 0;
    for (const DSSubPartition& p : partitions) {
        const auto coord = std::to_string(p.xp_) + "," +
                           std::to_string(p.yp_);
        if (!occupied.emplace(p.xp_, p.yp_).second) {
            return "duplicate partition grid cell (" + coord + ")";
        }
        const CoreRect c{p.core_rect_.get_x_low(), p.core_rect_.get_y_low(),
                         p.core_rect_.get_x_high(),
                         p.core_rect_.get_y_high()};
        if (c.xl < dom_x_low || c.yl < dom_y_low || c.xh > dom_x_high ||
            c.yh > dom_y_high) {
            return "partition (" + coord + ") core rect [" +
                   std::to_string(c.xl) + "," + std::to_string(c.yl) + "," +
                   std::to_string(c.xh) + "," + std::to_string(c.yh) +
                   "] extends outside the global density domain";
        }
        if ((c.xl - dom_x_low) % global.bin_width_ != 0 ||
            (c.xh - dom_x_low) % global.bin_width_ != 0 ||
            (c.yl - dom_y_low) % global.bin_height_ != 0 ||
            (c.yh - dom_y_low) % global.bin_height_ != 0) {
            return "partition (" + coord +
                   ") core rect is not aligned to the density grid";
        }
        total_area += (c.xh - c.xl) * (c.yh - c.yl);
        cores.push_back(c);
    }
    // 两两不重叠（分区数十~数百级，O(n²) 足够；半开区间——共享边界
    // 是正常衔接不算重叠）
    for (size_t i = 0; i < cores.size(); ++i) {
        for (size_t j = i + 1; j < cores.size(); ++j) {
            const CoreRect& a = cores[i];
            const CoreRect& b = cores[j];
            if (a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh) {
                return "partition cores overlap: [" +
                       std::to_string(a.xl) + "," + std::to_string(a.yl) +
                       "," + std::to_string(a.xh) + "," + std::to_string(a.yh) +
                       "] vs [" + std::to_string(b.xl) + "," +
                       std::to_string(b.yl) + "," + std::to_string(b.xh) +
                       "," + std::to_string(b.yh) + "]";
            }
        }
    }
    if (total_area != dom_area) {
        return "partition core union area " + std::to_string(total_area) +
               " != density domain area " + std::to_string(dom_area) +
               " (gap or overlap — coverage broken)";
    }
    return CMString{};
}


// —— 分区级校验（每分区一任务；只读本分区四类产物）────────────────────

DSPartitionCheckResult ds_verify_partition(
    uint32_t partition_id, uint32_t xp, uint32_t yp,
    const DSPartitionGeometry& geometry,
    const DSPartInstances& instances,
    const DSPartInstConnections& inst_connections,
    const DSPartNetConnections& net_connections) {
    DSPartitionCheckResult r;
    r.partition_id_ = partition_id;
    r.xp_ = xp;
    r.yp_ = yp;

    // 实例副本：计数 + primary/全量 id 素材（全局连续性与多 primary 判定）
    r.instance_count_ = instances.items_.size();
    for (const auto& [id, copy] : instances.items_) {
        r.instance_ids_.push_back(id);
        if (copy.is_primary()) {
            ++r.primary_instance_count_;
            r.primary_instance_ids_.push_back(id);
        }
    }

    // 几何：条目计数 + 网覆盖素材。net 0 桶仅含 OBS 条目时不计为网 0 覆盖
    //（OBS 与 root 首网同键共存，obs 位判别——OBS 不是网几何）
    for (const auto& [net_id, entries] : geometry.nets_) {
        r.geometry_entry_count_ += entries.size();
        ++r.net_count_;
        const bool has_real_geometry =
            net_id != 0 ||
            std::any_of(entries.begin(), entries.end(),
                        [](const DSGeomEntry& e) { return !e.is_obs(); });
        if (has_real_geometry) {
            r.net_ids_.push_back(net_id);
        }
    }
    r.crossing_net_count_ = geometry.crossing_nets_.size();
    for (const auto& [net_id, flag] : geometry.crossing_nets_) {
        (void)flag;
        r.net_ids_.push_back(net_id);
        r.crossing_net_ids_.push_back(net_id);
    }

    // 连接表：条目计数 + 网覆盖素材（跟随网副本的键 + 跟随实例副本条目
    // 的端点网 id）
    for (const auto& [net_id, conns] : net_connections.items_) {
        r.connection_count_ += conns.size();
        r.net_ids_.push_back(net_id);
    }
    for (const auto& [id, conns] : inst_connections.items_) {
        (void)id;
        r.connection_count_ += conns.size();
        for (const DSPartConnection& c : conns) {
            r.net_ids_.push_back(c.net_global_id_);
        }
    }

    sort_unique(r.instance_ids_);
    sort_unique(r.primary_instance_ids_);
    sort_unique(r.net_ids_);
    sort_unique(r.crossing_net_ids_);
    return r;
}

// —— 全局校验（单任务）──────────────────────────────────────────────

DSDesignCheckReport ds_verify_design(
    const DSHierTree& tree, const DSDesign& design, const DSStack& stack,
    const DSDensityGrid& global_density, const DSNetUnion& net_union,
    const CMVector<const DSBlockBuildData*>& blocks,
    const CMVector<const DSNetBuildData*>& nets,
    const CMVector<const DSBlockNames*>& names,
    const CMVector<const DSPartitionCheckResult*>& checks) {
    DSDesignCheckReport r;

    // 树区间推导期望域（口径见 ds_verify.h 文件头注释）
    uint64_t inst_slots = 0;
    for (const DSHierNode& n : tree.nodes_) {
        inst_slots += n.instance_count_;
        r.expected_nets_ += n.net_count_;
        r.expected_vias_ += n.via_count_;
    }
    const size_t node_count = tree.node_count();
    if (node_count > 0) {
        r.expected_instances_ = inst_slots - (node_count - 1) - 1;
    }
    r.instance_ids_.expected_ = r.expected_instances_;
    r.net_ids_.expected_ = r.expected_nets_;
    r.via_ids_.expected_ = r.expected_vias_;

    // 分区结果汇总：计数累加 + id 并集 + 多 primary 直方图
    std::unordered_map<uint64_t, uint32_t> primary_hist;
    std::unordered_set<uint64_t> instance_set;
    std::unordered_set<uint64_t> net_set;
    std::unordered_set<uint64_t> crossing_set;
    r.partition_count_ = static_cast<uint32_t>(checks.size());
    for (const DSPartitionCheckResult* c : checks) {
        r.total_primary_ += c->primary_instance_count_;
        r.total_instances_ += c->instance_count_;
        r.total_connections_ += c->connection_count_;
        r.total_geometry_entries_ += c->geometry_entry_count_;
        instance_set.insert(c->instance_ids_.begin(), c->instance_ids_.end());
        net_set.insert(c->net_ids_.begin(), c->net_ids_.end());
        crossing_set.insert(c->crossing_net_ids_.begin(),
                            c->crossing_net_ids_.end());
        for (const uint64_t id : c->primary_instance_ids_) {
            ++primary_hist[id];
        }
    }
    for (const auto& [id, copies] : primary_hist) {
        (void)id;
        if (copies > 1) {
            r.instance_ids_.duplicates_ += copies - 1;
        }
    }
    r.instance_ids_.actual_ = instance_set.size();
    r.net_ids_.actual_ = net_set.size();
    r.total_nets_ = net_set.size();
    r.total_crossing_nets_ = crossing_set.size();
    r.instance_ids_.holes_ =
        r.instance_ids_.expected_ > r.instance_ids_.actual_
            ? r.instance_ids_.expected_ - r.instance_ids_.actual_
            : 0;
    r.net_ids_.holes_ = r.net_ids_.expected_ > r.net_ids_.actual_
                            ? r.net_ids_.expected_ - r.net_ids_.actual_
                            : 0;

    // def 名 → 首份序号（重名保留首份，与树构建/S9 展开同语义）
    std::unordered_map<CMString, uint32_t> def_by_name;
    for (uint32_t i = 0; i < names.size(); ++i) {
        def_by_name.emplace(names[i]->block_name_, i);
    }

    // via id 域：per-DEF 网产物 via_instances_ 键经 (节点 via_start +
    // local − 1) 换算（via instance 不入分区产物——权威存储只在 S5b 产物；
    // 树节点按 block 名反查首份定义）。节点引用的定义不在名字表 = 树与
    // 产物对齐破坏（S6 构造已保证，读回防御）→ 归入 namemap 损坏描述。
    std::unordered_map<uint64_t, uint32_t> via_hist;
    CMString tree_def_missing;
    for (const DSHierNode& n : tree.nodes_) {
        const auto it = def_by_name.find(n.block_cell_name_);
        if (it == def_by_name.end()) {
            if (tree_def_missing.empty()) {
                tree_def_missing = "tree node " + std::to_string(n.id_) +
                                   " references block definition '" +
                                   n.block_cell_name_ +
                                   "' missing from the name table";
            }
            continue;
        }
        for (const auto& [local, via] : nets[it->second]->via_instances_) {
            (void)via;
            ++via_hist[n.via_start_ + local - 1];
        }
    }
    r.via_ids_.actual_ = via_hist.size();
    for (const auto& [id, copies] : via_hist) {
        (void)id;
        if (copies > 1) {
            r.via_ids_.duplicates_ += copies - 1;
        }
    }
    r.via_ids_.holes_ = r.via_ids_.expected_ > r.via_ids_.actual_
                            ? r.via_ids_.expected_ - r.via_ids_.actual_
                            : 0;

    // 密度守恒（观测 warn，primary 口径；口径详见 ds_verify.h 文件头）。
    // 右端按**树节点（实例化位置）**迭代——每节点反查首份 def 的
    // (instance_count − unplaced_count)（review 2026-09-13 修复：原按
    // def 求和使同一 def 被实例化 k 次时少算 (k−1)×placed，合法库恒
    // 误报 DSGN::0023；与上方 via 域同一树迭代模式）
    uint64_t expected_placed = 0;
    for (const DSHierNode& n : tree.nodes_) {
        const auto it = def_by_name.find(n.block_cell_name_);
        if (it == def_by_name.end()) {
            continue;  // 定义缺失已由上方 tree_def_missing 记录（fatal 路径）
        }
        const DSBlockBuildData& blk = *blocks[it->second];
        expected_placed +=
            blk.stats_.instance_count > blk.stats_.unplaced_count
                ? blk.stats_.instance_count - blk.stats_.unplaced_count
                : 0;
    }
    if (expected_placed != r.total_primary_) {
        r.density_variance_ =
            "primary instance total " + std::to_string(r.total_primary_) +
            " != tree-expanded placed instances " +
            std::to_string(expected_placed) + " (sum over tree nodes of "
            "first-def instance_count - unplaced_count)";
    }

    // 密度总量（global_density 三通道，观测参照）
    r.density_instance_total_ =
        static_cast<uint64_t>(global_density.total_count());
    r.density_metal_total_ =
        static_cast<uint64_t>(global_density.metal_total());
    r.density_via_total_ = static_cast<uint64_t>(global_density.via_total());

    // 损坏类（并查集 / 分区覆盖 / namemap；树-def 对齐缺失并入 namemap）
    r.union_inconsistency_ = ds_check_net_union(net_union);
    r.coverage_gap_ = ds_check_partition_coverage(design.partitions_,
                                                  global_density);
    r.namemap_inconsistency_ = ds_check_name_maps(design, stack, names);
    if (!tree_def_missing.empty()) {
        r.namemap_inconsistency_ =
            r.namemap_inconsistency_.empty()
                ? tree_def_missing
                : r.namemap_inconsistency_ + "; " + tree_def_missing;
    }
    return r;
}

// —— 损坏类处置（首个非空项 fatal 退出；观测类由调用方 warn）──────────

void ds_verify_report_or_fatal(const DSDesignCheckReport& report) {
    if (!report.union_inconsistency_.empty()) {
        MSG_FATAL_EXIT("DSGN::0019", 0, 80,
                       "design verify: net union inconsistent (corrupt "
                       "data) — {}",
                       report.union_inconsistency_);
    }
    if (!report.coverage_gap_.empty()) {
        MSG_FATAL_EXIT("DSGN::0020", 0, 80,
                       "design verify: partition grid coverage broken "
                       "(corrupt data) — {}",
                       report.coverage_gap_);
    }
    if (!report.namemap_inconsistency_.empty()) {
        MSG_FATAL_EXIT("DSGN::0021", 0, 80,
                       "design verify: name map inconsistent (corrupt "
                       "data) — {}",
                       report.namemap_inconsistency_);
    }
}

}  // namespace fly
