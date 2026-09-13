#include <emir/design/cpp/ds_merge.h>

#include <emir/lib/cpp/lib_types.h>
#include <message/cpp/message_macros.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace fly {

namespace {

// 名单格式化（DSGN 消息用，避免引入 fmt/ranges）
CMString join_names(const CMVector<CMString>& names) {
    CMString out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += ", ";
        out += names[i];
    }
    return out;
}

}  // namespace

// ── S2 汇总：cell lef 并行任务产物并入全局容器 ──────────────────────

int ds_merge_cell_lef(DSDesign& dst, const DSDesign& src_part,
                      DSPinGeometry& dst_geom,
                      const DSPinGeometry& src_geom) {
    int conflicts = 0;
    // src 局部 cell id → dst 全局 id（被抛弃 cell 不入映射）
    CMUnorderedMap<uint32_t, uint32_t> cell_id_map;

    for (uint32_t src_id = 0; src_id < src_part.cells_.size(); ++src_id) {
        const DSCell& c = src_part.cells_[src_id];
        if (dst.find_cell(c.get_name()) != nullptr) {
            // 跨文件重复 macro：保留首份抛弃后续（DSGN::0001）
            ++conflicts;
            MSG("DSGN::0001", 0,
                "duplicate macro '{}' from cell lef dropped (keeping first)",
                c.get_name());
            continue;
        }
        const uint32_t dst_id = dst.add_cell(DSCell(c));
        cell_id_map[src_id] = dst_id;
    }

    // pin hasher 重挂：src 局部 pin id → 全局平铺新 id（基址 = dst 现有
    // pin 数；仅收录 cell 的 pin 注册，被抛弃 cell 的 pin 随之不入）。
    // R4：全局新 id 同步回填 dst cell.pins_ 的 DSPin::pin_id_（重挂在先、
    // 回填在后），pin 几何按全局 pin id 重挂（src 键 = 局部 pin id，被
    // 抛弃 cell 的 pin 几何随之丢弃）。R7 ㊱：pin 名经 src pin hasher 的
    // 组合键（"cell/pin"）反查（DSPin 自身不存 name）
    const uint32_t pin_base =
        static_cast<uint32_t>(dst.pin_names_.name_table_.size());
    for (const auto& [src_id, dst_id] : cell_id_map) {
        const DSCell& c = src_part.cells_[src_id];
        for (uint32_t pi = 0; pi < c.pins_.size(); ++pi) {
            const DSPin& p = c.pins_[pi];
            const uint32_t local_pin_id = p.get_pin_id();
            if (!DSPinNameHasher::is_valid_id(local_pin_id) ||
                local_pin_id >=
                    src_part.pin_names_.name_table_.size()) {
                continue;  // 局部 hasher 与 pin 集不一致（不应发生，防御）
            }
            const CMString& key = src_part.pin_names_.get_name(local_pin_id);
            if (key.empty()) {
                continue;  // 局部 hasher 空洞（assign 稀疏未登记下标）
            }
            const uint32_t new_id = pin_base + local_pin_id;
            dst.pin_names_.assign(key, new_id);
            dst.cells_[dst_id].pins_[pi].set_pin_id(new_id);
            const auto git = src_geom.pin_geometry_.find(local_pin_id);
            if (git != src_geom.pin_geometry_.end()) {
                dst_geom.add_geometries(new_id,
                                        CMVector<DSShapeRef>(git->second));
            }
        }
    }

    // fake cell ids 与 lib_link 按 cell id 映射重挂
    for (uint32_t src_fid : src_part.fake_cell_ids_) {
        auto it = cell_id_map.find(src_fid);
        if (it != cell_id_map.end()) {
            dst.fake_cell_ids_.push_back(it->second);
        }
    }
    for (const auto& [src_id, lib_name] : src_part.lib_link_) {
        auto it = cell_id_map.find(src_id);
        if (it != cell_id_map.end()) {
            dst.lib_link_[it->second] = lib_name;
        }
    }

    // via cell 合入（重名保留首份 + DSGN::0005）
    for (const auto& v : src_part.via_cells_) {
        if (dst.find_via_cell(v.get_name()) != nullptr) {
            ++conflicts;
            MSG("DSGN::0005", 0,
                "duplicate via cell '{}' dropped (keeping first)",
                v.get_name());
            continue;
        }
        dst.add_via_cell(DSViaCell(v));
    }

    return conflicts;
}

