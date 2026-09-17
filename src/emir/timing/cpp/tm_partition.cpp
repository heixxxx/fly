#include <emir/timing/cpp/tm_partition.h>
#include <emir/timing/cpp/tm_parser.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace fly {
namespace {

// 读文件字节区间 [start, end)（越界截断到文件尾；不可读返回 false——
// 入口校验已拦不可读，此处失败属运行期环境异常，调用方按块失败兜底）
bool read_byte_range(const CMString& path, uint64_t start, uint64_t end,
                     CMString& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const uint64_t file_size = static_cast<uint64_t>(in.tellg());
    if (start >= file_size) {
        out.clear();
        return true;  // 空区间（越界截断）
    }
    end = std::min(end, file_size);
    in.seekg(static_cast<std::streamoff>(start));
    CMString text(static_cast<size_t>(end - start), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    out = std::move(text);
    return true;
}

// 按分隔符拆段（strip_prefix 段匹配与名字维度拆段共用；段级精确匹配
// 口径——plan §7.4 防 u_dut 误配 u_dutx）
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

// 段序列以分隔符重组为 mapper 路径语法（design db 层级路径恒 '/'）
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

// strip_prefix 段级剥离（§7.4）：起始段序列与前缀段精确匹配则剥去；
// 未命中 / 剥后余空返回 false（调用方 TIMG::0010 计数跳过）
bool strip_prefix_segments(const CMString& name, char delim,
                           const CMVector<CMString>& prefix_segs,
                           CMString& out) {
    const CMVector<CMString> segs = split_segments(name, delim);
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

// 层级实例路径 → 树节点 id（2026-09-16 裁定 1 语义：路径不含设计名前缀
// ——自 root 内容段起；空串 = root。块实例定位专用——DSNameMapperT 的
// global id 反查会命中父块区间，不能用于块节点定位）。未命中 kNoNode。
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

// 块内 local 实例 id → global（⑧⑨ 区间换算：local 0 = 块实例自身占位
// → self_global_id；hasher 哨兵/越界 → 默认哨兵）。hasher 底座裸值域
// 豁免边界：入参出参均裸 uint64，调用点显式互转。
CMInstanceId local_to_global_instance(const DSHierNode& node,
                                      uint64_t local) {
    if (local == 0) {
        return node.get_self_global_id();
    }
    if (local >= node.get_instance_count()) {
        return CMInstanceId{};
    }
    return CMInstanceId{node.get_instance_start().value() + local};
}

// 条目值搬运：TMNameTiming → TMPinTiming（值/存在位/常量/多源标记原样）
TMPinTiming make_pin_timing(const TMNameTiming& e, CMPinId pin_id) {
    TMPinTiming p;
    p.pin_id_ = pin_id;
    p.rise_arrival_ = e.rise_arrival_;
    p.fall_arrival_ = e.fall_arrival_;
    p.rise_slew_ = e.rise_slew_;
    p.fall_slew_ = e.fall_slew_;
    if (e.is_rise_arrival()) {
        p.set_rise_arrival();
    }
    if (e.is_fall_arrival()) {
        p.set_fall_arrival();
    }
    if (e.is_rise_slew()) {
        p.set_rise_slew();
    }
    if (e.is_fall_slew()) {
        p.set_fall_slew();
    }
    if (e.is_constant()) {
        p.set_constant();
    }
    if (e.is_multi_source()) {
        p.set_multi_source();
    }
    return p;
}

// —— 换算主体（T2；块绑定节点集空 = 纯路径形态）——

// 换算上下文聚合（避免逐条目长参数）
struct EntryConvertEnv {
    const TMDesignContext* ctx;
    char delim;                                  // 文件层级分隔符
    const CMVector<CMString>* prefix_segs;       // strip_prefix 段表
    const CMVector<uint32_t>* block_nodes;       // 块绑定节点集（可空）
    TMFileStats* file_stats;                     // 逐文件计数
    TMStatsDelta* stats;                         // 全局计数
    TMEntrySlice* slice;                         // 分片产出
};

// 分片累积入口（同块内同 (实例, pin) 重复 = 解析兜底形态防御，保留首份；
// 跨块/跨文件重复由 T3 merge_slice 计冲突 TIMG::0006）
void accumulate_pin(EntryConvertEnv& env, CMPartitionId pid,
                    CMInstanceId inst_id, TMPinTiming&& pin,
                    CMClockId clock_id) {
    TMPartitionTiming& part = env.slice->partitions_[pid];
    part.part_id_ = pid;
    TMInstanceTiming& inst = part.items_[inst_id];
    if (!inst.clock_id_.is_valid() && clock_id.is_valid()) {
        inst.clock_id_ = clock_id;
    }
    if (inst.find_pin(pin.pin_id_) != nullptr) {
        return;  // 块内重复防御：保留首份
    }
    inst.pins_.push_back(std::move(pin));
}

// 块内局部下降：从节点起按段降到叶块，返回叶块 hasher 查末段的 local id
//（未命中 = hasher 哨兵）。block 节点自身 = 单段场景的叶。
uint64_t descend_local(const EntryConvertEnv& env, const DSHierNode* leaf,
                       const CMVector<CMString>& segs, bool instance_kind,
                       uint64_t invalid_local) {
    const DSHierTree& tree = env.ctx->design().get_hier_tree();
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        const uint32_t child =
            tree.find_child_by_instance_name(leaf->get_id(), segs[i]);
        if (child == DSHierTree::kNoNode) {
            return invalid_local;
        }
        leaf = &tree.node(child);
    }
    if (instance_kind) {
        const auto hasher = env.ctx->inst_hasher_of(leaf->get_block_cell_id());
        if (hasher == nullptr) {
            return invalid_local;
        }
        return hasher->get_id(segs.back());
    }
    const auto hasher = env.ctx->net_hasher_of(leaf->get_block_cell_id());
    if (hasher == nullptr) {
        return invalid_local;
    }
    return hasher->get_id(segs.back());
}

// 引脚条目换算（TWF 名 = 实例层级路径/引脚名；纯路径 → 全局 mapper、
// 块绑定 → 块内局部下降 + 偏移）。命中返回 true。
bool convert_pin_entry(EntryConvertEnv& env, const CMString& name,
                       const TMNameTiming& e) {
    const size_t last = name.find_last_of(env.delim);
    if (last == CMString::npos || last + 1 == name.size()) {
        ++env.file_stats->skipped_pin_count_;  // 形态错（无引脚段）
        return false;
    }
    const CMString pin_name(name.data() + last + 1, name.size() - last - 1);
    const CMString inst_path = join_segments(
        split_segments(CMString(name.data(), last), env.delim));
    // pin 全局名字空间单哈希查（2026-09-16 裁定 3：键 = 裸 pin 名——
    // plan §7 表「无 cell 组合键」）
    const CMPinId pin_id{env.ctx->design().pin_names_.get_id(pin_name)};
    if (!pin_id.is_valid()) {
        ++env.file_stats->skipped_pin_count_;  // TIMG::0002
        return false;
    }

    if (env.block_nodes == nullptr || env.block_nodes->empty()) {
        // 纯路径：全局 instance mapper（名换算路径 §7 表首行口径）
        const CMInstanceId inst_id{
            env.ctx->inst_mapper().get_global_id(inst_path)};
        if (!inst_id.is_valid()) {
            ++env.file_stats->skipped_instance_count_;  // TIMG::0001
            return false;
        }
        const CMPartitionId pid = env.ctx->locate_instance_partition(inst_id);
        if (!pid.is_valid()) {
            ++env.file_stats->unplaced_instance_count_;  // TIMG::0008
            return false;
        }
        accumulate_pin(env, pid, inst_id, make_pin_timing(e, pin_id),
                       e.clock_id_);
        return true;
    }
    // 块绑定（§7.1 偏移路径 / §7.2 定义级建库期复制——对每个实例化节点
    // 独立换算与分区路由）
    bool hit = false;
    for (uint32_t node_id : *env.block_nodes) {
        const DSHierTree& tree = env.ctx->design().get_hier_tree();
        const DSHierNode* leaf = &tree.node(node_id);
        const uint64_t local = descend_local(
            env, leaf, split_segments(inst_path, env.delim),
            /*instance_kind=*/true, DSInstanceNameHasher::kInvalidId);
        if (local == DSInstanceNameHasher::kInvalidId) {
            continue;
        }
        const CMInstanceId inst_id = local_to_global_instance(*leaf, local);
        if (!inst_id.is_valid()) {
            continue;
        }
        const CMPartitionId pid = env.ctx->locate_instance_partition(inst_id);
        if (!pid.is_valid()) {
            ++env.file_stats->unplaced_instance_count_;  // TIMG::0008
            continue;
        }
        accumulate_pin(env, pid, inst_id, make_pin_timing(e, pin_id),
                       e.clock_id_);
        hit = true;
    }
    if (!hit) {
        ++env.file_stats->skipped_instance_count_;  // TIMG::0001
    }
    return hit;
}

// 网条目 driver 定位与分片累积（pg 已过；多驱动网挂全部 driver 位条目
// ——裁定 4 不取首个；无 driver/悬空 → TIMG::0003 计数返回 false）。
// 分区定位 = 遍历快照分区 NETS 对象取首个含此网者（信号网 NETS 全量
// 补全——任一副本都有该网完整连接表；plan §7 不经 NET id map 路由——
// 该映射是 design db debug 专用辅助索引且无几何网不入段）
bool attach_net_drivers(EntryConvertEnv& env, CMNetId net_id,
                        const TMNameTiming& e) {
    CMSharedPtr<const DSNet> net;
    for (const auto& entry : env.ctx->all_partition_nets()) {
        auto candidate = entry.second->net_of(net_id).lock();
        if (candidate != nullptr) {
            net = candidate;
            break;
        }
    }
    if (net == nullptr) {
        ++env.file_stats->dangling_net_count_;  // TIMG::0003（悬浮网）
        return false;
    }
    CMVector<const DSNetConnEntry*> drivers;
    for (const DSNetConnEntry& conn : net->connections_) {
        if (conn.is_driver()) {
            drivers.push_back(&conn);
        }
    }
    if (drivers.empty()) {
        ++env.file_stats->dangling_net_count_;  // 0003（无驱动/仅端口）
        return false;
    }
    if (drivers.size() > 1) {
        ++env.stats->multi_driver_net_count_;  // 裁定 4
    }
    for (const DSNetConnEntry* d : drivers) {
        const CMPartitionId dpid =
            env.ctx->locate_instance_partition(d->inst_id_);
        if (!dpid.is_valid()) {
            // 防御：design db driver 恒已放置——环境不一致时计数
            ++env.file_stats->unplaced_instance_count_;
            continue;
        }
        accumulate_pin(env, dpid, d->inst_id_, make_pin_timing(e, d->pin_id_),
                       e.clock_id_);
    }
    return true;
}

// 换算结果三态（kNameMiss 延迟计数——CONSTANT 条目的引脚形态兜底成功
// 时不得残留网侧 miss 计数，调用方统一计数）
enum class ConvertOutcome { kNameMiss, kHit, kSkip };

// 网条目换算（NET 缺省风味 + CONSTANT 网形态）：网名 → global id →
// pg 判定 → driver 定位。
ConvertOutcome convert_net_entry(EntryConvertEnv& env, const CMString& name,
                                 const TMNameTiming& e) {
    const DSPgNetSet* pg = env.ctx->pg_nets();

    if (env.block_nodes == nullptr || env.block_nodes->empty()) {
        // 纯路径：全局 net mapper
        const CMNetId net_id{env.ctx->net_mapper().get_global_id(name)};
        if (!net_id.is_valid()) {
            return ConvertOutcome::kNameMiss;  // 0001 家族（网名，调用方计）
        }
        if (pg != nullptr && pg->is_pg(net_id)) {
            ++env.stats->pg_net_skip_count_;  // §7.5
            return ConvertOutcome::kSkip;
        }
        return attach_net_drivers(env, net_id, e)
                   ? ConvertOutcome::kHit
                   : ConvertOutcome::kSkip;  // driver 定位失败已在内部计数
    }

    // 块绑定：块端口条目（§7.1 归属键 = 块实例自身全局 id）或块内真实网。
    // attach_attempted = 网名已命中但 driver 定位失败（kSkip——dangling
    // 已在内部计数）；与「名字未命中」（kNameMiss）区分，防双计数
    bool hit = false;
    bool attach_attempted = false;
    for (uint32_t node_id : *env.block_nodes) {
        const DSHierTree& tree = env.ctx->design().get_hier_tree();
        const DSHierNode* leaf = &tree.node(node_id);
        // 端口判定：条目名 = 块端口名（非块内网）→ 归属块实例自身
        const auto net_hasher = env.ctx->net_hasher_of(
            leaf->get_block_cell_id());
        const bool is_internal_net =
            net_hasher != nullptr &&
            net_hasher->get_id(name) != DSNetNameHasher::kInvalidId;
        if (!is_internal_net) {
            const DSCell& cell =
                env.ctx->design().get_cell(leaf->get_block_cell_id());
            CMPinId port_pin_id;
            bool found_port = false;
            bool is_pg_pin = false;
            for (uint32_t i = 0; i < cell.pin_count(); ++i) {
                const DSPin& p = cell.pin_at(i);
                if (p.is_port() &&
                    env.ctx->design().pin_name_of(p.get_pin_id()) == name) {
                    port_pin_id = p.get_pin_id();
                    const DSPinType t = p.get_type();
                    is_pg_pin =
                        t == DSPinType::POWER || t == DSPinType::GROUND;
                    found_port = true;
                    break;
                }
            }
            if (found_port) {
                if (is_pg_pin) {
                    ++env.stats->pg_net_skip_count_;  // §7.5 pg 端口
                    return ConvertOutcome::kSkip;  // 跳过形态（不计命中）
                }
                const CMPartitionId pid = env.ctx->locate_instance_partition(
                    leaf->get_self_global_id());
                if (!pid.is_valid()) {
                    ++env.file_stats->unplaced_instance_count_;  // 0008
                    continue;
                }
                accumulate_pin(env, pid, leaf->get_self_global_id(),
                               make_pin_timing(e, port_pin_id), e.clock_id_);
                hit = true;
                continue;
            }
        }
        // 块内真实网：局部下降 + net hasher → 偏移换算（net local 从 1
        // 起、global = net_start + local——2026-09-14 空洞位口径）
        const uint64_t local = descend_local(
            env, leaf, split_segments(name, env.delim),
            /*instance_kind=*/false, DSNetNameHasher::kInvalidId);
        if (local == DSNetNameHasher::kInvalidId || local == 0 ||
            local >= leaf->get_net_count()) {
            continue;  // 块内未命中（local 0 = 空洞位不登记名）
        }
        const CMNetId net_id{leaf->get_net_start().value() + local};
        if (pg != nullptr && pg->is_pg(net_id)) {
            ++env.stats->pg_net_skip_count_;  // §7.5
            return ConvertOutcome::kSkip;  // 跳过形态（不计命中）
        }
        attach_attempted = true;
        if (attach_net_drivers(env, net_id, e)) {
            hit = true;
        }
    }
    if (hit) {
        return ConvertOutcome::kHit;
    }
    return attach_attempted ? ConvertOutcome::kSkip : ConvertOutcome::kNameMiss;
}

}  // namespace

// ── 落库形态构建期接口 ───────────────────────────────────────────────

const TMPinTiming* TMInstanceTiming::find_pin(CMPinId pin_id) const {
    for (const TMPinTiming& p : pins_) {
        if (p.pin_id_ == pin_id) {
            return &p;
        }
    }
    return nullptr;
}

const TMClockTable::Entry* TMClockTable::find(const CMString& name) const {
    for (const Entry& e : clocks_) {
        if (e.name_ == name) {
            return &e;
        }
    }
    return nullptr;
}

// ── T1 切块扫描 ─────────────────────────────────────────────────────

TMFileChunkPlan tm_plan_file_chunks(const CMString& path,
                                    uint64_t chunk_size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("TWF: file not readable: " + path);
    }
    TMFileChunkPlan plan;
    plan.file_name_ = path;
    // 流式扫描：顶层子构造边界（(TIMING_WINDOWS 外皮 depth=1；子构造
    // 1↔2——起点 = 1→2 的 '('、终点 = 2→1 的 ')'；引号内不计深度，与
    // 解析器词法同口径：TWF 字符串无引号转义形态）。起点后识别构造关键
    // 字（跳空白取原子，限长截断视为未知构造）定位首个数据构造
    // CAUSED_BY：其之前的全部顶层构造 = 文件头段（HEADER + WAVEFORM，
    // 评审 P1-1 的每块公共前缀——保证同文件各块时钟表一致、块内时钟 id
    // 编号域统一）
    constexpr size_t kBufSize = 1u << 16;
    constexpr size_t kKeywordMax = 32;
    char buf[kBufSize];
    bool in_string = false;
    int depth = 0;
    uint64_t off = 0;
    uint64_t first_construct = UINT64_MAX;
    uint64_t data_start = UINT64_MAX;   // 首个 CAUSED_BY 构造起点
    uint64_t construct_start = 0;       // 当前顶层子构造起点
    bool kw_pending = false;            // 构造起点后等待/正在读关键字
    bool kw_over = false;               // 关键字超长（按未知构造处理）
    CMString keyword;
    CMVector<uint64_t> bounds;
    // 关键字读毕判定（空白/构造闭合终止时调用）
    const auto judge_keyword = [&]() {
        if (!kw_over && keyword == "CAUSED_BY" && data_start == UINT64_MAX) {
            data_start = construct_start;
        }
        kw_pending = false;
    };
    while (in) {
        in.read(buf, static_cast<std::streamsize>(kBufSize));
        const size_t got = static_cast<size_t>(in.gcount());
        for (size_t i = 0; i < got; ++i) {
            const char c = buf[i];
            if (in_string) {
                if (c == '"') {
                    in_string = false;
                }
            } else if (c == '"') {
                in_string = true;
                kw_pending = false;  // 构造名不会是引号形态（异形）
            } else if (c == '(') {
                ++depth;
                if (depth == 2) {
                    if (off < first_construct) {
                        first_construct = off;
                    }
                    construct_start = off;
                    kw_pending = true;
                    kw_over = false;
                    keyword.clear();
                } else {
                    kw_pending = false;  // 名字未完成即嵌套（异形）
                }
            } else if (c == ')') {
                if (depth == 2) {
                    bounds.push_back(off + 1);
                    if (kw_pending && !keyword.empty()) {
                        judge_keyword();  // "(CAUSED_BY)" 紧闭形态
                    }
                }
                --depth;
                kw_pending = false;
            } else if (kw_pending && depth == 2) {
                if (static_cast<unsigned char>(c) <= ' ') {
                    if (!keyword.empty()) {
                        judge_keyword();
                    }
                } else if (keyword.size() < kKeywordMax) {
                    keyword.push_back(c);
                } else {
                    kw_over = true;
                }
            }
            ++off;
        }
    }
    plan.file_size_ = off;
    if (first_construct == UINT64_MAX) {
        // 无顶层子构造（空 TIMING_WINDOWS）→ 单空块（0 条目语义，stats
        // 照常产出）
        plan.chunk_starts_.push_back(0);
        plan.chunk_ends_.push_back(0);
        return plan;
    }
    if (data_start != UINT64_MAX) {
        // 有数据构造：prefix = 首个 CAUSED_BY 之前的全部顶层构造（头段）
        plan.prefix_start_ = first_construct;
        plan.prefix_end_ = data_start;
        plan.chunk_starts_.push_back(data_start);
        uint64_t last = data_start;
        for (const uint64_t b : bounds) {
            if (b <= last) {
                continue;  // 头段构造终点
            }
            if (b - last >= chunk_size) {
                plan.chunk_ends_.push_back(b);      // 当前块终点 = 新块起点
                plan.chunk_starts_.push_back(b);
                last = b;
            }
        }
        // 末块终点 = 最后顶层子构造终点（文件尾 ')' 外皮不入块——T2 重包
        // 外皮语义）；流级破损（全部构造未闭合）时 bounds 空——末块终点
        // 回退 file_size，把剩余文本包进块让解析器判破损（failed_chunk
        // 兜底计数，不静默吞掉破损）。max 防护：数据构造未闭合时
        // bounds.back() 为头段终点（< 块起点），不得倒挂块区间
        plan.chunk_ends_.push_back(
            std::max(bounds.empty() ? plan.file_size_ : bounds.back(),
                     plan.chunk_starts_.back()));
        return plan;
    }
    if (bounds.empty()) {
        // 有起点无闭合构造（流级破损）：prefix 置空，块包全部剩余文本交
        // 解析器判破损兜底（不静默吞掉）
        plan.chunk_starts_.push_back(first_construct);
        plan.chunk_ends_.push_back(plan.file_size_);
        return plan;
    }
    // 无数据构造（纯文件头）：prefix = 全部闭合构造（每块拼 prefix 解析
    // 仍产出时钟表——T4 合并输入保持有效），单空块承接「无条目」语义
    plan.prefix_start_ = first_construct;
    plan.prefix_end_ = bounds.back();
    plan.chunk_starts_.push_back(bounds.back());
    plan.chunk_ends_.push_back(bounds.back());
    return plan;
}

