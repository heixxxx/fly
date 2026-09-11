#include <emir/design/cpp/ds_name_mapper.h>

#include <cassert>

namespace fly {

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
            set_block_hasher(n.get_block_cell_id(), std::move(hasher));
            return;  // 取首个同名 block 定义（与 def_by_cell_name 一致）
        }
    }
}

// —— get_global_id：路径分段（无堆）→ 分派索引一次定位 → 叶层 hasher
//    → 区间换算 ——

namespace {

// 节点的实例名路径（自根逐层 '/' 连接；root 实例名 = block 名自指 ⑧）。
// 分派索引键（R8c）与 get_full_name 的 prefix 拼装共用
CMString hier_path_of(const DSHierTree& tree, uint32_t node_id) {
    CMVector<CMString> parts;
    const DSHierNode* n = &tree.node(node_id);
    while (true) {
        parts.push_back(n->get_instance_name());
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
    // 键 = 节点实例名路径（自根 '/' 连接，含 root——root 键即其实例名
    // 自身，承接两段路径（root + 叶名）的前缀查询）；值 = 节点 id。
    // nodes_ 下标序 = DFS 前序。同名兄弟（非法树形态的防御场景——⑮
    // 实例名在 block 内唯一）保留首个：下标升序遍历 + 已存在跳过，与
    // find_child_by_instance_name 的 children 首个命中指向一致
    //（children_ids_ 序 = 子节点 id 升序）。
    const uint32_t count = static_cast<uint32_t>(tree_->node_count());
    for (uint32_t i = 0; i < count; ++i) {
        const CMString key = hier_path_of(*tree_, i);
        if (dispatch_.find(key, DSHierTree::kNoNode) != DSHierTree::kNoNode) {
            continue;  // 同名兄弟防御：保留首个
        }
        dispatch_.insert(key, i);
    }
}

template <typename IdT>
IdT DSNameMapperT<IdT>::get_global_id(const CMString& full_hier_name) const {
    if (tree_ == nullptr || tree_->node_count() == 0 ||
        full_hier_name.empty()) {
        return kInvalidId;
    }
    // 分段全程无堆（R8c 裁定 53 顺带项）：叶段 = 最后一个 '/' 之后的
    // 尾段（原语义固定——最后一段必经叶层 hasher，中间段集合不变），
    // 前缀 = 其前全部段（即目标 block instance 的层次全路径，与分派
    // 索引键同构）。裁定 53 的「find(全路径) 不命中逐段去尾重查」在
    // 该叶段固定规则下收编为「剥叶段后前缀一次 find」：前缀命中 ⟺ 原
    // 逐层 find_child_by_instance_name 逐层命中（索引含全部节点路径键）；
    // 前缀未命中场景（中间段断裂 / 空段 / 多余尾段）继续去尾重查的任何
    // 命中都带着 ≥ 2 段的尾，交叶层 hasher 必是原实现不可达的误命中，
    // 语义保持要求一律未命中——故一次 find 定案，无回退循环。路径长度
    // O(总长) 替代原每层线性扫 O(扇出×段长)。
    const size_t last_slash = full_hier_name.find_last_of('/');
    if (last_slash == CMString::npos ||
        last_slash + 1 == full_hier_name.size()) {
        return kInvalidId;  // 无分隔单段（root 自身无叶名可查）/ 叶段为空
    }
    const uint32_t node_id =
        dispatch_.find(CMString(full_hier_name.data(), last_slash),
                       DSHierTree::kNoNode);
    if (node_id >= tree_->node_count()) {
        return kInvalidId;  // 前缀非 block instance 路径（含 root 首段
                            // 不匹配——索引键皆以 root 实例名开头）
    }
    const DSHierNode& cur = tree_->node(node_id);

    // 叶段：当前节点的注入 hasher 查 local id（㊻ 未注入 → 哨兵）
    const auto it = injected_.find(cur.get_block_cell_id());
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
    // 不登记 local 0、防御分支）；net = start + local − 1（local 从 1 起）
    if (kind_ == DSNameMapperKind::INSTANCE) {
        if (local == 0) {
            return cur.get_self_global_id();
        }
        if (local >= cur.get_instance_count()) {
            return kInvalidId;  // 越出该 block 区间（防御）
        }
        return cur.get_instance_start() + local;
    }
    if (local == 0 || local > cur.get_net_count()) {
        return kInvalidId;  // net local 0 保留未用 / 越界
    }
    return cur.get_net_start() + local - 1;
}

// —— get_full_name：区间反查 → 叶层 hasher → 递归向上拼 prefix ——

template <typename IdT>
CMString DSNameMapperT<IdT>::get_full_name(IdT global_id) const {
    if (tree_ == nullptr || tree_->node_count() == 0 || !is_valid_id(global_id)) {
        return {};
    }
    if (kind_ == DSNameMapperKind::INSTANCE) {
        const uint32_t node_id = tree_->block_of_instance(global_id);
        if (node_id == DSHierTree::kNoNode) {
            return {};
        }
        const DSHierNode& n = tree_->node(node_id);
        const CMString prefix = hier_path_of(*tree_, node_id);
        const IdT local = global_id - n.get_instance_start();
        if (local == 0) {
            return prefix;  // block instance 自身路径（⑧ local 0 占位）
        }
        const auto it = injected_.find(n.get_block_cell_id());
        if (it == injected_.end()) {
            return {};  // ㊻ 局部注入 = 局部可查
        }
        const CMString& name = it->second->get_name(local);
        if (name.empty()) {
            return {};  // 空洞（未登记下标）
        }
        return prefix + "/" + name;
    }
    const uint32_t node_id = tree_->block_of_net(global_id);
    if (node_id == DSHierTree::kNoNode) {
        return {};
    }
    const DSHierNode& n = tree_->node(node_id);
    const auto it = injected_.find(n.get_block_cell_id());
    if (it == injected_.end()) {
        return {};
    }
    const CMString& name = it->second->get_name(global_id - n.get_net_start() + 1);
    if (name.empty()) {
        return {};
    }
    return hier_path_of(*tree_, node_id) + "/" + name;
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
