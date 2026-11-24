#include <emir/spef/cpp/sp_partition.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fly {
namespace {

// 按分隔符拆段（strip_prefix 段匹配与块内名字拆段共用；段级精确匹配
// 口径——防 u_dut 误配 u_dutx，同 timing/waveform 先例）
CMVector<CMString> split_segments(const CMString& name, char delim) {
    CMVector<CMString> segs;
    size_t begin = 0;
    for (;;) {
        const size_t pos = name.find(delim, begin);
        if (pos == CMString::npos) {
            segs.emplace_back(name.data() + begin, name.size() - begin);
            return segs;
        }
        segs.emplace_back(name.data() + begin, pos - begin);
        begin = pos + 1;
    }
}

// 段序列以 '/' 重组为 design db 层级路径语法（层级路径恒 '/'）
CMString join_segments(const CMVector<CMString>& segs) {
    CMString out;
    for (const CMString& s : segs) {
        if (!out.empty()) {
            out += '/';
        }
        out += s;
    }
    return out;
}

// strip_prefix 段级剥离（§2.2）：名字段序列与前缀段起始精确匹配则剥
// 去；未命中 / 剥后余空返回 false（调用方 strip_miss_count 计数跳过）
bool strip_prefix_segments(const CMString& name,
                           const CMVector<CMString>& prefix_segs,
                           CMString& out) {
    const CMVector<CMString> segs = split_segments(name, '/');
    if (segs.size() <= prefix_segs.size()) {
        return false;  // 剥后必空或段数不足
    }
    for (size_t i = 0; i < prefix_segs.size(); ++i) {
        if (segs[i] != prefix_segs[i]) {
            return false;
        }
    }
    CMVector<CMString> rest(segs.begin() +
                                static_cast<long>(prefix_segs.size()),
                            segs.end());
    out = join_segments(rest);
    return !out.empty();
}

// 层次实例路径 → 树节点 id（路径不含设计名前缀——自 root 内容段起，
// 2026-09-16 命名裁定；块实例定位专用，同 timing 先例）。未命中 kNoNode
uint32_t find_node_by_path(const DSHierTree& tree, const CMString& path) {
    if (path.empty()) {
        return tree.node_count() > 0 ? 0u : DSHierTree::kNoNode;
    }
    uint32_t node_id = 0;
    for (const CMString& seg : split_segments(path, '/')) {
        node_id = tree.find_child_by_instance_name(node_id, seg);
        if (node_id == DSHierTree::kNoNode) {
            return DSHierTree::kNoNode;
        }
    }
    return node_id;
}

// 名字含转义判定（SPEF 转义 = 反斜杠转义下一字符）
bool has_escape(const CMString& name) {
    return name.find('\\') != CMString::npos;
}

// 去掉全部反斜杠（转义的下一字符原样保留——查找容错的回退名）
CMString unescape_name(const CMString& name) {
    if (!has_escape(name)) {
        return name;
    }
    CMString out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '\\' && i + 1 < name.size()) {
            ++i;  // 跳过反斜杠，转义字符保留
        }
        out += name[i];
    }
    return out;
}

// 层次分隔符归一（§2.2）：文件头分隔符非 '/' 时把未被转义的分隔符字
// 符换成 '/'（反斜杠转义的下一个字符原样保留——字面义字符）；分隔符
// 已是 '/' 时原样返回
CMString normalize_hier_name(const CMString& name, char divider) {
    if (divider == '/') {
        return name;
    }
    CMString out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '\\' && i + 1 < name.size()) {
            out += c;
            out += name[++i];  // 转义对原样保留
        } else if (c == divider) {
            out += '/';
        } else {
            out += c;
        }
    }
    return out;
}

// —— 换算上下文聚合（避免逐条目长参数）———————————————————————

struct ConvertEnv {
    const SPDesignContext* ctx;
    char divider;                            // 文件头层次分隔符
    const CMVector<CMString>* prefix_segs;   // strip_prefix 段表（可空）
    const CMVector<uint32_t>* block_nodes;   // 块绑定节点集（可空 = PATH）
    SPFileStats* fstats;
    SPEntrySlice* slice;
};

// 名字处理管线结果：kNameMiss = 查找未命中（调用方计 net_name_miss）；
// kStripMiss = strip_prefix 未命中（管线内已计数）
enum class NameOutcome { kHit, kStripMiss, kNameMiss };

// 树子节点按名查找（转义容错：原名未命中且含反斜杠 → 去转义再查一次）
uint32_t find_child_forgiving(const DSHierTree& tree, uint32_t node_id,
                              const CMString& name, bool forgiving) {
    uint32_t child = tree.find_child_by_instance_name(node_id, name);
    if (child != DSHierTree::kNoNode) {
        return child;
    }
    if (forgiving && has_escape(name)) {
        return tree.find_child_by_instance_name(node_id, unescape_name(name));
    }
    return DSHierTree::kNoNode;
}

// 块内局部下降（多段名 = 层级网语义——末段用下降叶块自己的 hasher，
// 同 waveform 先例；段查找带转义容错）。未命中返回 nullptr。
const DSHierNode* descend_block_node(const DSHierTree& tree,
                                     const DSHierNode& leaf,
                                     const CMVector<CMString>& segs,
                                     bool forgiving) {
    const DSHierNode* node = &leaf;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        const uint32_t child =
            find_child_forgiving(tree, node->get_id(), segs[i], forgiving);
        if (child == DSHierTree::kNoNode) {
            return nullptr;
        }
        node = &tree.node(child);
    }
    return node;
}