// ── T2 换算上下文 ───────────────────────────────────────────────────

void TMDesignContext::set_design(CMSharedPtr<DSDesign> design) {
    design_ = std::move(design);
    rebuild_mappers();
}

void TMDesignContext::rebuild_mappers() {
    if (!design_) {
        return;
    }
    // 两维度 mapper（持 design 内层级树观察指针——ds_make_name_mapper
    // 先例形态；design_ 共享计数保生命周期）。构造即建分派索引。
    inst_mapper_ = DSInstanceNameMapper(&design_->get_hier_tree(),
                                        DSNameMapperKind::INSTANCE);
    net_mapper_ = DSNetNameMapper(&design_->get_hier_tree(),
                                  DSNameMapperKind::NET);
    // hasher 注入关系重挂（mapper 为运行时构件，重组装后由此恢复）
    for (const auto& entry : inst_hashers_) {
        inst_mapper_.set_block_hasher(entry.first, entry.second);
    }
    for (const auto& entry : net_hashers_) {
        net_mapper_.set_block_hasher(entry.first, entry.second);
    }
}

void TMDesignContext::add_block_names(
    CMSharedPtr<const DSBlockNames> names) {
    if (!names || !design_) {
        return;
    }
    // hasher 底座裸值域豁免边界：cell id 查询/注入两侧显式互转
    const uint32_t cell_id = design_->cell_names_.get_id(names->block_name_);
    if (!DSCellNameHasher::is_valid_id(cell_id)) {
        return;  // block cell 未入全局表（防御跳过，同 ds_make_name_mapper）
    }
    // map 存非 const（序列化约束）、mapper 注入 const 化只读视图
    inst_hashers_[cell_id] = names->instance_names_;
    net_hashers_[cell_id] = names->net_names_;
    inst_mapper_.set_block_hasher(
        cell_id, CMSharedPtr<const DSInstanceNameHasher>(inst_hashers_[cell_id]));
    net_mapper_.set_block_hasher(
        cell_id, CMSharedPtr<const DSNetNameHasher>(net_hashers_[cell_id]));
}