// ── S4/S4b 汇总：DEF 头扫描产物并入全局容器 ─────────────────────────

int ds_merge_def_header(DSDesign& dst, const CMVector<DSCell>& block_cells,
                        const CMVector<CMString>& port_names,
                        DSPinGeometry& dst_geom,
                        const DSPinGeometry& port_geoms,
                        const CMVector<DSViaCell>& def_vias) {
    int conflicts = 0;

    for (const auto& blk : block_cells) {
        // block cell 与 macro 同一编号空间（⑯/㉙）：block 名进 cell
        // hasher（重名保留首份 + DSGN::0001）
        if (dst.find_cell(blk.get_name()) != nullptr) {
            ++conflicts;
            MSG("DSGN::0001", 0,
                "duplicate block cell '{}' dropped (keeping first)",
                blk.get_name());
            continue;
        }
        const uint32_t dst_id = dst.add_cell(DSCell(blk));

        // port pin id 全局平铺分配（D1），进 pin hasher（键 =
        // "design_name/port_name"，与 macro pin 同构）+ pin_id_ 回填 +
        // port 几何按全局 pin id 重挂（R4/R5：局部键 = pins_ 下标；
        // R7 ㊱：pin 名经 port_names 解析边界传入——DSPin 不存 name）
        const uint32_t pin_base =
            static_cast<uint32_t>(dst.pin_names_.name_table_.size());
        for (uint32_t pi = 0; pi < blk.pin_count(); ++pi) {
            const DSPin& p = blk.pin_at(pi);
            if (pi >= port_names.size()) {
                continue;  // 名单缺失（调用方契约错误，防御不越界）
            }
            const uint32_t new_id = pin_base + pi;
            dst.pin_names_.assign(blk.get_name() + "/" + port_names[pi],
                                  new_id);
            dst.cells_[dst_id].pins_[pi].set_pin_id(new_id);
            const auto git = port_geoms.pin_geometry_.find(pi);
            if (git != port_geoms.pin_geometry_.end()) {
                dst_geom.add_geometries(new_id,
                                        CMVector<DSShapeRef>(git->second));
            }
        }
    }

    // via cell 权威表合入（⑫ 前缀名天然隔离跨 DEF 重名；同前缀重名保留
    // 首份 + DSGN::0005）
    for (const auto& v : def_vias) {
        if (dst.find_via_cell(v.get_name()) != nullptr) {
            ++conflicts;
            MSG("DSGN::0005", 0,
                "duplicate via cell '{}' dropped (keeping first)",
                v.get_name());
            continue;
        }
        dst.add_via_cell(DSViaCell(v));
    }

    return conflicts;
}

// ── S5a 汇总：per-DEF 产物的 fake cell 并入全局容器 ─────────────────

int ds_merge_block_build(DSDesign& dst, DSBlockBuildData& block_data) {
    int merged = 0;

    // fake cell 并入（⑳：id 保持任务内分配值；目标位被占用 → 顺延到
    // 下一空位并重映射引用）。占位 cell（空名）即空位。
    for (const DSCell& fake_src : block_data.fake_cells_) {
        const CMString& fake_name = fake_src.get_name();
        if (dst.find_cell(fake_name) != nullptr) {
            continue;  // 同名 fake 已并入（同 block DEF 重复提交）——保留首份
        }
        auto id_it = block_data.fake_name_to_id_.find(fake_name);
        if (id_it == block_data.fake_name_to_id_.end()) {
            continue;  // 登记缺失（不应发生，防御）
        }
        uint32_t id = id_it->second;
        while (id < dst.cells_.size() && !dst.cells_[id].get_name().empty()) {
            ++id;  // 冲突顺延（⑳：冲突率不严格，兜底保证唯一落位）
        }
        DSCell fake = fake_src;
        dst.add_cell_at(id, std::move(fake));
        dst.fake_cell_ids_.push_back(id);
        ++merged;

        // 引用重映射（任务内分配 id → 全局落位 id）：instance.cell_id_ +
        // per-cell 统计键 + fake 登记表
        if (id != id_it->second) {
            for (auto& [iid, inst] : block_data.instances_) {
                if (inst.get_cell_id() == id_it->second) {
                    inst.set_cell_id(id);
                }
            }
            CMUnorderedMap<uint32_t, uint64_t> remapped;
            for (const auto& [cid, count] :
                 block_data.stats_.per_cell_counts_) {
                remapped[cid == id_it->second ? id : cid] = count;
            }
            block_data.stats_.per_cell_counts_ = std::move(remapped);
            id_it->second = id;
        }
    }

    return merged;
}

