#include <emir/design/cpp/ds_flatten.h>

#include <log/cpp/logger.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace fly {

namespace {

using I64Point = GEOPointT<int64_t>;
using I64Rect = GEORectT<int64_t>;
using I64Transform = GEOTransformT<int64_t>;

// 复合变换的 int64 域形态（大坐标平移溢出防御，同 S8 scatter 口径）
I64Transform to_i64(const GEOTransform& t) {
    return I64Transform(
        I64Point(t.get_offset().get_x(), t.get_offset().get_y()),
        t.get_orient());
}

I64Rect to_i64(const GEORect& r) {
    return I64Rect(r.get_x_low(), r.get_y_low(), r.get_x_high(),
                   r.get_y_high());
}

// int64 域坐标 → int32 存储域（全局 DBU）越界判定（review 2026-09-13：
// wire 段端点外扩 half_w / via enclosure 平移叠加的极端输入可越出
// int32——截断为 implementation-defined 静默损坏几何，与 via 不可解析
// WARN 跳过同族兜底：越界条目 WARN + 丢弃计数）
bool fits_i32(int64_t v) {
    return v >= std::numeric_limits<int32_t>::min() &&
           v <= std::numeric_limits<int32_t>::max();
}

// int64 域矩形 → 全局 DBU 存储域（int32；越界返回 nullopt 由调用方
// WARN 丢弃）
std::optional<GEORect> to_i32_rect(const I64Rect& r) {
    if (!fits_i32(r.get_x_low()) || !fits_i32(r.get_y_low()) ||
        !fits_i32(r.get_x_high()) || !fits_i32(r.get_y_high())) {
        return std::nullopt;
    }
    return GEORect(static_cast<int32_t>(r.get_x_low()),
                   static_cast<int32_t>(r.get_y_low()),
                   static_cast<int32_t>(r.get_x_high()),
                   static_cast<int32_t>(r.get_y_high()));
}

// int64 矩形 × int32 extend 矩形的半开区间交叠（**坐标分量判定**——
// extend 最外围方向为 int32 极值，禁 width()/height()，裁定 ⑦；
// 共享边界线不算交叠，与 GEORect::overlaps 同语义）
bool overlaps_extend(const I64Rect& r, const GEORect& e) {
    return r.get_x_low() < e.get_x_high() && e.get_x_low() < r.get_x_high() &&
           r.get_y_low() < e.get_y_high() && e.get_y_low() < r.get_y_high();
}

// 放置点归属（裁定补记①）：primary = core_rect 半开区间包含的分区
//（core 网格铺切、恰一命中）；无 core 命中（实例越出 DIEAREA 的输入形
// 态防御）→ 回退距 core 最近的分区（平方距离 tie 取小 id——恰一 primary
// 不变式恒成立）；copies = extend_rect 包含放置点的全部分区（primary 的
// extend ⊇ core 恒含其点）。
struct PointAssignment {
    uint32_t primary = 0;
    CMVector<uint32_t> copies;
};

PointAssignment assign_point(const I64Point& p,
                             const CMVector<DSSubPartition>& parts) {
    static constexpr int64_t kDistCap = int64_t{1} << 30;  // 平方域防溢出
    PointAssignment out;
    bool core_hit = false;
    int64_t best_d2 = std::numeric_limits<int64_t>::max();
    uint32_t best_pid = 0;
    for (uint32_t i = 0; i < parts.size(); ++i) {
        const GEORect& core = parts[i].core_rect_;
        const bool in_core = core.get_x_low() <= p.get_x() &&
                             p.get_x() < core.get_x_high() &&
                             core.get_y_low() <= p.get_y() &&
                             p.get_y() < core.get_y_high();
        if (in_core && !core_hit) {
            out.primary = i;  // 半开区间铺切 → 至多一命中，首个即唯一
            core_hit = true;
        }
        const int64_t dx = std::min(
            p.get_x() < core.get_x_low()
                ? core.get_x_low() - p.get_x()
                : std::max<int64_t>(p.get_x() - core.get_x_high(), 0),
            kDistCap);
        const int64_t dy = std::min(
            p.get_y() < core.get_y_low()
                ? core.get_y_low() - p.get_y()
                : std::max<int64_t>(p.get_y() - core.get_y_high(), 0),
            kDistCap);
        const int64_t d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best_pid = i;
        }
        const GEORect& ext = parts[i].extend_rect_;
        if (ext.get_x_low() <= p.get_x() && p.get_x() < ext.get_x_high() &&
            ext.get_y_low() <= p.get_y() && p.get_y() < ext.get_y_high()) {
            out.copies.push_back(i);
        }
    }
    if (!core_hit) {
        out.primary = best_pid;  // 防御回退：距 core 最近（tie 小 id）
    }
    bool primary_in_copies = false;
    for (const uint32_t pid : out.copies) {
        if (pid == out.primary) {
            primary_in_copies = true;
            break;
        }
    }
    if (!primary_in_copies) {
        out.copies.push_back(out.primary);  // 恰一 primary 副本不变式
    }
    std::sort(out.copies.begin(), out.copies.end());
    return out;
}

