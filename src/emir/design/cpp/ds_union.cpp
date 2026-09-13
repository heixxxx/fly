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
uint32_t net_depth(const DSHierTree& tree, uint64_t net_global_id,
                   CMUnorderedMap<uint32_t, uint32_t>* memo) {
    const uint32_t node = tree.block_of_net(net_global_id);
    if (node == DSHierTree::kNoNode) {
        return UINT32_MAX;
    }
    return hier_depth(tree, node, memo);
}

}  // namespace

// —— DSNetUnion ——

uint64_t DSNetUnion::find(uint64_t net_global_id) const {
    const auto it = root_of_.find(net_global_id);
    return it == root_of_.end() ? net_global_id : it->second;
}

const CMVector<uint64_t>* DSNetUnion::members(uint64_t root) const {
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

    // 子定义端口索引：block 名 → (port 名 → 子网 local id 集)。扫描子网
    // 产物连接表的 ("PIN", port) 引用（S5b 名字形态，defi 回调语义）。
    CMUnorderedMap<CMString, CMUnorderedMap<CMString, CMVector<uint64_t>>>
        port_nets;
    for (const DSNetBuildData* child : child_defs) {
        if (child == nullptr) {
            continue;  // 防御（编排侧空条目）
        }
        auto& index = port_nets[child->get_block_name()];
        for (const auto& [local_id, conns] : child->connections_) {
            for (const DSNetConnection& c : conns) {
                if (c.is_port_ref()) {
                    index[c.get_pin_name()].push_back(local_id);
                }
            }
        }
    }

    // 本 def 的 port 网 local id 集（与实例化位置无关，一网一计）
    for (const auto& [local_id, conns] : parent_nets.connections_) {
        for (const DSNetConnection& c : conns) {
            if (c.is_port_ref()) {
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

    // 逐位置逐连接收集 (父网, 子网) 边：实例名命中树 children = block
    // instance（实例名块内唯一，叶实例无树节点）；同名 port 的子网集全
    // 并（同一子网连多 port 连到不同父网 → 两父网 union，电气等价）
    CMVector<std::pair<uint64_t, uint64_t>> edges;
    for (const uint32_t pos : positions) {
        for (const auto& [local_net, conns] : parent_nets.connections_) {
            const uint64_t parent_global = tree.global_net_id(pos, local_net);
            if (parent_global == DSHierTree::kNoNode) {
                continue;  // 防御：越界 local id（S5a/S5b 计数不一致兜底）
            }
            for (const DSNetConnection& c : conns) {
                if (c.is_port_ref()) {
                    continue;  // 顶层引脚连接：root 候选，不产生跨层 union
                }
                const uint32_t child_node = tree.find_child_by_instance_name(
                    pos, c.get_instance_name());
                if (child_node == DSHierTree::kNoNode) {
                    continue;  // 叶实例连接（非块实例），不产生跨层 union
                }
                const auto def_it = port_nets.find(
                    tree.node(child_node).get_block_cell_name());
                if (def_it == port_nets.end()) {
                    continue;  // 子定义网产物未提供（层级不完整兜底）
                }
                const auto nets_it = def_it->second.find(c.get_pin_name());
                if (nets_it == def_it->second.end()) {
                    continue;  // 该 port 未连接任何子网（父网不经此下探）
                }
                for (const uint64_t child_local : nets_it->second) {
                    const uint64_t child_global =
                        tree.global_net_id(child_node, child_local);
                    if (child_global == DSHierTree::kNoNode) {
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
    CMUnorderedMap<uint64_t, uint64_t> parent;
    const auto uf_find = [&parent](uint64_t x) {
        uint64_t root = x;
        for (auto it = parent.find(root);
             it != parent.end() && it->second != root;
             it = parent.find(root)) {
            root = it->second;
        }
        while (x != root) {  // 路径压缩
            const uint64_t next = parent[x];
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
            const uint64_t ra = uf_find(e.net_a_);
            const uint64_t rb = uf_find(e.net_b_);
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
        for (const uint64_t local : slice->port_net_ids_) {
            for (const uint32_t pos : pos_it->second) {
                const uint64_t g = tree.global_net_id(pos, local);
                if (g == DSHierTree::kNoNode) {
                    continue;  // 防御：越界 local id
                }
                parent.emplace(g, g);  // 已在边集 = 已并类（emplace 幂等）
            }
        }
    }

    // 3) 分组：root → 成员
    CMUnorderedMap<uint64_t, CMVector<uint64_t>> groups;
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
        uint64_t canonical = members.front();
        uint32_t best_depth = net_depth(tree, canonical, &depth_memo);
        for (const uint64_t m : members) {
            const uint32_t d = net_depth(tree, m, &depth_memo);
            if (d < best_depth) {
                best_depth = d;
                canonical = m;
            }
        }
        for (const uint64_t m : members) {
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
