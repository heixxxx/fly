#include <emir/design/cpp/ds_name_mapper.h>

#include <cassert>

namespace fly {

// 2026-09-16 裁定 1/2（本文件路径语义）：实例/网层级路径**不含设计名
// 前缀**——hier_path_of 跳过 root 段（root 自身路径 = 空串、顶层平铺
// 实例/网 = 单段名）；root 实例名恒空串（get_full_name(0) 返回 ""、
// get_global_id("") = 0，对称语义，仅 instance 维度）。

// —— set_block_hasher 便利口（cell name 经树解析）——

template <typename IdT>
void DSNameMapperT<IdT>::set_block_hasher(
    const CMString& cell_name,
    CMSharedPtr<const DSNameHasherT<IdT>> hasher) {
    if (tree_ == nullptr) {
        return;  // 无树可解析（未知名场景的防御——不注册不生效）
    }
    for (size_t i = 0; i < tree_->node_count(); ++i) {
        const DSHierNode& n = tree_->node(static_cast<uint32_t>(i));
        if (n.get_block_cell_name() == cell_name) {
            set_block_hasher(n.get_block_cell_id().value(), std::move(hasher));
            return;  // 取首个同名 block 定义（与 def_by_cell_name 一致）
        }
    }
}

// —— get_global_id：路径分段（无堆）→ 分派索引一次定位 → 叶层 hasher
//    → 区间换算 ——

namespace {

// 节点的实例名路径（2026-09-16 裁定 1：**不含设计名前缀**——路径从顶
// 层内容起，root 段跳过：顶层平铺实例即单段名、root 自身 = 空串；外部
// 工具名字（TWF/网表）从不含设计名，按原「自根含 root」语义全量未命
// 中）。分派索引键（R8c）与 get_full_name 的 prefix 拼装共用
CMString hier_path_of(const DSHierTree& tree, uint32_t node_id) {
    CMVector<CMString> parts;
    const DSHierNode* n = &tree.node(node_id);
    while (true) {
        if (n->get_id() != n->get_parent_id()) {
            parts.push_back(n->get_instance_name());  // root 段不入路径
        }
        if (n->get_id() == n->get_parent_id()) {
            break;  // root 自指哨兵
        }
        n = &tree.node(n->get_parent_id());
    }
    CMString out;
    for (size_t i = parts.size(); i-- > 0;) {
        if (!out.empty()) {
            out += '/';
        }
        out += parts[i];
    }
    return out;
}

}  // namespace

template <typename IdT>
void DSNameMapperT<IdT>::rebuild_dispatch_index() {
    dispatch_.clear();
    if (tree_ == nullptr) {
        return;
    }
    // 键 = 节点实例名路径（2026-09-16 裁定 1：不含设计名前缀，root 段
    // 跳过——root 无路径键，空前缀查询由 get_global_id 直取 node 0）；
    // 值 = 节点 id。nodes_ 下标序 = DFS 前序。同名兄弟（非法树形态的防
    // 御场景——⑮ 实例名在 block 内唯一）保留首个：下标升序遍历 + 已存
    // 在跳过，与 find_child_by_instance_name 的 children 首个命中指向
    // 一致（children_ids_ 序 = 子节点 id 升序）。
    const uint32_t count = static_cast<uint32_t>(tree_->node_count());
    for (uint32_t i = 1; i < count; ++i) {  // i 从 1 起：root 不入索引
        const CMString key = hier_path_of(*tree_, i);
        if (dispatch_.find(key, DSHierTree::kNoNode) != DSHierTree::kNoNode) {
            continue;  // 同名兄弟防御：保留首个
        }
        dispatch_.insert(key, i);
    }
}

template <typename IdT>
IdT DSNameMapperT<IdT>::get_global_id(const CMString& full_hier_name) const {
    if (tree_ == nullptr || tree_->node_count() == 0) {
        return kInvalidId;
    }
    // 空串 = root 自身（2026-09-16 裁定 2 对称语义：root 实例名恒空串，
    // global id 0）。net 维度无「root 网」，恒未命中
    if (full_hier_name.empty()) {
        return kind_ == DSNameMapperKind::INSTANCE
                   ? tree_->node(0).get_self_global_id().value()
                   : kInvalidId;
    }
    // 分段全程无堆（R8c 裁定 53 顺带项）：叶段 = 最后一个 '/' 之后的
    // 尾段（最后一段必经叶层 hasher），前缀 = 其前全部段（即目标 block
    // instance 的层次路径，与分派索引键同构）。2026-09-16 裁定 1：路径
    // 不含设计名前缀——root 段不入索引（rebuild 跳过 node 0），空前缀
    // （单段名 = 顶层平铺实例/网）直取 root。前缀命中 ⟺ 逐层命中（索
    // 引含全部非 root 节点路径键）；前缀未命中场景（中间段断裂 / 空段 /
    // 多余尾段）的任何回退命中都带着 ≥ 2 段的尾，交叶层 hasher 必是误
    // 命中——一次 find 定案，无回退循环。路径长度 O(总长)。
    const size_t last_slash = full_hier_name.find_last_of('/');
    if (last_slash != CMString::npos &&
        last_slash + 1 == full_hier_name.size()) {
        return kInvalidId;  // 叶段为空
    }
    uint32_t node_id = DSHierTree::kNoNode;
    if (last_slash == CMString::npos) {
        node_id = 0;  // 单段名 = 顶层内容（root 块内的实例/网）
    } else {
        node_id = dispatch_.find(CMString(full_hier_name.data(), last_slash),
                                 DSHierTree::kNoNode);
        if (node_id >= tree_->node_count()) {
            return kInvalidId;  // 前缀非 block instance 路径
        }
    }
    const DSHierNode& cur = tree_->node(node_id);

    // 叶段：当前节点的注入 hasher 查 local id（㊻ 未注入 → 哨兵）
    const auto it = injected_.find(cur.get_block_cell_id().value());
    if (it == injected_.end()) {
        return kInvalidId;
    }
    const IdT local =
        it->second->get_id(CMString(full_hier_name.data() + last_slash + 1,
                                    full_hier_name.size() - last_slash - 1));
    if (!is_valid_id(local)) {
        return kInvalidId;
    }
    // 区间换算（⑨）：instance = start + local（local 0 → 自身 ⑧，hasher
    // 不登记 local 0、防御分支）；net = start + local（区间长度含 local 0
    // 空洞位，2026-09-14 裁定——无 −1）。树字段为强类型 id，机器值域
    // （IdT = 裸 uint64）边界显式转换
    if (kind_ == DSNameMapperKind::INSTANCE) {
        if (local == 0) {
            return cur.get_self_global_id().value();
        }
        if (local >= cur.get_instance_count()) {
            return kInvalidId;  // 越出该 block 区间（防御）
        }
        return (cur.get_instance_start() + local).value();
    }
    if (local == 0 || local >= cur.get_net_count()) {
        return kInvalidId;  // net local 0 = 空洞位（不登记名）/ 越界
    }
    // 区间长度含空洞位（2026-09-14 裁定）：global = start + local 无 −1
    return (cur.get_net_start() + local).value();
}

// —— get_full_name：区间反查 → 叶层 hasher → 递归向上拼 prefix ——

template <typename IdT>
CMString DSNameMapperT<IdT>::get_full_name(IdT global_id) const {
    if (tree_ == nullptr || tree_->node_count() == 0 || !is_valid_id(global_id)) {
        return {};
    }
    if (kind_ == DSNameMapperKind::INSTANCE) {
        const uint32_t node_id = tree_->block_of_instance(
            CMInstanceId{global_id});
        if (node_id == DSHierTree::kNoNode) {
            return {};
        }
        const DSHierNode& n = tree_->node(node_id);
        const CMString prefix = hier_path_of(*tree_, node_id);
        // 树字段强类型 id——与机器值域 IdT（裸 uint64）边界显式转换
        const IdT local = global_id - n.get_instance_start().value();
        if (local == 0) {
            return prefix;  // block instance 自身路径（⑧ local 0 占位）
        }
        const auto it = injected_.find(n.get_block_cell_id().value());
        if (it == injected_.end()) {
            return {};  // ㊻ 局部注入 = 局部可查
        }
        const CMString& name = it->second->get_name(local);
        if (name.empty()) {
            return {};  // 空洞（未登记下标）
        }
        // 裁定 1：root 块（prefix 空）的顶层实例 = 单段名
        return prefix.empty() ? name : prefix + "/" + name;
    }
    const uint32_t node_id =
        tree_->block_of_net(CMNetId{global_id});
    if (node_id == DSHierTree::kNoNode) {
        return {};
    }
    const DSHierNode& n = tree_->node(node_id);
    const auto it = injected_.find(n.get_block_cell_id().value());
    if (it == injected_.end()) {
        return {};
    }
    const CMString& name =
        it->second->get_name(global_id - n.get_net_start().value());
    if (name.empty()) {
        return {};  // 空洞（local 0 空洞位 / 未登记下标）
    }
    // 裁定 1：root 块（prefix 空）的顶层网 = 单段名
    const CMString prefix = hier_path_of(*tree_, node_id);
    return prefix.empty() ? name : prefix + "/" + name;
}

// 显式实例化（业务唯一实例化组；IdT = uint64，㊹ global id 空间）
template class DSNameMapperT<uint64_t>;

// —— 统一组装工厂（㊵②+㊻：读 DSBlockNames + 构造 mapper + 注入）——

DSInstanceNameMapper ds_make_name_mapper(
    const DSDesign& design, const CMVector<const DSBlockNames*>& names,
    DSNameMapperKind kind) {
    DSInstanceNameMapper mapper(&design.get_hier_tree(), kind);
    for (const DSBlockNames* bn : names) {
        if (bn == nullptr) {
            continue;
        }
        const uint32_t cell_id = design.cell_names_.get_id(bn->block_name_);
        if (!DSCellNameHasher::is_valid_id(cell_id)) {
            continue;  // block cell 未入全局表（防御跳过）
        }
        // 零拷贝：伴生对象持有即 CMSharedPtr，const 化转换共享计数
        //（CMSharedPtr<T> → CMSharedPtr<const T> 隐式）
        const CMSharedPtr<const DSNameHasherT<uint64_t>> view =
            kind == DSNameMapperKind::INSTANCE
                ? CMSharedPtr<const DSInstanceNameHasher>(bn->instance_names_)
                : CMSharedPtr<const DSNetNameHasher>(bn->net_names_);
        mapper.set_block_hasher(cell_id, view);
    }
    return mapper;
}

}  // namespace fly