// 电源引脚预展开（D18）：cell 的 POWER/GROUND pin，锚点 = 该 pin 全部
// 几何矩形的聚合包围盒中心（int64 中点），经复合变换 g 映射全局坐标。
// pin 无几何（lib-only 等形态）跳过——无锚点可算，不虚构。
void expand_power_pins(DSInstance& copy, const DSInstance& src,
                       const I64Transform& g, const DSDesign& design,
                       const DSPinGeometry* pin_geoms) {
    if (pin_geoms == nullptr || src.cell_id_ >= design.cells_.size()) {
        return;
    }
    const DSCell& cell = design.cells_[src.cell_id_];
    for (const DSPin& pin : cell.pins_) {
        const auto type = static_cast<DSPinType>(pin.type_);
        if (type != DSPinType::POWER && type != DSPinType::GROUND) {
            continue;
        }
        const CMVector<DSShapeRef>* geos = pin_geoms->geometry_of(
            pin.pin_id_);
        if (geos == nullptr || geos->empty()) {
            continue;
        }
        int64_t xl = geos->front().rect_.get_x_low();
        int64_t yl = geos->front().rect_.get_y_low();
        int64_t xh = geos->front().rect_.get_x_high();
        int64_t yh = geos->front().rect_.get_y_high();
        for (size_t k = 1; k < geos->size(); ++k) {
            const GEORect& r = (*geos)[k].rect_;
            xl = std::min(xl, static_cast<int64_t>(r.get_x_low()));
            yl = std::min(yl, static_cast<int64_t>(r.get_y_low()));
            xh = std::max(xh, static_cast<int64_t>(r.get_x_high()));
            yh = std::max(yh, static_cast<int64_t>(r.get_y_high()));
        }
        const I64Point anchor(xl + (xh - xl) / 2, yl + (yh - yl) / 2);
        const I64Point gp = g.apply(anchor);
        if (!fits_i32(gp.get_x()) || !fits_i32(gp.get_y())) {
            // 越界点跳过该电源引脚（review 2026-09-13：截断为静默损坏；
            // 与无几何电源 pin 跳过同族）
            WARN("ds_flatten_block: power pin {} anchor out of int32 "
                 "domain — skipped", pin.pin_id_);
            continue;
        }
        DSPowerPin pp;
        pp.pin_id_ = pin.pin_id_;
        pp.pos_ = GEOPoint(static_cast<int32_t>(gp.get_x()),
                           static_cast<int32_t>(gp.get_y()));
        copy.add_power_pin(std::move(pp));
    }
}

}  // namespace

// —— DSPartitionProduct ——（分片追加合并；同 global id 的 instance 副本
// 键覆盖——global id 全局唯一、同键仅同源重放；列表类字段拼接）
void DSPartitionProduct::merge_from(const DSPartitionProduct& src) {
    for (const auto& [gid, inst] : src.instances_.items_) {
        instances_.items_[gid] = inst;
    }
    for (const auto& [gid, conns] : src.inst_connections_.items_) {
        CMVector<DSPartConnection>& dst = inst_connections_.items_[gid];
        dst.insert(dst.end(), conns.begin(), conns.end());
    }
    for (const auto& [nid, conns] : src.net_connections_.items_) {
        CMVector<DSPartConnection>& dst = net_connections_.items_[nid];
        dst.insert(dst.end(), conns.begin(), conns.end());
    }
    for (const auto& [nid, entries] : src.geometry_.nets_) {
        CMVector<DSGeomEntry>& dst = geometry_.nets_[nid];
        dst.insert(dst.end(), entries.begin(), entries.end());
    }
    for (const auto& [nid, flag] : src.geometry_.crossing_nets_) {
        (void)flag;
        geometry_.mark_crossing(nid);
    }
}