void TMDesignContext::set_inst_id_map(
    CMSharedPtr<DSIdPartitionIndex> index) {
    inst_index_ = std::move(index);
}

void TMDesignContext::add_inst_segment(
    CMSharedPtr<DSIdPartitionSegment> segment) {
    if (segment) {
        inst_segments_[segment->get_id_start()] = std::move(segment);
    }
}

void TMDesignContext::set_pg_nets(CMSharedPtr<DSPgNetSet> pg_nets) {
    pg_nets_ = std::move(pg_nets);
}

void TMDesignContext::add_partition_nets(
    CMSharedPtr<DSPartitionNets> nets) {
    if (nets) {
        part_nets_[nets->part_id_.value()] = std::move(nets);
    }
}

CMPartitionId TMDesignContext::locate_instance_partition(
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
    const CMPartitionId pid = it->second->partition_of(inst_id.value());
    return pid.is_valid() ? pid : CMPartitionId{};
}

CMSharedPtr<DSPartitionNets> TMDesignContext::partition_nets(
    CMPartitionId pid) const {
    const auto it = part_nets_.find(pid.value());
    return it == part_nets_.end() ? CMSharedPtr<DSPartitionNets>{}
                                  : it->second;
}

CMSharedPtr<const DSInstanceNameHasher> TMDesignContext::inst_hasher_of(
    CMCellId block_cell_id) const {
    const auto it = inst_hashers_.find(block_cell_id.value());
    return it == inst_hashers_.end() ? CMSharedPtr<const DSInstanceNameHasher>{}
                                     : it->second;
}