// 块定义 net hasher 查找（容错同全局口径）
CMNetId lookup_local_net(const DSNetNameHasher& hasher, const CMString& name,
                         bool forgiving) {
    CMNetId local = hasher.get_id(name);
    if (local.is_valid()) {
        return local;
    }
    if (forgiving && has_escape(name)) {
        return hasher.get_id(unescape_name(name));
    }
    return CMNetId{};
}

// 名字处理管线一次（归一 → strip_prefix → 查找三形态）。候选全局网 id
// 收进 ids_out（空 = 名字未命中）。forgiving = true 时查找段带去转义回
// 退（全管线容错的第二次尝试——strip 结果与第一次一致，无重复计数）。
NameOutcome resolve_once(ConvertEnv& env, const CMString& raw_name,
                         bool forgiving, CMVector<CMNetId>& ids_out) {
    ids_out.clear();
    // ① 归一：文件头分隔符非 '/' 时换 '/'（转义感知）
    const CMString name = normalize_hier_name(raw_name, env.divider);
    // ② strip_prefix 段级剥离（按归一后的 '/' 段）
    CMString lookup_name = name;
    if (env.prefix_segs != nullptr) {
        if (!strip_prefix_segments(name, *env.prefix_segs, lookup_name)) {
            ++env.fstats->strip_miss_count_;   // SPMG::0002
            return NameOutcome::kStripMiss;
        }
    }
    // ③ 查找三形态
    if (env.block_nodes == nullptr || env.block_nodes->empty()) {
        // PATH：全局 net mapper（条目名 = 全层级路径，不含设计名前缀）
        CMNetId id = env.ctx->net_mapper().get_global_id(lookup_name);
        if (!id.is_valid() && forgiving && has_escape(lookup_name)) {
            id = env.ctx->net_mapper().get_global_id(
                unescape_name(lookup_name));
        }
        if (!id.is_valid()) {
            return NameOutcome::kNameMiss;
        }
        ids_out.push_back(id);
        return NameOutcome::kHit;
    }
    // 块绑定：逐块节点局部下降 + 偏移换算（BLOCK_INST = 单节点、
    // BLOCK_CELL = block cell 全部实例化节点——建库期复制，§2.2）
    const DSHierTree& tree = env.ctx->design().get_hier_tree();
    const CMVector<CMString> segs = split_segments(lookup_name, '/');
    bool hit = false;
    for (const uint32_t node_id : *env.block_nodes) {
        const DSHierNode& leaf = tree.node(node_id);
        const DSHierNode* final_node =
            descend_block_node(tree, leaf, segs, forgiving);
        if (final_node == nullptr) {
            continue;
        }
        const CMSharedPtr<const DSNetNameHasher> hasher =
            env.ctx->net_hasher_of(final_node->get_block_cell_id());
        if (hasher == nullptr) {
            continue;   // 叶块未注入防御（入口校验后环境漂移）
        }
        const CMNetId local =
            lookup_local_net(*hasher, segs.back(), forgiving);
        if (!local.is_valid() || local == 0 ||
            local >= final_node->get_net_count()) {
            continue;   // 块内未命中（local 0 = 空洞位不登记名）
        }
        // 区间换算（net_start + local 无 −1，2026-09-14 裁定——net 区间
        // 长度含 local 0 空洞位）
        ids_out.push_back(final_node->get_net_start() + local);
        hit = true;
    }
    return hit ? NameOutcome::kHit : NameOutcome::kNameMiss;
}

// 任一分区 NETS 副本定位网（信号网 NETS 全量补全——任一副本即完整连接
// 表，同 timing/waveform 既有先例）
CMSharedPtr<const DSNet> find_net_any_partition(const SPDesignContext& ctx,
                                                CMNetId net_id) {
    for (const auto& entry : ctx.all_partition_nets()) {
        auto candidate = entry.second->net_of(net_id).lock();
        if (candidate != nullptr) {
            return candidate;
        }
    }
    return nullptr;
}