// ── S9 展开算法（每 block 定义一调用；方案「每份 DEF 数据只读一次」）──
// 各出现位置的复合变换 = 树节点 composite_transform_（S6 DFS 递推回填，
// 见 DSHierNode 字段注释），本函数不读父块产物。
CMVector<std::pair<uint32_t, DSPartitionProduct>> ds_flatten_block(
    const DSHierTree& tree, const DSBlockBuildData& block,
    const DSNetBuildData& nets, const DSBlockNames& names,
    const DSDesign& design, const DSPinGeometry* pin_geoms,
    const CMVector<DSSubPartition>& partitions) {
    CMVector<std::pair<uint32_t, DSPartitionProduct>> out;
    if (partitions.empty() || tree.node_count() == 0) {
        return out;  // 无分区（无 DEF 建库）/ 空树：空结果放行
    }
    // int64→int32 越界丢弃计数（review 2026-09-13：极端输入几何静默
    // 损坏防御——与 via 不可解析跳过同族「跳过并计数」兜底）
    uint64_t out_of_domain_count = 0;

    // 该定义的全部出现位置（树上 block cell 名匹配；无位置 = 未被实例化）
    CMVector<uint32_t> positions;
    for (const DSHierNode& node : tree.nodes_) {
        if (node.get_block_cell_name() == block.get_block_name()) {
            positions.push_back(node.get_id());
        }
    }
    if (positions.empty()) {
        return out;
    }

    // 分区产物下标（pid → out 下标，仅产出非空分区）
    CMUnorderedMap<uint32_t, size_t> product_index;
    const auto product_for = [&](uint32_t pid) -> DSPartitionProduct& {
        auto it = product_index.find(pid);
        if (it == product_index.end()) {
            out.emplace_back(pid, DSPartitionProduct{});
            it = product_index.emplace(pid, out.size() - 1).first;
        }
        return out[it->second].second;
    };

    for (const uint32_t pos : positions) {
        const DSHierNode& node = tree.node(pos);
        const I64Transform t = to_i64(node.composite_transform_);
        const uint64_t inst_start = node.instance_start_;
        const uint64_t net_start = node.net_start_;

        // 块实例自身的副本位置（port 连接条目跟随；global id =
        // self_global_id_——副本由父块展开产出，本展开只补连接条目）
        const PointAssignment self_assign = assign_point(t.get_offset(),
                                                         partitions);

        // S5b 名字形态 → global 连接项（instance 名经伴生 hasher →
        // local id → +inst_start；("PIN", port) → 块实例自身 global id +
        // port 位，⑧ local 0 映射）。未登记实例名（S5a/S5b desync 防御）
        // WARN 后丢弃条目，不中断建库。
        const auto make_connection = [&](const DSNetConnection& c,
                                         uint64_t global_net,
                                         DSPartConnection& pc) -> bool {
            if (c.is_port_ref()) {
                pc.instance_global_id_ = node.get_self_global_id();
                pc.set_port();
            } else {
                const uint64_t local =
                    names.instance_names_
                        ? names.instance_names_->get_id(c.get_instance_name())
                        : DSInstanceNameHasher::kInvalidId;
                if (!DSInstanceNameHasher::is_valid_id(local)) {
                    WARN("ds_flatten_block: connection instance '{}' not in "
                         "name hasher (block {}) — entry dropped",
                         c.get_instance_name(), block.get_block_name());
                    return false;
                }
                pc.instance_global_id_ = inst_start + local;
            }
            pc.net_global_id_ = global_net;
            pc.pin_name_ = c.get_pin_name();
            return true;
        };

        // 连接桶：local instance id（0 = 块自身 port 引用）→ 条目集
        //（单遍扫描连接表 O(conns)，供 INST_CONNECTIONS 跟随副本挂接）
        CMUnorderedMap<uint64_t, CMVector<DSPartConnection>> conn_bucket;
        for (const auto& [local_net, conns] : nets.connections_) {
            const uint64_t global_net = net_start + local_net - 1;
            for (const DSNetConnection& c : conns) {
                DSPartConnection pc;
                if (!make_connection(c, global_net, pc)) {
                    continue;
                }
                const uint64_t bucket_key =
                    pc.is_port() ? uint64_t{0}
                                 : pc.instance_global_id_ - inst_start;
                conn_bucket[bucket_key].push_back(std::move(pc));
            }
        }

        // —— INSTANCES + INST_CONNECTIONS（跟随 instance 副本）——
        for (const auto& [local_id, inst] : block.instances_) {
            if (local_id == 0) {
                continue;  // ⑧ 占位（块实例自身由父块展开产出副本）
            }
            if (inst.get_placement_status() ==
                static_cast<uint8_t>(DSPlacementStatus::UNPLACED)) {
                continue;  // 无物理放置（D14 兜底计数对象），不入分区
            }
            const I64Transform g = t.compose(to_i64(inst.get_transform()));
            const uint64_t global_id = inst_start + local_id;
            const PointAssignment pa = assign_point(g.get_offset(),
                                                    partitions);
            for (const uint32_t pid : pa.copies) {
                DSInstance copy = inst;
                if (!fits_i32(g.get_offset().get_x()) ||
                    !fits_i32(g.get_offset().get_y())) {
                    // 实例本体不可丢（连接完整性优先）：WARN + 保留截断
                    // 值（review 2026-09-13 口径——极端越界放置的实例
                    // 坐标失真但网表完整）
                    WARN("ds_flatten_block: instance offset out of int32 "
                         "domain (local {}) — truncated value kept",
                         local_id);
                }
                copy.transform_ = GEOTransform(
                    GEOPoint(static_cast<int32_t>(g.get_offset().get_x()),
                             static_cast<int32_t>(g.get_offset().get_y())),
                    g.get_orient());
                expand_power_pins(copy, inst, g, design, pin_geoms);
                if (pid == pa.primary) {
                    copy.set_primary();
                }
                product_for(pid).instances_.items_[global_id] =
                    std::move(copy);
                const auto bit = conn_bucket.find(local_id);
                if (bit != conn_bucket.end()) {
                    CMVector<DSPartConnection>& dst =
                        product_for(pid).inst_connections_.items_[global_id];
                    dst.insert(dst.end(), bit->second.begin(),
                               bit->second.end());
                }
            }
        }
        // 块自身 port 连接条目（跟随块实例自身的副本，键 = self_global_id_）
        const auto self_conn = conn_bucket.find(0);
        if (self_conn != conn_bucket.end()) {
            for (const uint32_t pid : self_assign.copies) {
                CMVector<DSPartConnection>& dst =
                    product_for(pid).inst_connections_.items_
                        [node.get_self_global_id()];
                dst.insert(dst.end(), self_conn->second.begin(),
                           self_conn->second.end());
            }
        }

        // —— GEOMETRY（net 几何副本 + is_crossing）+ NET_CONNECTIONS ——
        CMUnorderedSet<uint64_t> geo_nets;
        for (const auto& [id, _] : nets.wires_) {
            geo_nets.insert(id);
        }
        for (const auto& [id, _] : nets.rects_) {
            geo_nets.insert(id);
        }
        for (const auto& [id, _] : nets.net_via_ids_) {
            geo_nets.insert(id);
        }

        for (const uint64_t local_net : geo_nets) {
            const uint64_t global_net = net_start + local_net - 1;
            CMUnorderedSet<uint32_t> hit_pids;

            // 全局矩形铺入 extend 交叠分区：每命中分区一份条目（坐标分
            // 量判定、不裁剪——裁定补记④/⑦；via 条目 primary 位按其放
            // 置点归属分区置位，primary_pid < 0 = 非 via 条目）
            const auto spread_rect = [&](const I64Rect& grect,
                                         uint32_t layer_id,
                                         uint32_t via_cell_id,
                                         int32_t primary_pid) {
                for (uint32_t pid = 0; pid < partitions.size(); ++pid) {
                    if (!overlaps_extend(grect, partitions[pid].extend_rect_)) {
                        continue;
                    }
                    const auto rect32 = to_i32_rect(grect);
                    if (!rect32.has_value()) {
                        WARN("ds_flatten_block: geometry entry out of "
                             "int32 domain (net {}, layer {}) — dropped",
                             global_net, layer_id);
                        ++out_of_domain_count;
                        continue;
                    }
                    DSGeomEntry e;
                    e.layer_id_ = layer_id;
                    e.rect_ = *rect32;
                    if (via_cell_id != DSGeomEntry::kNoViaCell) {
                        e.via_cell_id_ = via_cell_id;
                    }
                    if (static_cast<int32_t>(pid) == primary_pid) {
                        e.set_primary();
                    }
                    product_for(pid).geometry_.add_entry(global_net,
                                                         std::move(e));
                    hit_pids.insert(pid);
                }
            };

            // wire 段矩形化：相邻点对 + 宽度半开区间外扩（同 S5b 密度
            // 节点口径，int64 中间量）
            const auto* wires = nets.wires_of(local_net);
            if (wires != nullptr) {
                for (const DSNetWire& wire : *wires) {
                    const int64_t half_w = wire.width_ / 2;
                    for (size_t i = 0; i + 1 < wire.points_.size(); ++i) {
                        const GEOPoint& p0 = wire.points_[i];
                        const GEOPoint& p1 = wire.points_[i + 1];
                        const I64Rect seg(
                            std::min<int64_t>(p0.get_x(), p1.get_x()) -
                                half_w,
                            std::min<int64_t>(p0.get_y(), p1.get_y()) -
                                half_w,
                            std::max<int64_t>(p0.get_x(), p1.get_x()) +
                                half_w,
                            std::max<int64_t>(p0.get_y(), p1.get_y()) +
                                half_w);
                        spread_rect(t.apply_box(seg), wire.layer_id_,
                                    DSGeomEntry::kNoViaCell, -1);
                    }
                }
            }
            // rect 项原样变换
            const auto* rects = nets.rects_of(local_net);
            if (rects != nullptr) {
                for (const DSNetRect& rect : *rects) {
                    spread_rect(t.apply_box(to_i64(rect.rect_)),
                                rect.layer_id_, DSGeomEntry::kNoViaCell, -1);
                }
            }
            // via instance 图形展开：via cell 的 cut/enclosure 矩形平移
            // 至放置点后经复合变换（via 条目带 primary 位——放置点归属，
            // 裁定补记①；权威表快照外 id 防御跳过，同 S5b 密度节点口径）
            const auto* via_ids = nets.via_ids_of(local_net);
            if (via_ids != nullptr) {
                for (const uint64_t via_id : *via_ids) {
                    const DSViaInstance* vi = nets.via_instance_at(via_id);
                    if (vi == nullptr ||
                        vi->via_cell_id_ >= design.via_cells_.size()) {
                        WARN("ds_flatten_block: via instance {} not "
                             "resolvable (block {}) — skipped",
                             via_id, block.get_block_name());
                        continue;
                    }
                    const DSViaCell& vc = design.via_cells_[vi->via_cell_id_];
                    const I64Point vpos(
                        static_cast<int64_t>(vi->pos_.get_x()),
                        static_cast<int64_t>(vi->pos_.get_y()));
                    const PointAssignment vpa =
                        assign_point(t.apply(vpos), partitions);
                    // cut 层 id 未判定兜底回退 bottom 层（同 S5b 密度节点）
                    const uint32_t cut_layer =
                        vc.get_cut_layer_id() != UINT32_MAX
                            ? vc.get_cut_layer_id()
                            : vc.get_bottom_layer_id();
                    const auto spread_group =
                        [&](const CMVector<GEORect>& rs, uint32_t layer_id) {
                            for (const GEORect& r : rs) {
                                const I64Rect local(
                                    static_cast<int64_t>(r.get_x_low()) +
                                        vpos.get_x(),
                                    static_cast<int64_t>(r.get_y_low()) +
                                        vpos.get_y(),
                                    static_cast<int64_t>(r.get_x_high()) +
                                        vpos.get_x(),
                                    static_cast<int64_t>(r.get_y_high()) +
                                        vpos.get_y());
                                spread_rect(t.apply_box(local), layer_id,
                                            vi->via_cell_id_,
                                            static_cast<int32_t>(
                                                vpa.primary));
                            }
                        };
                    spread_group(vc.cut_rects_, cut_layer);
                    spread_group(vc.bottom_enclosure_,
                                 vc.get_bottom_layer_id());
                    spread_group(vc.top_enclosure_, vc.get_top_layer_id());
                }
            }

            if (hit_pids.empty()) {
                continue;  // 该网几何未落任何分区（越出全域的防御形态）
            }
            // is_crossing（裁定补记④）：成员图形散布多于一个分区
            if (hit_pids.size() > 1) {
                for (const uint32_t pid : hit_pids) {
                    product_for(pid).geometry_.mark_crossing(global_net);
                }
            }

            // NET_CONNECTIONS（跟随 net 副本——仅几何所在分区；非 pg 全
            // 量补全 / pg 仅本区 instance 副本相关条目，裁定补记②）
            const auto* conns = nets.connections_of(local_net);
            if (conns == nullptr || conns->empty()) {
                continue;
            }
            CMVector<DSPartConnection> full;
            CMVector<CMVector<uint32_t>> entry_pids;
            for (const DSNetConnection& c : *conns) {
                DSPartConnection pc;
                if (!make_connection(c, global_net, pc)) {
                    continue;
                }
                CMVector<uint32_t> pids;
                if (pc.is_port()) {
                    pids = self_assign.copies;
                } else {
                    const uint64_t local = pc.instance_global_id_ - inst_start;
                    const auto iit = block.instances_.find(local);
                    if (iit != block.instances_.end() &&
                        iit->second.get_placement_status() !=
                            static_cast<uint8_t>(
                                DSPlacementStatus::UNPLACED)) {
                        const I64Transform g =
                            t.compose(to_i64(iit->second.get_transform()));
                        pids = assign_point(g.get_offset(), partitions).copies;
                    }
                }
                full.push_back(std::move(pc));
                entry_pids.push_back(std::move(pids));
            }
            if (nets.is_pg_net(local_net)) {
                for (const uint32_t pid : hit_pids) {
                    CMVector<DSPartConnection> part;
                    for (size_t i = 0; i < full.size(); ++i) {
                        if (std::find(entry_pids[i].begin(),
                                      entry_pids[i].end(),
                                      pid) != entry_pids[i].end()) {
                            part.push_back(full[i]);
                        }
                    }
                    if (!part.empty()) {
                        product_for(pid).net_connections_.items_[global_net] =
                            std::move(part);
                    }
                }
            } else {
                for (const uint32_t pid : hit_pids) {
                    product_for(pid).net_connections_.items_[global_net] =
                        full;
                }
            }
        }

        // —— DEF obstruction（net 0 + obs 位；每位置一份副本，无
        // primary 概念——裁定补记③）——
        for (const DSShapeRef& obs : block.obstructions_) {
            const I64Rect g = t.apply_box(to_i64(obs.rect_));
            for (uint32_t pid = 0; pid < partitions.size(); ++pid) {
                if (!overlaps_extend(g, partitions[pid].extend_rect_)) {
                    continue;
                }
                const auto obs32 = to_i32_rect(g);
                if (!obs32.has_value()) {
                    WARN("ds_flatten_block: obstruction out of int32 "
                         "domain (layer {}) — dropped", obs.layer_id_);
                    ++out_of_domain_count;
                    continue;
                }
                DSGeomEntry e;
                e.layer_id_ = obs.layer_id_;
                e.rect_ = *obs32;
                e.set_obs();
                product_for(pid).geometry_.add_entry(0, std::move(e));
            }
        }
    }
    if (out_of_domain_count > 0) {
        WARN("ds_flatten_block: {} geometry/obstruction entrie(s) out of "
             "int32 domain dropped (block {})", out_of_domain_count,
             block.get_block_name());
    }
    return out;
}

}  // namespace fly