CMSharedPtr<const DSNetNameHasher> TMDesignContext::net_hasher_of(
    CMCellId block_cell_id) const {
    const auto it = net_hashers_.find(block_cell_id.value());
    return it == net_hashers_.end() ? CMSharedPtr<const DSNetNameHasher>{}
                                    : it->second;
}

// ── T2 逐块换算 ─────────────────────────────────────────────────────

TMEntrySlice tm_convert_chunk(const TMDesignContext& ctx,
                              const CMString& path, uint64_t prefix_start,
                              uint64_t prefix_end, uint64_t chunk_start,
                              uint64_t chunk_end,
                              const TMFileBinding& binding,
                              uint32_t file_index) {
    TMEntrySlice slice;
    slice.stats_.file_index_ = file_index;
    TMFileStats file_stats;
    file_stats.source_file_ = path;

    // 读文件头段公共前缀（HEADER + WAVEFORM 段——评审 P1-1：拼在每块前
    // 使同文件各块时钟表一致、块内时钟 id 域统一）与块字节区间，重包外皮
    //（T1 已剥离 (TIMING_WINDOWS 外皮——块 = 顶层子构造序列，包装后即
    // 自包含合法 TWF）
    CMString prefix_text;
    CMString text;
    if (!read_byte_range(path, prefix_start, prefix_end, prefix_text) ||
        !read_byte_range(path, chunk_start, chunk_end, text)) {
        ++file_stats.failed_chunk_count_;  // 运行期不可读（环境异常兜底）
        slice.stats_.files_.push_back(std::move(file_stats));
        return slice;
    }
    TMTimingFile file;
    try {
        file = tm_parse_twf_text("(TIMING_WINDOWS\n" + prefix_text + "\n" +
                                     text + "\n)",
                                 path);
    } catch (const std::exception&) {
        // 单块语法破损兜底（plan §6 范式 (b)）：跳过 + 计数，空分片照常
        // 产出（依赖链保持满足）
        ++file_stats.failed_chunk_count_;
        slice.stats_.files_.push_back(std::move(file_stats));
        return slice;
    }
    file_stats.time_scale_sec_ = file.time_scale_sec_;

    // 绑定定位（一次性；BLOCK_INST = 块实例节点、BLOCK_CELL = block
    // cell 全部实例化节点。定位失败 = 入口校验后环境漂移的防御场景，按
    // 块失败兜底计数——不 raise（worker 任务第三态禁令））
    CMVector<uint32_t> block_nodes;
    if (binding.kind == TMFileBindingKind::BLOCK_INST) {
        const uint32_t node_id = find_node_by_path(
            ctx.design().get_hier_tree(), binding.block_inst);
        if (node_id == DSHierTree::kNoNode) {
            ++file_stats.failed_chunk_count_;
            slice.stats_.files_.push_back(std::move(file_stats));
            return slice;
        }
        block_nodes.push_back(node_id);
    } else if (binding.kind == TMFileBindingKind::BLOCK_CELL) {
        const uint32_t cell_id =
            ctx.design().cell_names_.get_id(binding.block_cell);
        if (!DSCellNameHasher::is_valid_id(cell_id)) {
            ++file_stats.failed_chunk_count_;
            slice.stats_.files_.push_back(std::move(file_stats));
            return slice;
        }
        const DSHierTree& tree = ctx.design().get_hier_tree();
        for (uint32_t i = 0; i < tree.node_count(); ++i) {
            if (tree.node(i).get_block_cell_id().value() == cell_id) {
                block_nodes.push_back(i);
            }
        }
        if (block_nodes.empty()) {
            ++file_stats.failed_chunk_count_;
            slice.stats_.files_.push_back(std::move(file_stats));
            return slice;
        }
    }

    // strip_prefix 段表（§7.4；仅块绑定形态可附加——入口校验保证）
    CMVector<CMString> prefix_segs;
    if (!binding.strip_prefix.empty()) {
        prefix_segs = split_segments(binding.strip_prefix, file.hier_delim());
    }

    EntryConvertEnv env;
    env.ctx = &ctx;
    env.delim = file.hier_delim();
    env.prefix_segs = &prefix_segs;
    env.block_nodes = block_nodes.empty() ? nullptr : &block_nodes;
    env.file_stats = &file_stats;
    env.stats = &slice.stats_;
    env.slice = &slice;

    for (const TMNameTiming& e : file.entries_) {
        ++file_stats.entry_count_;
        // strip_prefix 段级剥离（未命中 / 剥后余空 → TIMG::0010 跳过）
        CMString name = e.name_;
        if (!prefix_segs.empty()) {
            if (!strip_prefix_segments(e.name_, env.delim, prefix_segs,
                                       name)) {
                ++file_stats.strip_miss_count_;  // TIMG::0010
                continue;
            }
        }
        ConvertOutcome outcome = ConvertOutcome::kNameMiss;
        if (e.is_pin_kind()) {
            outcome = convert_pin_entry(env, name, e) ? ConvertOutcome::kHit
                                                      : ConvertOutcome::kSkip;
        } else {
            outcome = convert_net_entry(env, name, e);
            if (outcome == ConvertOutcome::kNameMiss && e.is_constant() &&
                name.find(env.delim) != CMString::npos) {
                // 独立 CONSTANT 条目（无维度标记）的引脚形态兜底：网换
                // 算未命中且名含层级分隔符 → 按引脚条目重试（失败时引脚
                // 侧已按形态计数——实例/pin 未命中，不再回记网名 miss）
                outcome = convert_pin_entry(env, name, e)
                              ? ConvertOutcome::kHit
                              : ConvertOutcome::kSkip;
            }
        }
        if (outcome == ConvertOutcome::kNameMiss) {
            ++file_stats.net_name_miss_count_;  // 0001 家族（网名）
            continue;
        }
        if (outcome != ConvertOutcome::kHit) {
            continue;
        }
        ++file_stats.hit_count_;
        if (e.is_constant()) {
            ++env.stats->const_entry_count_;
        } else if (!e.is_rise_arrival() && !e.is_fall_arrival() &&
                   !e.is_rise_slew() && !e.is_fall_slew()) {
            ++env.stats->no_window_count_;  // NO_TW 覆盖观测
        }
    }
    // 弃收与 C/D 观测（自解析产物聚合；块序直和）+ 引用未登记时钟的条
    // 目分组数（评审 P1-1 补聚合——时钟归属丢失在 summary 可见）
    env.stats->dropped_source_res_count_ += file.dropped_source_res_count_;
    env.stats->dropped_slack_count_ += file.dropped_slack_count_;
    env.stats->missing_clock_count_ += file.missing_clock_count_;
    env.stats->cd_flag_c_count_ += file.cd_flag_c_count_;
    env.stats->cd_flag_d_count_ += file.cd_flag_d_count_;
    env.stats->clocks_ = file.clocks_;  // 时钟表合并任务输入（ns 已换算；
                                        // 头段公共前缀保证块间一致）

    slice.stats_.files_.push_back(std::move(file_stats));
    return slice;
}