// 单网路由（T2 步骤 3-6）：定位网 → 可挂连接条目（driver / receiver；
// 端口条目不是实例——排除，不计未放置）→ 连接位实例 primary 分区各放
// 一条（同分区同网只产一条；值取自 SPNet，R_NET 标缩减形态〔裁定 5〕）。
// 至少一条产出 = true。
bool route_net(ConvertEnv& env, CMNetId net_id, const SPNet& src) {
    const CMSharedPtr<const DSNet> net =
        find_net_any_partition(*env.ctx, net_id);
    if (net == nullptr) {
        // 网名命中但 design db 无布线几何（分区 NETS 无此网）——未布线
        // 设计的合法兜底（SPMG::0011），下游该网视为未覆盖
        ++env.fstats->unrouted_net_count_;
        return false;
    }
    CMVector<const DSNetConnEntry*> attachable;
    for (const DSNetConnEntry& conn : net->connections_) {
        // 端口条目不是实例（实例编号 = 块自身占位值，根块为 0——查不
        // 到分区），路由只认实例连接位，直接跳过、不计未放置实例。判
        // 定只看 is_port()，不看驱动/接收标记（设计库对端口条目标记的
        // 口径后续会调整——输入端口将标成它所在网的驱动者，依赖标记判
        // 端口的代码到时会被带坏）
        if (conn.is_port()) {
            continue;
        }
        if (conn.is_driver() || conn.is_receiver()) {
            attachable.push_back(&conn);
        }
    }
    if (attachable.empty()) {
        ++env.fstats->unrouted_net_count_;   // 无可挂连接位
        return false;
    }
    SPNetEntry entry;
    entry.net_id_ = net_id;
    entry.total_cap_reported_ff_ = src.total_cap_reported_ff_;
    entry.ground_cap_ff_ = src.ground_cap_ff_;
    entry.coupling_cap_ff_ = src.coupling_cap_ff_;
    if (src.kind_ == SPNetKind::R_NET) {
        entry.flags_ = kSpFlagReduced;
    }
    bool hit = false;
    CMUnorderedSet<CMPartitionId> seen;   // 同分区同网只产一条（大扇出网
                                          // 去重 O(1)——时钟/复位上万连接位）
    for (const DSNetConnEntry* conn : attachable) {
        const CMPartitionId pid =
            env.ctx->locate_instance_partition(conn->inst_id_);
        if (!pid.is_valid()) {
            ++env.fstats->unplaced_instance_count_;   // 未放置实例跳过
            continue;
        }
        if (!seen.insert(pid).second) {
            continue;
        }
        SPPartitionSlice& part = env.slice->partitions_[pid];
        part.part_id_ = pid;
        part.entries_.push_back(entry);
        hit = true;
    }
    if (!hit) {
        // 有可挂条目但连接位实例全部未放置：不产条目，计未布线
        ++env.fstats->unrouted_net_count_;
    }
    return hit;
}

// 绑定定位（一次性；BLOCK_INST = 块实例节点、BLOCK_CELL = block cell 全
// 部实例化节点——timing 同款。定位失败 = 入口校验后环境漂移的防御场景，
// 调用方按块失败兜底计数——不 raise）
CMVector<uint32_t> resolve_binding_nodes(const SPDesignContext& ctx,
                                         const SPFileBinding& binding,
                                         bool& ok) {
    CMVector<uint32_t> nodes;
    ok = true;
    if (binding.kind == SPFileBindingKind::BLOCK_INST) {
        const uint32_t node_id = find_node_by_path(
            ctx.design().get_hier_tree(), binding.block_inst);
        if (node_id == DSHierTree::kNoNode) {
            ok = false;
            return nodes;
        }
        nodes.push_back(node_id);
    } else if (binding.kind == SPFileBindingKind::BLOCK_CELL) {
        const CMCellId block_cell =
            ctx.design().cell_names_.get_id(binding.block_cell);
        if (!block_cell.is_valid()) {
            ok = false;
            return nodes;
        }
        const DSHierTree& tree = ctx.design().get_hier_tree();
        for (uint32_t i = 0; i < tree.node_count(); ++i) {
            if (tree.node(i).get_block_cell_id() == block_cell) {
                nodes.push_back(i);
            }
        }
        if (nodes.empty()) {
            ok = false;
        }
    }
    return nodes;
}

}  // namespace

// ── T2 换算上下文（WFDesignContext 同构实现——运行时构件不序列化）──

void SPDesignContext::set_design(CMSharedPtr<DSDesign> design) {
    design_ = std::move(design);
    rebuild_mappers();
}

void SPDesignContext::rebuild_mappers() {
    if (!design_) {
        return;
    }
    // 两维度 mapper（树观察 = aliasing shared_ptr 持 design_ 计数——
    // §16 业务层零裸指针；构造即建分派索引）
    const CMSharedPtr<const DSHierTree> tree{design_,
                                             &design_->get_hier_tree()};
    inst_mapper_ = DSInstanceNameMapper(tree);
    net_mapper_ = DSNetNameMapper(tree);
    // hasher 注入关系重挂（mapper 为运行时构件，重组装后由此恢复）
    for (const auto& entry : inst_hashers_) {
        inst_mapper_.set_block_hasher(entry.first, entry.second);
    }
    for (const auto& entry : net_hashers_) {
        net_mapper_.set_block_hasher(entry.first, entry.second);
    }
}

void SPDesignContext::add_block_names(CMSharedPtr<const DSBlockNames> names) {
    if (!names || !design_) {
        return;
    }
    const CMCellId block_cell_id =
        design_->cell_names_.get_id(names->block_name_);
    if (!block_cell_id.is_valid()) {
        return;  // block cell 未入全局表（防御跳过，同 ds_make_name_mapper）
    }
    // map 存非 const（序列化约束）、mapper 注入 const 化只读视图
    inst_hashers_[block_cell_id] = names->instance_names_;
    net_hashers_[block_cell_id] = names->net_names_;
    inst_mapper_.set_block_hasher(
        block_cell_id,
        CMSharedPtr<const DSInstanceNameHasher>(inst_hashers_[block_cell_id]));
    net_mapper_.set_block_hasher(
        block_cell_id,
        CMSharedPtr<const DSNetNameHasher>(net_hashers_[block_cell_id]));
}

void SPDesignContext::set_inst_id_map(CMSharedPtr<DSIdPartitionIndex> index) {
    inst_index_ = std::move(index);
}

void SPDesignContext::add_inst_segment(
    CMSharedPtr<DSIdPartitionSegment> segment) {
    if (segment) {
        inst_segments_[segment->id_start_] = std::move(segment);
    }
}