// ── S3：lib cell ↔ lef cell 结构 merge（裁定 ⑯；声明在 DSDesign）────

int DSDesign::merge_lib(const LIBLibrary& lib) {
    int matched = 0;
    CMVector<CMString> lef_only;  // 0002：lef 有 lib 无
    CMVector<CMString> lib_only;  // 0003：lib 有 lef 无
    auto tables = std::make_shared<DSPinTables>();

    for (uint32_t id = 0; id < cells_.size(); ++id) {
        DSCell& c = cells_[id];
        const LIBCell* lc = lib.find_cell(c.get_name());
        if (lc == nullptr) {
            lef_only.push_back(c.get_name());
            continue;
        }
        ++matched;
        // 匹配：填 lib 来源字段 + lib 关联（cell id → lib cell 名）
        // + ㉗ lib_cell 来源标记
        c.set_library_name(lc->library_name_);
        c.set_lib_cell();
        lib_link_[id] = lc->name_;

        // pin 集合比对（⑯：逐 cell 缺失 pin 名单，提醒不拦截）。
        // R7 ㊱：DSPin 不存 name——lef pin 名经 pin hasher（键 =
        // "cell_name/pin_name"）按 pin_id_ 反查（pin_name_of 取名段；
        // DSGN::0004 是用户可见输出，name 仅在边界转换）
        CMVector<CMString> missing_in_lib;
        CMVector<CMString> missing_in_lef;
        for (const auto& lp : lc->pins_) {
            bool found = false;
            for (const auto& dp : c.pins_) {
                if (pin_name_of(dp.get_pin_id()) == lp.name_) {
                    found = true;
                    break;
                }
            }
            if (!found) missing_in_lib.push_back(lp.name_);
        }
        for (const auto& dp : c.pins_) {
            bool found = false;
            for (const auto& lp : lc->pins_) {
                if (lp.name_ == pin_name_of(dp.get_pin_id())) {
                    found = true;
                    break;
                }
            }
            if (!found) missing_in_lef.push_back(pin_name_of(dp.get_pin_id()));
        }
        if (!missing_in_lib.empty() || !missing_in_lef.empty()) {
            MSG("DSGN::0004", 0,
                "cell '{}' pin mismatch: missing in lib [{}], missing in "
                "lef [{}]",
                c.get_name(), join_names(missing_in_lib),
                join_names(missing_in_lef));
        }

        // ⑰：逐 pin 提取 lib 功耗/时序表（R4 按全局 pin id 落位：lib
        // pin 名查 pin hasher（键 = cell_name/pin_name）得全局 id；查
        // 不到的 pin 即 pin 集合不一致，已在上方 DSGN::0004 缺失名单
        // 路径中提醒，其表无处挂载不落位）
        for (const auto& lp : lc->pins_) {
            CMString key = c.get_name() + "/" + lp.name_;
            const uint32_t pin_id = pin_names_.get_id(key);
            if (!DSPinNameHasher::is_valid_id(pin_id)) {
                continue;
            }
            CMVector<CMLookupTable> ip;
            CMVector<CMLookupTable> tm;
            for (const auto& grp : lp.internal_powers_) {
                for (const auto& t : grp.tables_) ip.push_back(t);
            }
            for (const auto& arc : lp.timings_) {
                for (const auto& t : arc.tables_) tm.push_back(t);
            }
            if (!ip.empty()) {
                tables->add_internal_power_tables(pin_id, std::move(ip));
            }
            if (!tm.empty()) {
                tables->add_timing_tables(pin_id, std::move(tm));
            }
        }
    }

    for (const auto& lc : lib.cells_) {
        if (find_cell(lc.name_) == nullptr) {
            lib_only.push_back(lc.name_);
        }
    }

    if (!lef_only.empty()) {
        MSG("DSGN::0002", 0,
            "{} lef cells have no lib counterpart (no current model): [{}]",
            lef_only.size(), join_names(lef_only));
    }
    if (!lib_only.empty()) {
        MSG("DSGN::0003", 0,
            "{} lib cells not covered by lef (skipped): [{}]",
            lib_only.size(), join_names(lib_only));
    }

    set_pin_tables(std::move(tables));
    return matched;
}