// ── T3 / T4 合并 ────────────────────────────────────────────────────

// 时钟 id 域重映射（块内 id → 最终表 id；评审 P1-1 的桥）。文件号或块内
// id 越界（正常流程不发生——remap 由同批 slices 产出）返回默认哨兵，归
// 属降级为无时钟不指错
CMClockId tm_remap_clock_id(const TMClockRemap& remap, uint32_t file_index,
                            CMClockId chunk_clock_id) {
    if (!chunk_clock_id.is_valid()) {
        return chunk_clock_id;
    }
    if (file_index >= remap.file_maps_.size()) {
        return CMClockId{};
    }
    const CMVector<uint32_t>& file_map = remap.file_maps_[file_index];
    if (chunk_clock_id.value() >= file_map.size()) {
        return CMClockId{};
    }
    return CMClockId{file_map[chunk_clock_id.value()]};
}

// T3：每分区合并（时钟归属经 remap 重写为最终表 id；文件感知冲突判定，
// 见头文件注释）
TMPartitionTiming tm_merge_partition(
    const CMVector<const TMEntrySlice*>& slices, const TMClockRemap& remap,
    CMPartitionId pid, uint64_t& conflict_count) {
    TMPartitionTiming out;
    out.part_id_ = pid;
    // 同 (实例, pin) 首见来源文件号（合并过程局部表，不序列化——键 =
    // inst << 32 | pin；64 位实例 id 高 32 位与本组合键冲突的场景不存在
    //——instance id 实际量级 10⁹ << 2^32）
    CMUnorderedMap<uint64_t, uint32_t> first_file;
    const auto key_of = [](CMInstanceId inst, CMPinId pin) {
        return (inst.value() << 32) | pin.value();
    };
    for (const TMEntrySlice* slice : slices) {
        if (slice == nullptr) {
            continue;
        }
        const auto it = slice->partitions_.find(pid);
        if (it == slice->partitions_.end()) {
            continue;  // 本块未触及该分区
        }
        const uint32_t file_index = slice->stats_.file_index_;
        for (const auto& [inst_id, src] : it->second.items_) {
            // 时钟归属先重映射到最终表 id 域（块内 id 不可跨块/跨文件比
            // 较——评审 P1-1）
            const CMClockId clock_id =
                tm_remap_clock_id(remap, file_index, src.clock_id_);
            auto dit = out.items_.find(inst_id);
            if (dit == out.items_.end()) {
                TMInstanceTiming& slot = out.items_[inst_id];
                slot.clock_id_ = clock_id;
                slot.pins_ = src.pins_;
                for (const TMPinTiming& pin : src.pins_) {
                    first_file[key_of(inst_id, pin.pin_id_)] = file_index;
                }
                continue;
            }
            TMInstanceTiming& dst = dit->second;
            // 实例时钟归属：无 → 有补齐（无信息损失）；双有不同保留首份
            if (!dst.clock_id_.is_valid() && clock_id.is_valid()) {
                dst.clock_id_ = clock_id;
            }
            for (const TMPinTiming& pin : src.pins_) {
                const uint64_t key = key_of(inst_id, pin.pin_id_);
                const auto fit = first_file.find(key);
                if (fit != first_file.end()) {
                    if (fit->second != file_index) {
                        ++conflict_count;  // TIMG::0006：跨文件保留首份
                    }
                    continue;  // 同文件跨块同源形态：静默保留首份
                }
                first_file.emplace(key, file_index);
                dst.pins_.push_back(pin);
            }
        }
    }
    return out;
}