void SPDesignContext::set_pg_nets(CMSharedPtr<DSPgNetSet> pg_nets) {
    pg_nets_ = std::move(pg_nets);
}

void SPDesignContext::add_partition_nets(CMSharedPtr<DSPartitionNets> nets) {
    if (nets) {
        part_nets_[nets->part_id_.value()] = std::move(nets);
    }
}

void SPDesignContext::add_partition_inst_connections(
    CMPartitionId pid, CMSharedPtr<DSPartInstConnections> conns) {
    if (conns) {
        inst_conns_[pid.value()] = std::move(conns);
    }
}

void SPDesignContext::set_net_union(CMSharedPtr<DSNetUnion> net_union) {
    net_union_ = std::move(net_union);
}

CMPartitionId SPDesignContext::locate_instance_partition(
    CMInstanceId inst_id) const {
    if (!inst_index_) {
        return CMPartitionId{};
    }
    const uint64_t seg_start =
        inst_index_->find_segment_start(inst_id.value());
    if (seg_start == DSIdPartitionIndex::kIdMapNoSegment) {
        return CMPartitionId{};
    }
    const auto it = inst_segments_.find(seg_start);
    if (it == inst_segments_.end()) {
        return CMPartitionId{};
    }
    return it->second->partition_of(inst_id.value());
}

CMSharedPtr<const DSInstanceNameHasher> SPDesignContext::inst_hasher_of(
    CMCellId block_cell_id) const {
    const auto it = inst_hashers_.find(block_cell_id);
    return it == inst_hashers_.end()
               ? nullptr
               : CMSharedPtr<const DSInstanceNameHasher>(it->second);
}

CMSharedPtr<const DSNetNameHasher> SPDesignContext::net_hasher_of(
    CMCellId block_cell_id) const {
    const auto it = net_hashers_.find(block_cell_id);
    return it == net_hashers_.end()
               ? nullptr
               : CMSharedPtr<const DSNetNameHasher>(it->second);
}

// ── T2 逐块换算 ─────────────────────────────────────────────────────

SPEntrySlice sp_convert_chunk(const SPDesignContext& ctx,
                              const SPFileHeader& header,
                              const SPChunkResult& parsed, uint32_t file_index,
                              uint32_t scope_index, const SPFileBinding& binding,
                              const CMString& source_file) {
    SPEntrySlice slice;
    slice.file_index_ = file_index;
    slice.scope_index_ = scope_index;

    SPFileStats fstats;
    fstats.source_file_ = source_file;
    // 电容单位换算系数（1 文件单位 = 多少 fF——可追溯，裁定 7）
    fstats.c_unit_ff_per_unit_ = header.cap_scale_ff_;
    // 解析层计数原样带入（SPMG 消息族的解析侧计数源）
    fstats.net_count_ = parsed.net_count_;
    fstats.rnet_count_ = parsed.rnet_count_;
    fstats.pnet_skip_count_ = parsed.pnet_skip_count_;
    fstats.broken_net_count_ = parsed.broken_net_count_;
    fstats.name_map_miss_count_ = parsed.name_map_miss_count_;
    fstats.dropped_record_count_ = parsed.dropped_record_count_;
    fstats.multi_value_net_count_ = parsed.multi_value_net_count_;
    fstats.value_count_max_ = parsed.value_count_max_;
    fstats.value_index_fallback_count_ = parsed.value_index_fallback_count_;
    fstats.cap_count_ = parsed.cap_count_;
    fstats.coupling_cap_count_ = parsed.coupling_cap_count_;
    fstats.res_count_ = parsed.res_count_;

    // 绑定定位（一次性）。定位失败 = 入口校验后环境漂移的防御场景：按
    // 块失败兜底（failed_chunk_count 计 1）、网全部不处理、空分片照常
    // 产出（worker 任务第三态禁令——不 raise）
    bool bind_ok = true;
    const CMVector<uint32_t> block_nodes =
        resolve_binding_nodes(ctx, binding, bind_ok);
    if (!bind_ok) {
        ++fstats.failed_chunk_count_;
        slice.stats_.files_.push_back(std::move(fstats));
        return slice;
    }

    // strip_prefix 段表（前缀先按文件头分隔符归一——与归一后的名字段
    // 序对齐；前缀为空 = 不剥离）
    CMVector<CMString> prefix_segs;
    if (!binding.strip_prefix.empty()) {
        prefix_segs = split_segments(
            normalize_hier_name(binding.strip_prefix, header.divider_), '/');
    }

    ConvertEnv env;
    env.ctx = &ctx;
    env.divider = header.divider_;
    env.prefix_segs = prefix_segs.empty() ? nullptr : &prefix_segs;
    env.block_nodes = block_nodes.empty() ? nullptr : &block_nodes;
    env.fstats = &fstats;
    env.slice = &slice;

    for (const SPNet& net : parsed.nets_) {
        // 名字管线（§2.2 归一 + strip_prefix + 三形态查找；查找容错：
        // 原名未命中且含反斜杠 → 去转义回退再查一次，仍未命中才算未匹
        // 配——回退收敛在 resolve_once 的 forgiving 重入）
        CMVector<CMNetId> ids;
        NameOutcome outcome = resolve_once(env, net.name_, false, ids);
        if (outcome == NameOutcome::kNameMiss && has_escape(net.name_)) {
            outcome = resolve_once(env, net.name_, true, ids);
        }
        if (outcome != NameOutcome::kHit) {
            if (outcome == NameOutcome::kNameMiss) {
                ++fstats.net_name_miss_count_;   // SPMG::0001
            }
            continue;   // kStripMiss 已在管线内计数
        }
        // 逐候选网 id：pg 判定（跳过 + 计数）→ 分区路由。至少一个 id 路
        // 由到至少一个分区 = 命中（BLOCK_CELL 多实例化逐 id 判 pg）
        bool hit = false;
        const DSPgNetSet* pg = ctx.pg_nets();
        for (const CMNetId id : ids) {
            if (pg != nullptr && pg->is_pg(id)) {
                ++fstats.pg_net_skip_count_;   // SPMG::0003（§2.2 防御口径）
                continue;
            }
            if (route_net(env, id, net)) {
                hit = true;
            }
        }
        if (hit) {
            ++fstats.hit_count_;
        }
    }

    // 条目按 net id 升序（确定性——排序键为强类型 id，不依赖无序容器
    // 迭代序）
    for (auto& entry : slice.partitions_) {
        std::sort(entry.second.entries_.begin(), entry.second.entries_.end(),
                  [](const SPNetEntry& a, const SPNetEntry& b) {
                      return a.net_id_ < b.net_id_;
                  });
    }
    slice.stats_.files_.push_back(std::move(fstats));
    return slice;
}