// —— S6：层级树构建 + 起始编号分配（⑧⑨⑮）——

DSHierTree ds_build_hier_tree(const CMVector<const DSBlockBuildData*>& blocks,
                              const CMVector<const DSNetBuildData*>& nets,
                              const DSDesign& design) {
    DSHierTree tree;
    if (blocks.empty()) {
        if (!nets.empty()) {
            // 不可恢复结构错误（DSGN::0011）：builder 输入不对齐，进程码 80 退出
            //（dev-rules §7 第三类处置，2026-09-12 裁定——原 raise 改 fatal message）。
            MSG_FATAL_EXIT("DSGN::0011", 0, 80,
                "ds_merge: nets without blocks — misaligned builder inputs");
        }
        return tree;  // 无 DEF 建库：空树（不触发 D22 零根 fatal）
    }
    if (nets.size() != blocks.size()) {
        MSG_FATAL_EXIT("DSGN::0011", 0, 80,
            "ds_merge: nets/blocks size mismatch ({} vs {}) — per-DEF inputs must align by def_paths order",
            nets.size(), blocks.size());
    }

    // 1) block cell 名 → def 序号（重名保留首份，与 S4 汇总语义一致）
    CMUnorderedMap<CMString, uint32_t> def_by_cell_name;
    for (uint32_t i = 0; i < blocks.size(); ++i) {
        def_by_cell_name.emplace(blocks[i]->get_block_name(), i);
    }

    // 2) block 引用收集（供入度统计与 DFS 共用）：fake cell 不在 design
    //    表或非 block_cell 位 → 非 block 引用（④ 解析阶段每 DEF 数据单
    //    份，定义 DAG 在实例展开时成树）
    const auto block_refs_of = [&](uint32_t def_idx) {
        // 返回 (local id, 子 def 序号) 升序列
        CMVector<std::pair<uint64_t, uint32_t>> refs;
        const DSBlockBuildData& block = *blocks[def_idx];
        for (const auto& [local_id, inst] : block.instances_) {
            if (local_id == 0 || inst.get_cell_id() >= design.cells_.size()) {
                continue;  // 占位 / fake（稀疏落位外的任务内 id）
            }
            const DSCell& cell = design.cells_[inst.get_cell_id()];
            if (!cell.is_block_cell()) {
                continue;
            }
            auto it = def_by_cell_name.find(cell.get_name());
            if (it == def_by_cell_name.end() || it->second == def_idx) {
                continue;  // block cell 定义未随 DEF 提供 / self-reference
            }
            refs.emplace_back(local_id, it->second);
        }
        std::sort(refs.begin(), refs.end());
        return refs;
    };

    // 3) 主 DEF = 唯一无父者（多根/零根 → 不可恢复结构错误，D22/DSGN::0011）
    CMVector<uint32_t> in_degree(blocks.size(), 0);
    for (uint32_t i = 0; i < blocks.size(); ++i) {
        for (const auto& [local_id, child] : block_refs_of(i)) {
            (void)local_id;
            ++in_degree[child];
        }
    }
    CMVector<uint32_t> roots;
    for (uint32_t i = 0; i < blocks.size(); ++i) {
        if (in_degree[i] == 0) {
            roots.push_back(i);
        }
    }
    if (roots.size() != 1) {
        MSG_FATAL_EXIT("DSGN::0011", 0, 80,
            "ds_merge: hierarchy roots are not unique ({} parentless defs) — invalid design",
            roots.size());
    }

    // 4) 自根 DFS：深度优先序连续分配三类起始编号（区间长度 = 该 block
    //    定义的计数，⑨）；定义层面的每次引用各建一个节点（实例层面为
    //    树）；当前路径重访同一 def = 环 → fatal（DSGN::0011，码 80 退出）。
    //    递归深度 = 层级深度。R7 ㊳：三类区间与 self_global_id 64 位。
    uint64_t inst_start = 0;
    uint64_t net_start = 0;
    uint64_t via_start = 0;
    CMVector<uint8_t> on_path(blocks.size(), 0);

    const std::function<void(uint32_t, const CMString&, uint32_t, uint64_t,
                             const GEOTransform&)>
        visit = [&](uint32_t def_idx, const CMString& instance_name,
                    uint32_t parent_id, uint64_t self_global_id,
                    const GEOTransform& composite) {
            if (on_path[def_idx] != 0) {
                MSG_FATAL_EXIT("DSGN::0011", 0, 80,
                    "ds_merge: block hierarchy cycle detected at '{}'",
                    blocks[def_idx]->get_block_name());
            }
            on_path[def_idx] = 1;

            const DSBlockBuildData& block = *blocks[def_idx];
            DSHierNode node;
            node.id_ = static_cast<uint32_t>(tree.nodes_.size());
            node.parent_id_ = parent_id;
            node.block_cell_name_ = block.get_block_name();
            node.instance_name_ = instance_name;
            // R7 ㊻：block cell 全局 id（DSNameMapperT 注入表主口键；
            // cell hasher 未命中 = kInvalidId——纯合成测试等场景）
            node.block_cell_id_ =
                design.cell_names_.get_id(block.get_block_name());
            node.self_global_id_ = self_global_id;
            // 自根复合放置变换（S9 展开的坐标基准；root 恒等，随 DFS
            // 递推：子复合 = 父复合 ∘ 父块实例表中本实例的放置 transform）
            node.composite_transform_ = composite;
            node.instance_start_ = inst_start;
            node.instance_count_ =
                static_cast<uint64_t>(block.instance_total());
            inst_start += node.instance_count_;
            node.net_start_ = net_start;
            node.net_count_ = static_cast<uint64_t>(block.net_count());
            net_start += node.net_count_;
            // via instance 区间：计数 = S5b 产物统计（与 S5a 计数同构
            // 入参），local id 从 1 起（⑨，换算同 net 语义）
            node.via_start_ = via_start;
            node.via_count_ = nets[def_idx]->stats_.via_instance_count;
            via_start += node.via_count_;
            const uint32_t node_id = node.id_;
            tree.nodes_.push_back(std::move(node));
            if (node_id != parent_id) {
                tree.nodes_[parent_id].get_ref_children_ids().push_back(
                    node_id);
            }

            for (const auto& [local_id, child_def] : block_refs_of(def_idx)) {
                // R7 ㊱：子 block instance 的实例名经双向 instance hasher
                // 反查（DSInstance 不存 name）。子复合变换 = 本复合 ∘ 本
                // 块实例表中该实例的放置 transform（实例缺失防御恒等——
                // 树与实例表同源不应发生，S8 scatter 同口径）
                const auto iit = block.instances_.find(local_id);
                const GEOTransform child_composite =
                    iit != block.instances_.end()
                        ? composite.compose(iit->second.get_transform())
                        : composite;
                visit(child_def,
                      block.instance_names_->get_name(local_id),
                      node_id, node.instance_start_ + local_id,
                      child_composite);
            }
            on_path[def_idx] = 0;
        };
    visit(roots[0], blocks[roots[0]]->get_block_name(), 0, 0,
          GEOTransform());

    tree.design_name_ = blocks[roots[0]]->get_block_name();
    return tree;
}

}  // namespace fly