// T4 ①：统计聚合——全部块统计片段直和 + 按文件名归并逐文件表
TMSummary tm_merge_summary(const CMVector<const TMStatsDelta*>& deltas,
                           uint64_t cross_file_conflict_count,
                           uint64_t clock_conflict_count) {
    TMSummary s;
    s.cross_file_conflict_count_ = cross_file_conflict_count;
    s.clock_conflict_count_ = clock_conflict_count;
    // 按文件名归并逐文件表（同文件多块：计数累加、首块定名/单位）
    CMUnorderedMap<CMString, size_t> file_index;
    for (const TMStatsDelta* delta : deltas) {
        if (delta == nullptr) {
            continue;
        }
        for (const TMFileStats& f : delta->files_) {
            auto it = file_index.find(f.source_file_);
            if (it == file_index.end()) {
                file_index.emplace(f.source_file_, s.files_.size());
                s.files_.push_back(f);
                continue;
            }
            TMFileStats& dst = s.files_[it->second];
            dst.entry_count_ += f.entry_count_;
            dst.hit_count_ += f.hit_count_;
            dst.skipped_instance_count_ += f.skipped_instance_count_;
            dst.net_name_miss_count_ += f.net_name_miss_count_;
            dst.skipped_pin_count_ += f.skipped_pin_count_;
            dst.dangling_net_count_ += f.dangling_net_count_;
            dst.unplaced_instance_count_ += f.unplaced_instance_count_;
            dst.strip_miss_count_ += f.strip_miss_count_;
            dst.failed_chunk_count_ += f.failed_chunk_count_;
        }
        // 全局计数直和
        s.multi_driver_net_count_ += delta->multi_driver_net_count_;
        s.pg_net_skip_count_ += delta->pg_net_skip_count_;
        s.const_entry_count_ += delta->const_entry_count_;
        s.no_window_count_ += delta->no_window_count_;
        s.dropped_source_res_count_ += delta->dropped_source_res_count_;
        s.dropped_slack_count_ += delta->dropped_slack_count_;
        s.missing_clock_count_ += delta->missing_clock_count_;
        s.cd_flag_c_count_ += delta->cd_flag_c_count_;
        s.cd_flag_d_count_ += delta->cd_flag_d_count_;
    }
    // 逐文件族全局汇总（TIMG 消息文案的计数源）
    for (const TMFileStats& f : s.files_) {
        s.total_entry_count_ += f.entry_count_;
        s.total_hit_count_ += f.hit_count_;
        s.skipped_instance_count_ += f.skipped_instance_count_;
        s.net_name_miss_count_ += f.net_name_miss_count_;
        s.skipped_pin_count_ += f.skipped_pin_count_;
        s.dangling_net_count_ += f.dangling_net_count_;
        s.unplaced_instance_count_ += f.unplaced_instance_count_;
        s.strip_miss_count_ += f.strip_miss_count_;
        s.failed_chunk_count_ += f.failed_chunk_count_;
    }
    return s;
}