// ── T3 每分区合并 ───────────────────────────────────────────────────

const SPNetLoad* SPPartitionNetLoad::find_net(CMNetId net_id) const {
    const auto it = std::lower_bound(
        items_.begin(), items_.end(), net_id,
        [](const SPNetLoad& e, CMNetId id) { return e.net_id_ < id; });
    if (it != items_.end() && it->net_id_ == net_id) {
        return &*it;
    }
    return nullptr;
}

SPPartitionNetLoad sp_merge_partition(
    const SPDesignContext& ctx, const CMVector<const SPEntrySlice*>& slices,
    CMPartitionId pid, SPMergeStats& stats_out) {
    SPMergeStats& st = stats_out;
    st = SPMergeStats{};   // 出参清零（调用方重复使用同一统计对象时安全）

    SPPartitionNetLoad out;
    out.part_id_ = pid;
    CMUnorderedSet<uint32_t> overflow_files;   // 位图溢出的文件集（本区去重）

    // 按到达序归组（net id → 累计 + 已贡献作用域集）。到达序 = 调用方
    // 排列的「文件序 × 块序」——「同作用域保留首份」依此序（裁定 8）。
    // 作用域集 = 该网全部已登记贡献的作用域（任意到达序下重合检测都正
    // 确；网的贡献作用域数不超过文件数，小向量线性查即可）
    struct Acc {
        double total = 0.0;
        double ground = 0.0;
        double coupling = 0.0;
        uint64_t mask = 0;
        uint8_t flags = 0;
        CMVector<uint32_t> scopes;   // 已贡献作用域集（线性查）
    };
    CMUnorderedMap<CMNetId, Acc> acc;

    for (const SPEntrySlice* slice : slices) {
        const auto it = slice->partitions_.find(pid);
        if (it == slice->partitions_.end()) {
            continue;
        }
        // 贡献来源位下标 = 文件序（file_index ≥ 64 位图不置位，按
        // file_index 去重计溢出文件数）
        uint64_t bit = 0;
        if (slice->file_index_ < 64) {
            bit = uint64_t{1} << slice->file_index_;
        } else if (!overflow_files.contains(slice->file_index_)) {
            overflow_files.insert(slice->file_index_);
        }
        for (const SPNetEntry& e : it->second.entries_) {
            auto [fit, inserted] = acc.emplace(e.net_id_, Acc{});
            if (inserted) {
                fit->second.total = e.total_cap_reported_ff_;
                fit->second.ground = e.ground_cap_ff_;
                fit->second.coupling = e.coupling_cap_ff_;
                fit->second.mask = bit;
                fit->second.flags = e.flags_;
                fit->second.scopes.push_back(slice->scope_index_);
                continue;
            }
            if (std::find(fit->second.scopes.begin(), fit->second.scopes.end(),
                          slice->scope_index_) != fit->second.scopes.end()) {
                // 同作用域重合（同一网被同作用域的多个文件描述）：保留
                // 首份，其余丢弃（裁定 8，SPMG::0007）
                ++st.same_scope_overlap_count_;
                continue;
            }
            // 不同作用域的贡献：三值相加、来源位图按位或、缩减形态标记
            // = 全部贡献都是缩减形态时才置位。合并数值的确定性由调用方
            // 固定的「文件序 × 块序」到达序保证（浮点加法非严格结合律，
            // 不依赖顺序无关性）
            fit->second.total += e.total_cap_reported_ff_;
            fit->second.ground += e.ground_cap_ff_;
            fit->second.coupling += e.coupling_cap_ff_;
            fit->second.mask |= bit;
            fit->second.flags &= e.flags_;
            fit->second.scopes.push_back(slice->scope_index_);
            ++st.cross_scope_sum_count_;
        }
    }
    st.source_mask_overflow_count_ = overflow_files.size();

    // ── 电气网分组合并（2026-11-24 用户裁定：flatten 后经层次端口逻辑
    //    相连的各段网是同一张电气网）────────────────────────────────
    // 全局逐网编号负载表：从全部分片的**全部条目**（全部分区——电气网
    // 合计要包含落在别的分区的段）按同一规则合并：同作用域保留首份（同
    // 作用域的跨分区复制是同一贡献的分区副本，保留首份无损；同作用域
    // 多文件描述同一网照裁定 8 保留首份），跨作用域相加。不产生计数
    // ——same_scope/cross_scope 的计数语义只属于本分区合并（上面的第
    // 一遍）。分区内遍历按分区 id 升序（slice->partitions_ 是无序容器
    // ——排序保证全局表相加顺序与分片序 × 分区序唯一确定，同网的跨作
    // 用域相加顺序不依赖无序容器迭代序）
    CMUnorderedMap<CMNetId, Acc> global_acc;
    for (const SPEntrySlice* slice : slices) {
        uint64_t bit = 0;
        if (slice->file_index_ < 64) {
            bit = uint64_t{1} << slice->file_index_;
        }
        CMVector<uint32_t> pids;
        pids.reserve(slice->partitions_.size());
        for (const auto& [slice_pid, part] : slice->partitions_) {
            pids.push_back(slice_pid.value());
        }
        std::sort(pids.begin(), pids.end());
        for (const uint32_t pid_value : pids) {
            const SPPartitionSlice& part =
                slice->partitions_.at(CMPartitionId{pid_value});
            for (const SPNetEntry& e : part.entries_) {
                auto [fit, inserted] = global_acc.emplace(e.net_id_, Acc{});
                if (inserted) {
                    fit->second.total = e.total_cap_reported_ff_;
                    fit->second.ground = e.ground_cap_ff_;
                    fit->second.coupling = e.coupling_cap_ff_;
                    fit->second.mask = bit;
                    fit->second.flags = e.flags_;
                    fit->second.scopes.push_back(slice->scope_index_);
                    continue;
                }
                if (std::find(fit->second.scopes.begin(),
                              fit->second.scopes.end(),
                              slice->scope_index_) !=
                    fit->second.scopes.end()) {
                    continue;   // 同作用域重复——保留首份（无损/裁定 8）
                }
                fit->second.total += e.total_cap_reported_ff_;
                fit->second.ground += e.ground_cap_ff_;
                fit->second.coupling += e.coupling_cap_ff_;
                fit->second.mask |= bit;
                fit->second.flags &= e.flags_;
                fit->second.scopes.push_back(slice->scope_index_);
            }
        }
    }

    // 并查集组员数（组 = 上下文注入的 DSNetUnion；未注入/网不在表/单
    // 成员悬空类 → 1——平展常态）
    const DSNetUnion* union_ptr = ctx.net_union();
    auto member_count = [union_ptr](CMNetId root) -> size_t {
        if (union_ptr == nullptr) {
            return 1;
        }
        const auto m = union_ptr->members(root).lock();
        return m != nullptr ? m->size() : 1;
    };

    // 按组求和：root → 电气网合计（三值相加、位图或、缩减标记与——首
    // 条建档）。任一被描述网的组根的合计必然在表（组求和遍历全局表全
    // 部条目）。遍历按网 id 升序（global_acc 是无序容器——排序保证同
    // 组多成员的相加顺序确定，不依赖无序容器迭代序）
    CMVector<CMNetId> global_ids;
    global_ids.reserve(global_acc.size());
    for (const auto& [net_id, a] : global_acc) {
        global_ids.push_back(net_id);
    }
    std::sort(global_ids.begin(), global_ids.end());
    CMUnorderedMap<CMNetId, Acc> group_sum;
    for (const CMNetId net_id : global_ids) {
        const Acc& a = global_acc.at(net_id);
        const CMNetId root =
            union_ptr != nullptr ? union_ptr->find(net_id) : net_id;
        auto [fit, inserted] = group_sum.emplace(root, a);
        if (inserted) {
            fit->second.scopes.clear();
            continue;
        }
        fit->second.total += a.total;
        fit->second.ground += a.ground;
        fit->second.coupling += a.coupling;
        fit->second.mask |= a.mask;
        fit->second.flags &= a.flags;
    }

    // 提取 + 按 net id 升序（确定性）+ 行首值与明细合计一致性观测
    //（SPMG::0013，只观测不改数值——对合并后非缩减形态的网；差额 =
    // |total − (ground + coupling)|、基准 = max(total, ground + coupling)，
    // 超过 max(1e-3 fF, 1e-4 × 基准) 计一次并更新最大相对偏差）
    // 条目落点（两步）：① 本区有原始条目的网编号——数值 = 电气网合计
    //（多成员组取组求和表——下游按手里任一段的网编号查到的都是整张电
    // 气网的负载）；单成员组不进组求和、取本区合并值——数值与分层合
    // 并逐位一致（浮点相加顺序不重排）。② 组内未被描述的成员网
    //（2026-11-24 评审修正）：凡本区网表（分区 NETS 对象）里有的信号
    // 网，只要它所在的电气网有负载（组求和表命中），就补一条电气网合
    // 计条目——来源位图与缩减标记同组一致；补全条目那一步不依赖并查
    // 集——平展设计里布线几何落在本区网表、连接位实例全在别的分区的
    // 网在本区也会多出这条补全条目。信号网对象是全量补全副本（每分区
    // 含全部信号网），每区自足；电源地网在 NETS_PG 对象、不在本表，
    // 天然排除。本区网表缺失时不补（无网编号可查——同「NETS 对象缺
    // 失」既有口径）
    auto observe_mismatch = [&st](const SPNetLoad& e) {
        if (e.flags_ & kSpFlagReduced) {
            return;
        }
        const double detail = e.ground_cap_ff_ + e.coupling_cap_ff_;
        const double diff = std::fabs(e.total_cap_reported_ff_ - detail);
        const double base = std::max(e.total_cap_reported_ff_, detail);
        if (diff > std::max(1e-3, 1e-4 * base)) {
            ++st.cap_mismatch_count_;
            const double rel = base > 0.0 ? diff / base : 0.0;
            st.cap_mismatch_max_rel_ = std::max(st.cap_mismatch_max_rel_, rel);
        }
    };
    out.items_.reserve(acc.size());
    for (const auto& [net_id, a] : acc) {
        const CMNetId root =
            union_ptr != nullptr ? union_ptr->find(net_id) : net_id;
        const bool multi = member_count(root) >= 2;
        const Acc& src = multi ? group_sum.at(root) : a;
        SPNetLoad e;
        e.net_id_ = net_id;
        e.total_cap_reported_ff_ = src.total;
        e.ground_cap_ff_ = src.ground;
        e.coupling_cap_ff_ = src.coupling;
        e.source_mask_ = src.mask;
        e.flags_ = src.flags;
        observe_mismatch(e);
        out.items_.push_back(std::move(e));
    }
    const auto part_nets_it = ctx.all_partition_nets().find(pid.value());
    if (part_nets_it != ctx.all_partition_nets().end()) {
        CMUnorderedSet<CMNetId> present;
        present.reserve(out.items_.size());
        for (const SPNetLoad& e : out.items_) {
            present.insert(e.net_id_);
        }
        for (const auto& [net_id, net] : part_nets_it->second->nets_) {
            if (present.contains(net_id)) {
                continue;   // ① 已有原始条目
            }
            const CMNetId root =
                union_ptr != nullptr ? union_ptr->find(net_id) : net_id;
            const auto gs = group_sum.find(root);
            if (gs == group_sum.end()) {
                continue;   // 所在电气网无负载（没有任何成员被描述）
            }
            SPNetLoad e;
            e.net_id_ = net_id;
            e.total_cap_reported_ff_ = gs->second.total;
            e.ground_cap_ff_ = gs->second.ground;
            e.coupling_cap_ff_ = gs->second.coupling;
            e.source_mask_ = gs->second.mask;
            e.flags_ = gs->second.flags;
            observe_mismatch(e);
            out.items_.push_back(std::move(e));
        }
    }
    std::sort(out.items_.begin(), out.items_.end(),
              [](const SPNetLoad& x, const SPNetLoad& y) {
                  return x.net_id_ < y.net_id_;
              });

    // 覆盖率统计（裁定 10；分母口径见 sp_partition.h 文件头注释——
    // 2026-11-24 实施期修正：材料 = 本区实例连接对象 INST_CONNECTIONS
    // + 已布线信号网 id 集合〔上下文全部分区 NETS 对象的网 id 并集〕，
    // 不是本区 NETS——NETS 按布线几何归属分区而寄生条目按连接位实例
    // 归属分区，两者可以不同）。本区实例连接对象缺失时各项记 0 直接
    // 返回（不建并集），不报错（同「NETS 对象缺失」的既有口径）。
    // 覆盖判定按电气网口径（2026-11-24 用户裁定）：一张网「已覆盖」=
    // 它所在的电气网有负载——组求和表命中即已覆盖（组任一成员被描述
    // 即入表；不受「本区条目只含本区成员编号」限制）
    const auto conns_it = ctx.all_inst_connections().find(pid.value());
    if (conns_it != ctx.all_inst_connections().end()) {
        CMUnorderedSet<CMNetId> routed_nets;
        for (const auto& nets_entry : ctx.all_partition_nets()) {
            for (const auto& [net_id, net] : nets_entry.second->nets_) {
                routed_nets.insert(net_id);
            }
        }
        const DSPartInstConnections& conns = *conns_it->second;
        // 网分母：本区实例连接里出现的、属于已布线信号网集合的不同网数。
        // 键 0 = root 设计级端口连接——root 非实体实例，端口外的片外负
        // 载也不在 SPEF 里，整键排除（既不进网分母也不进实例集合）。
        // 实例 → 所连已布线信号网；同实例同网多条连接不去重（覆盖/零
        // 电容判定幂等）
        CMUnorderedSet<CMNetId> part_routed;
        CMUnorderedMap<CMInstanceId, CMVector<CMNetId>> inst_nets;
        for (const auto& [inst, items] : conns.items_) {
            if (inst == CMInstanceId{0}) {
                continue;   // 键 0 = root 设计级端口连接——整键排除
            }
            for (const DSPartConnection& conn : items) {
                if (!routed_nets.contains(conn.net_global_id_)) {
                    continue;   // pg 网等不在信号网 NETS 里——自然排除
                }
                part_routed.insert(conn.net_global_id_);
                inst_nets[inst].push_back(conn.net_global_id_);
            }
        }
        st.routed_signal_net_count_ = part_routed.size();
        // 组负载点查（网 → 所在电气网的组求和条目；未命中 = 电气网无
        // 负载——没有任何成员被描述）
        auto group_load_of = [&group_sum,
                              union_ptr](CMNetId net_id) -> const Acc* {
            const CMNetId root =
                union_ptr != nullptr ? union_ptr->find(net_id) : net_id;
            const auto it = group_sum.find(root);
            return it == group_sum.end() ? nullptr : &it->second;
        };
        for (const CMNetId net_id : part_routed) {
            if (group_load_of(net_id) != nullptr) {
                ++st.covered_net_count_;
            }
        }
        st.uncovered_net_count_ =
            st.routed_signal_net_count_ - st.covered_net_count_;
        for (const auto& [inst, ids] : inst_nets) {
            bool all_covered = true;
            bool all_zero = true;
            for (const CMNetId id : ids) {
                const Acc* g = group_load_of(id);
                if (g == nullptr) {
                    all_covered = false;
                    all_zero = false;
                    continue;
                }
                // 零电容口径（对电气网合计）：非缩减 = 三值全 0；缩减
                // 形态只看行首值（两个明细合计值无效）
                const bool zero = (g->flags & kSpFlagReduced) != 0
                                      ? g->total == 0.0
                                      : g->total == 0.0 &&
                                            g->ground == 0.0 &&
                                            g->coupling == 0.0;
                if (!zero) {
                    all_zero = false;
                }
            }
            if (!all_covered) {
                ++st.uncovered_instance_count_;
            } else if (all_zero) {
                ++st.zero_cap_instance_count_;
            }
        }
    }
    return out;
}

