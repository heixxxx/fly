#include <emir/design/cpp/ds_union.h>

#include <emir/design/cpp/ds_types.h>

#include <algorithm>
#include <utility>

namespace fly {

namespace {

// 树节点深度（parent 链上溯；root 自指 0 = 无父哨兵；memo 化——同一节
// 点在逐类 root 规范化中反复查询，量小）
uint32_t hier_depth(const DSHierTree& tree, uint32_t node_id,
                    CMUnorderedMap<uint32_t, uint32_t>* memo) {
    const auto it = memo->find(node_id);
    if (it != memo->end()) {
        return it->second;
    }
    uint32_t depth = 0;
    uint32_t cur = node_id;
    while (cur != 0 && cur != DSHierTree::kNoNode) {
        cur = tree.parent(cur);
        ++depth;
    }
    memo->emplace(node_id, depth);
    return depth;
}

// 网所属 block instance 的树深度（global id → 区间反查 → 深度；越界防
// 御回退最大值——规范化中自然落选）
uint32_t net_depth(const DSHierTree& tree, CMNetId net_global_id,
                   CMUnorderedMap<uint32_t, uint32_t>* memo) {
    const uint32_t node = tree.block_of_net(net_global_id);
    if (node == DSHierTree::kNoNode) {
        return UINT32_MAX;
    }
    return hier_depth(tree, node, memo);
}

}  // namespace

// —— DSNetUnion ——

CMNetId DSNetUnion::find(CMNetId net_global_id) const {
    const auto it = root_of_.find(net_global_id);
    return it == root_of_.end() ? net_global_id : it->second;
}

const CMVector<CMNetId>* DSNetUnion::members(CMNetId root) const {
    const auto it = members_of_.find(root);
    return it == members_of_.end() ? nullptr : &it->second;
}

uint64_t DSNetUnion::class_count() const { return members_of_.size(); }

// —— per-DEF 局部收集 ——

DSNetUnionSlice ds_collect_net_union_slice(
    const DSHierTree& tree,
    const DSNetBuildData& parent_nets,
    const CMVector<const DSNetBuildData*>& child_defs) {
    DSNetUnionSlice slice;
    slice.block_name_ = parent_nets.get_block_name();

    // 子定义端口索引（2026-09-16 裁定 3 键升维）：(子 block cell id,
    // port pin 全局 id) → 子网 local id 集。扫描子网产物连接表的 port
    // 引用条目（is_port 位，⑧ local 0 占位）。裁定 3 后 pin id = 全局
    // pin 名字空间 id（同名 port 跨 block 共享 id）——单 pin id 键会把
    // 不同子定义的同名 port 网误并，对接键升维为 (block cell id, pin id)
    // 定义级 port 身份；子 block cell id 经树扫描按 block 名反查（同名
    // block 保留首份，与 S6/S7 既有语义一致）。
    CMUnorderedMap<CMString, CMCellId> cell_id_of_block;
    for (const DSHierNode& node : tree.nodes_) {
        cell_id_of_block.emplace(node.get_block_cell_name(),
                                 node.get_block_cell_id());
    }
    // 两层索引：pin id → (子 block cell id → 子网 local id 集)
    CMUnorderedMap<CMPinId, CMUnorderedMap<CMCellId, CMVector<CMNetId>>>
        port_pin_nets;
    for (const DSNetBuildData* child : child_defs) {
        if (child == nullptr) {
            continue;  // 防御（编排侧空条目）
        }
        const auto cid_it = cell_id_of_block.find(child->get_block_name());
        if (cid_it == cell_id_of_block.end()) {
            continue;  // 防御：子定义不在树上（对齐错误已在树构建期 fatal）
        }
        for (const auto& [local_id, conns] : child->connections_) {
            for (const DSNetConnection& c : conns) {
                if (c.is_port()) {
                    port_pin_nets[c.pin_id_][cid_it->second].push_back(
                        local_id);
                }
            }
        }
    }

    // 本 def 的 port 网 local id 集（与实例化位置无关，一网一计）
    for (const auto& [local_id, conns] : parent_nets.connections_) {
        for (const DSNetConnection& c : conns) {
            if (c.is_port()) {
                slice.port_net_ids_.push_back(local_id);
                break;  // 一网一计
            }
        }
    }
    std::sort(slice.port_net_ids_.begin(), slice.port_net_ids_.end());
    slice.port_net_ids_.erase(
        std::unique(slice.port_net_ids_.begin(), slice.port_net_ids_.end()),
        slice.port_net_ids_.end());

    // 该 def 的全部实例化位置（树上 block cell 名匹配；无位置 = 该 def
    // 未被实例化——仅 port 网集有效，无边可收）
    CMVector<uint32_t> positions;
    for (const DSHierNode& node : tree.nodes_) {
        if (node.get_block_cell_name() == slice.block_name_) {
            positions.push_back(node.get_id());
        }
    }

    // 块实例 local id → 树节点 id 索引（id 对接键，2026-09-13 裁定：S7
    // 内部链路零字符串匹配）：非 root 节点 self_global_id_ = 父块
    // instance_start + 父块内 local id（⑧）。全局建一次（量 = 树节点数）。
    CMUnorderedMap<CMInstanceId, uint32_t> node_by_self_id;
    for (const DSHierNode& node : tree.nodes_) {
        node_by_self_id.emplace(node.get_self_global_id(), node.get_id());
    }

    // 逐位置逐连接收集 (父网, 子网) 边：父侧 (子实例 local id, port pin
    // 全局 id) × 子侧 (local 0, 同一 port pin id) 直接相等对接（同一
    // port 的全局 pin id 唯一）；同名 port 的子网集全并（同一子网连多
    // port 连到不同父网 → 两父网 union，电气等价）
    CMVector<std::pair<CMNetId, CMNetId>> edges;
    for (const uint32_t pos : positions) {
        const CMInstanceId inst_start = tree.instance_range(pos).first;
        for (const auto& [local_net, conns] : parent_nets.connections_) {
            const CMNetId parent_global = tree.global_net_id(pos, local_net);
            if (!parent_global.is_valid()) {
                continue;  // 防御：越界 local id（S5a/S5b 计数不一致兜底）
            }
            for (const DSNetConnection& c : conns) {
                if (c.is_port()) {
                    continue;  // 顶层引脚连接：root 候选，不产生跨层 union
                }
                // 子实例 local id → 树 child 节点（self_global_id 反查；
                // 叶实例不在树上 = 未命中，不产生跨层 union）
                const auto node_it =
                    node_by_self_id.find(inst_start + c.instance_local_id_);
                if (node_it == node_by_self_id.end()) {
                    continue;  // 叶实例连接（非块实例），不产生跨层 union
                }
                const uint32_t child_node = node_it->second;
                if (tree.parent(child_node) != pos) {
                    continue;  // 防御：非本位置的块实例（数据不变式兜底）
                }
                // 对接键 = (子块 cell id, port pin id)——定义级 port 身份
                const auto nets_it = port_pin_nets.find(c.pin_id_);
                if (nets_it == port_pin_nets.end()) {
                    continue;  // 该 port 未连接任何子网（父网不经此下探）
                }
                const auto sub_it = nets_it->second.find(
                    tree.node(child_node).get_block_cell_id());
                if (sub_it == nets_it->second.end()) {
                    continue;  // 同名 port 属其它子定义（裁定 3 隔离）
                }
                for (const CMNetId child_local : sub_it->second) {
                    const CMNetId child_global =
                        tree.global_net_id(child_node, child_local);
                    if (!child_global.is_valid()) {
                        continue;  // 防御：越界子网 local id
                    }
                    edges.emplace_back(std::minmax(parent_global,
                                                   child_global));
                }
            }
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    slice.edges_.reserve(edges.size());
    for (const auto& [a, b] : edges) {
        slice.edges_.push_back(DSNetUnionEdge{a, b});
    }
    return slice;
}

// —— 全局汇总 ——

DSNetUnion ds_build_net_union(const DSHierTree& tree,
                              const CMVector<const DSNetUnionSlice*>& slices) {
    DSNetUnion out;

    // 1) 小规模并查集（port 级规模，路径压缩）
    CMUnorderedMap<CMNetId, CMNetId> parent;
    const auto uf_find = [&parent](CMNetId x) {
        CMNetId root = x;
        for (auto it = parent.find(root);
             it != parent.end() && it->second != root;
             it = parent.find(root)) {
            root = it->second;
        }
        while (x != root) {  // 路径压缩
            const CMNetId next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    };
    for (const DSNetUnionSlice* slice : slices) {
        if (slice == nullptr) {
            continue;
        }
        for (const DSNetUnionEdge& e : slice->edges_) {
            parent.emplace(e.net_a_, e.net_a_);
            parent.emplace(e.net_b_, e.net_b_);
            const CMNetId ra = uf_find(e.net_a_);
            const CMNetId rb = uf_find(e.net_b_);
            if (ra != rb) {
                parent[ra] = rb;
            }
        }
    }
    // 无边不提前返回（review 2026-09-13 运行证实：全部 port 未连父网的
    // 输入形态下悬空判定被整体跳过，违反裁定 ④）——悬空 emplace 循环
    // 对空 parent 天然安全（emplace 单成员类），空树/空 slice 由后续
    // positions_of 空 + groups 空自然收敛为空结果放行

    // 2) 悬空 port 网：不在任何边上的非 root 块 port 网（root = 自身的
    //    单成员类，裁定 ④）。root 块 port 网 = 顶层引脚连接（root 候选
    //    语义），排除。block 名 → 实例化位置一次建好（树扫描一遍）。
    CMUnorderedMap<CMString, CMVector<uint32_t>> positions_of;
    for (const DSHierNode& node : tree.nodes_) {
        positions_of[node.get_block_cell_name()].push_back(node.get_id());
    }
    const CMString root_block_name =
        tree.node_count() > 0 ? tree.node(0).get_block_cell_name() : CMString();
    for (const DSNetUnionSlice* slice : slices) {
        if (slice == nullptr || slice->block_name_ == root_block_name) {
            continue;
        }
        const auto pos_it = positions_of.find(slice->block_name_);
        if (pos_it == positions_of.end()) {
            continue;  // def 未被实例化：无位置即无 global id 可悬空
        }
        for (const CMNetId local : slice->port_net_ids_) {
            for (const uint32_t pos : pos_it->second) {
                const CMNetId g = tree.global_net_id(pos, local);
                if (!g.is_valid()) {
                    continue;  // 防御：越界 local id
                }
                parent.emplace(g, g);  // 已在边集 = 已并类（emplace 幂等）
            }
        }
    }

    // 3) 分组：root → 成员
    CMUnorderedMap<CMNetId, CMVector<CMNetId>> groups;
    for (const auto& [id, p] : parent) {
        (void)p;
        groups[uf_find(id)].push_back(id);
    }

    // 4) root 规范化（层级最高优先、同级最小 global id）+ 两层重挂。
    //    成员升序后首个 = 最小 id，层级严格更低者才替换——同级自然保留
    //    最小 global id。
    CMUnorderedMap<uint32_t, uint32_t> depth_memo;
    for (auto& [root, members] : groups) {
        (void)root;
        std::sort(members.begin(), members.end());
        CMNetId canonical = members.front();
        uint32_t best_depth = net_depth(tree, canonical, &depth_memo);
        for (const CMNetId m : members) {
            const uint32_t d = net_depth(tree, m, &depth_memo);
            if (d < best_depth) {
                best_depth = d;
                canonical = m;
            }
        }
        for (const CMNetId m : members) {
            out.root_of_[m] = canonical;  // 含 canonical 自映射（两层不变式）
        }
        out.members_of_[canonical] = std::move(members);
        if (out.members_of_[canonical].size() == 1) {
            ++out.dangling_count_;  // 单成员类 = 悬空 port 网（裁定 ④）
        }
    }
    return out;
}

// —— S7 编排辅助 ——

CMVector<uint32_t> ds_net_union_child_indexes(
    const DSHierTree& tree, const CMVector<CMString>& block_names,
    uint32_t index) {
    CMVector<uint32_t> out;
    if (index >= block_names.size()) {
        return out;  // 调用方契约（def_paths 序对齐）防御
    }
    // block 名 → def 序号（重名保留首份，与 S6 树构建 def_by_cell_name
    // 同语义）
    CMUnorderedMap<CMString, uint32_t> index_of;
    for (uint32_t i = 0; i < block_names.size(); ++i) {
        index_of.emplace(block_names[i], i);
    }
    CMSet<uint32_t> seen;
    for (const DSHierNode& node : tree.nodes_) {
        if (node.get_block_cell_name() != block_names[index]) {
            continue;
        }
        for (const uint32_t child : node.get_children_ids()) {
            const auto it =
                index_of.find(tree.node(child).get_block_cell_name());
            if (it == index_of.end() || it->second == index) {
                continue;  // 定义未提供 / 自引用防御
            }
            if (seen.insert(it->second).second) {
                out.push_back(it->second);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace fly