// T4 ②（时钟表合并任务本体）：时钟表跨文件合并（同名保留首份；周期/沿
// 任一差异计 TIMG::0007）+ per-file 重映射表产出
namespace {

// 单文件表归并进 out（同名保留首份），返回 src 每条目在 out 表的下标
//（评审 P1-1：重映射依据）
CMVector<uint32_t> merge_clock_file(TMClockTable& out,
                                    const TMClockTable& src,
                                    uint64_t& conflict_count) {
    CMVector<uint32_t> map;
    map.reserve(src.clocks_.size());
    for (const TMClockTable::Entry& c : src.clocks_) {
        size_t found = out.clocks_.size();
        for (size_t i = 0; i < out.clocks_.size(); ++i) {
            if (out.clocks_[i].name_ == c.name_) {
                found = i;
                break;
            }
        }
        if (found == out.clocks_.size()) {
            TMClockTable::Entry e;
            e.name_ = c.name_;
            e.period_ = c.period_;
            e.posedge_ = c.posedge_;
            e.negedge_ = c.negedge_;
            out.clocks_.push_back(std::move(e));
        } else if (out.clocks_[found].period_ != c.period_ ||
                   out.clocks_[found].posedge_ != c.posedge_ ||
                   out.clocks_[found].negedge_ != c.negedge_) {
            ++conflict_count;  // TIMG::0007：保留首份
        }
        map.push_back(static_cast<uint32_t>(found));
    }
    return map;
}
}  // namespace