// ── T4 汇总 ─────────────────────────────────────────────────────────

SPSummary sp_merge_summary(const CMVector<const SPStatsDelta*>& deltas,
                           uint64_t invalid_binding_count,
                           const CMVector<SPMergeStats>& merge_stats,
                           uint64_t electrical_net_count) {
    SPSummary s;
    // 绑定目标未命中被跳过的文件数（flow 侧判定，原样落字段）
    s.invalid_binding_count_ = invalid_binding_count;
    // 多成员电气网数（设计级：flow 根任务读并查集对象的组数，原样落字
    // 段——不经合并统计聚合，2026-11-24 评审修正）
    s.electrical_net_count_ = electrical_net_count;
    // 内部单位基准声明（裁定 7：文件数值已按头档位换算成 fF / ohm）
    s.internal_cap_unit_ff_ = 1.0;
    s.internal_res_unit_ohm_ = 1.0;

    // 逐文件按文件名归并（同一文件的多块分片：计数累加、value_count_max
    // 取最大、首条建档——来源文件与单位档位块间一致）
    CMUnorderedMap<CMString, size_t> by_file;
    for (const SPStatsDelta* d : deltas) {
        for (const SPFileStats& f : d->files_) {
            auto it = by_file.find(f.source_file_);
            if (it == by_file.end()) {
                by_file.emplace(f.source_file_, s.files_.size());
                s.files_.push_back(f);
                continue;
            }
            SPFileStats& acc = s.files_[it->second];
            acc.net_count_ += f.net_count_;
            acc.rnet_count_ += f.rnet_count_;
            acc.pnet_skip_count_ += f.pnet_skip_count_;
            acc.broken_net_count_ += f.broken_net_count_;
            acc.name_map_miss_count_ += f.name_map_miss_count_;
            acc.dropped_record_count_ += f.dropped_record_count_;
            acc.multi_value_net_count_ += f.multi_value_net_count_;
            acc.value_count_max_ =
                std::max(acc.value_count_max_, f.value_count_max_);
            acc.value_index_fallback_count_ += f.value_index_fallback_count_;
            acc.cap_count_ += f.cap_count_;
            acc.coupling_cap_count_ += f.coupling_cap_count_;
            acc.res_count_ += f.res_count_;
            acc.hit_count_ += f.hit_count_;
            acc.net_name_miss_count_ += f.net_name_miss_count_;
            acc.pg_net_skip_count_ += f.pg_net_skip_count_;
            acc.unrouted_net_count_ += f.unrouted_net_count_;
            acc.strip_miss_count_ += f.strip_miss_count_;
            acc.unplaced_instance_count_ += f.unplaced_instance_count_;
            acc.failed_chunk_count_ += f.failed_chunk_count_;
        }
    }
    // 缩减网数全局 = Σ 逐文件
    for (const SPFileStats& f : s.files_) {
        s.rnet_count_ += f.rnet_count_;
    }
    // 全局 = 各分区合并统计之和（最大相对偏差取最大）
    for (const SPMergeStats& m : merge_stats) {
        s.routed_signal_net_count_ += m.routed_signal_net_count_;
        s.covered_net_count_ += m.covered_net_count_;
        s.uncovered_net_count_ += m.uncovered_net_count_;
        s.uncovered_instance_count_ += m.uncovered_instance_count_;
        s.zero_cap_instance_count_ += m.zero_cap_instance_count_;
        s.cap_mismatch_count_ += m.cap_mismatch_count_;
        s.cap_mismatch_max_rel_ =
            std::max(s.cap_mismatch_max_rel_, m.cap_mismatch_max_rel_);
        s.same_scope_overlap_count_ += m.same_scope_overlap_count_;
        s.cross_scope_sum_count_ += m.cross_scope_sum_count_;
        s.source_mask_overflow_count_ += m.source_mask_overflow_count_;
    }
    return s;
}

}  // namespace fly