TMClockTable tm_merge_clocks(
    const CMVector<std::pair<int, TMClockTable>>& file_clocks,
    TMClockRemap& remap, uint64_t& conflict_count) {
    // ① 文件内归并（同名保留首份——头段公共前缀保证同文件跨块 WAVEFORM
    // 表一致，防御异形）
    CMVector<TMClockTable> per_file;
    per_file.reserve(file_clocks.size());
    for (const auto& [kind, clocks] : file_clocks) {
        (void)kind;
        TMClockTable merged;
        uint64_t ignored = 0;
        merge_clock_file(merged, clocks, ignored);
        per_file.push_back(std::move(merged));
    }
    // ② 跨文件优先级合并（2026-09-16 裁定 5）：顶层文件（无绑定、纯路径
    // kind 0）定义优先，其余（块绑定文件）按文件序首份兜底。逐文件记录
    // 块内 id → 最终表下标（T3 时钟归属重写依据——评审 P1-1）
    TMClockTable out;
    remap.file_maps_.assign(file_clocks.size(), {});
    for (size_t i = 0; i < file_clocks.size(); ++i) {
        if (file_clocks[i].first == 0) {
            remap.file_maps_[i] = merge_clock_file(out, per_file[i],
                                                   conflict_count);
        }
    }
    for (size_t i = 0; i < file_clocks.size(); ++i) {
        if (file_clocks[i].first != 0) {
            remap.file_maps_[i] = merge_clock_file(out, per_file[i],
                                                   conflict_count);
        }
    }
    return out;
}

}  // namespace fly
