#include <emir/waveform/cpp/wf_partition.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
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
// 未命中 / 剥后余空返回 false（调用方 WFMG::0010 计数跳过）
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

// 根 scope 剥除（plan §7，2026-09-16 命名裁定：design db 层级路径不含
// 设计名前缀）：var 名首段 = 设计名（DSHierTree design_name_）时剥去；
// 其余原样返回（不匹配由名字换算未命中计数兜底）
CMString strip_root_scope(const CMString& name, const CMString& design_name) {
    if (design_name.empty() || name.size() <= design_name.size()) {
        return name;
    }
    if (name.compare(0, design_name.size(), design_name) == 0 &&
        name[design_name.size()] == '/') {
        return name.substr(design_name.size() + 1);
    }
    return name;
}

// 位名后缀解析：name = "base[i]"（i 非负十进制）→ base + i；其余形态
//（无后缀 / 空下标 / 非数字）返回 false
struct BitSuffix {
    CMString base;
    int64_t index = 0;
};

bool parse_bit_suffix(const CMString& name, BitSuffix& out) {
    if (name.size() < 3 || name.back() != ']') {
        return false;
    }
    const size_t open = name.find_last_of('[');
    if (open == CMString::npos || open == 0 || open + 1 == name.size() - 1) {
        return false;
    }
    int64_t value = 0;
    for (size_t i = open + 1; i + 1 < name.size(); ++i) {
        const char c = name[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out.base = name.substr(0, open);
    out.index = value;
    return true;
}

// 位权 → 位名下标（上游接口备注①：轨道 bits_[i] i=0 为位权 2^0 位；
// 位名方向由声明区间裁定——[3:0] 降序 2^0 位 = 下标 right、[0:3] 升序
// 2^0 位 = 下标 left、无区间 = 下标即位权）
int64_t name_index_of_bit(const WFVar& var, uint32_t weight) {
    if (!var.has_range_ || var.width_ <= 1) {
        return static_cast<int64_t>(weight);
    }
    if (var.range_left_ >= var.range_right_) {
        return var.range_right_ + static_cast<int64_t>(weight);
    }
    return var.range_left_ + static_cast<int64_t>(weight);
}

// 位名下标 → 位权（聚合方向换算；越界返回 width = 无效哨兵，调用方判弃）
uint32_t bit_of_name_index(const WFVar& var, int64_t name_index) {
    int64_t weight = name_index;
    if (var.has_range_ && var.width_ > 1) {
        weight = var.range_left_ >= var.range_right_
                     ? name_index - var.range_right_
                     : name_index - var.range_left_;
    }
    if (weight < 0 || weight >= static_cast<int64_t>(var.width_)) {
        return var.width_;
    }
    return static_cast<uint32_t>(weight);
}

// 位名构造：base + "[i]"
CMString make_bit_name(const CMString& base, int64_t index) {
    CMString out = base;
    out += '[';
    out += std::to_string(index);
    out += ']';
    return out;
}

// 层级实例路径 → 树节点 id（2026-09-16 裁定 1 语义：路径不含设计名前
// 缀——自 root 内容段起；空串 = root。块实例定位专用——DSNameMapperT
// 的 global id 反查会命中父块区间，不能用于块节点定位）。未命中 kNoNode。
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
// → self_global_id；hasher 哨兵/越界 → 默认哨兵）
CMInstanceId local_to_global_instance(const DSHierNode& node,
                                      CMInstanceId local) {
    if (local == 0) {
        return node.get_self_global_id();
    }
    if (local >= node.get_instance_count()) {
        return CMInstanceId{};
    }
    return CMInstanceId{node.get_instance_start().value() + local.value()};
}

// ── 块字节区间事件扫描（裁定 V-2：**纯事件**——不过滤值等价重复行、
//    不做四态时长统计，均挪 T3 合并端；无当前值表。词法与解析器共用
//    wf_types 公共单点（裁定 IV-5）：wf_scalar_value / wf_dump_keyword；
//    语义与解析器逐条对齐：标量速记 = 全位同值 / 向量最高位在前 LSB 右
//    对齐短值补 0 / $dump 系四关键字 / $dumpoff 全 x 合成——不过滤形态
//    下无差别施值，过滤由 T3 承担）──

class ChunkScanner {
public:
    ChunkScanner(const WFVarTable& vars, uint64_t timescale_fs)
        : vars_(vars), timescale_fs_(timescale_fs) {
        // 逐轨道逐位事件槽（轨道/位规模以 var 表为准）
        events_.resize(vars_.track_count());
        for (size_t t = 0; t < events_.size(); ++t) {
            const uint32_t width =
                t < vars_.track_widths_.size() ? vars_.track_widths_[t] : 0;
            events_[t].resize(width);
        }
    }

    void run(const CMString& text) {
        data_ = text.data();
        size_ = text.size();
        pos_ = 0;
        CMString tok;
        while (next(tok)) {
            if (tok[0] == '#' && tok.size() > 1) {
                now_fs_ = parse_ts_digits(tok.data() + 1, tok.size() - 1) *
                          timescale_fs_;
            } else if (tok[0] == '$') {
                parse_keyword(tok);
            } else {
                parse_value_record(tok);
            }
        }
    }

    // 逐轨道逐位事件（时间升序；[track][bit]；未经等值过滤——T3 过滤）
    CMVector<CMVector<CMVector<WFEvent>>> events_;
    // 解析级观测（事件总数〔未过滤〕/ x·z 态事件观测 / real·EVCD 值行
    // 跳过——0012）
    uint64_t total_event_count_ = 0;
    uint64_t x_z_event_count_ = 0;
    uint64_t unsupported_record_count_ = 0;

private:
    static bool is_space(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
    }

    bool next(CMString& tok) {
        tok.clear();
        while (pos_ < size_ && is_space(data_[pos_])) {
            ++pos_;
        }
        if (pos_ >= size_) {
            return false;
        }
        while (pos_ < size_ && !is_space(data_[pos_])) {
            tok.push_back(data_[pos_]);
            ++pos_;
        }
        return true;
    }

    static uint64_t parse_ts_digits(const char* s, size_t n) {
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) {
            const char c = s[i];
            if (c < '0' || c > '9') {
                return v;  // 防御：块文本已经切块扫描校验，容错截断
            }
            v = v * 10 + static_cast<uint64_t>(c - '0');
        }
        return v;
    }

    void emit(size_t track, size_t bit, WFValue v) {
        events_[track][bit].push_back(WFEvent{now_fs_, v});
        ++total_event_count_;
        if (v == WFValue::X || v == WFValue::Z) {
            ++x_z_event_count_;
        }
    }

    void parse_keyword(const CMString& tok) {
        // $keyword 可带括号参数后缀（$dumpvars(1, top) 单 token 形态）
        // ——词法公共单点 wf_dump_keyword + 与解析器同构的参数消费
        CMString kw;
        bool scan_args = false;
        const bool is_dump = wf_dump_keyword(tok, kw, scan_args);
        if (scan_args) {
            consume_args();
        } else if (is_dump && tok.find('(') == CMString::npos) {
            const size_t save = pos_;
            CMString t;
            if (next(t) && t[0] == '(') {
                consume_args_from(t);
            } else {
                pos_ = save;  // 非参数 token 回退
            }
        }
        if (is_dump) {
            parse_dump_section(kw == "$dumpoff");
        } else {
            skip_to_end();  // $comment / 未知关键字（防御性跳过）
        }
    }

    void consume_args() {
        CMString t;
        while (next(t)) {
            if (t.find(')') != CMString::npos) {
                return;
            }
        }
    }

    void consume_args_from(const CMString& first) {
        if (first.find(')') != CMString::npos) {
            return;
        }
        consume_args();
    }

    void skip_to_end() {
        CMString t;
        for (;;) {
            if (!next(t)) {
                return;  // 未闭合（防御：块文本已校验，容错收尾）
            }
            if (t == "$end") {
                return;
            }
        }
    }

    // $dump 段：段内值行照常施值；$dumpoff 语义 = 全值轨道置 x——段内
    // 未显式出现的轨道逐位合成 x 事件（与解析器同口径；重复置位由 T3
    // 等值过滤消除）
    void parse_dump_section(bool is_off) {
        CMUnorderedSet<uint32_t> touched;
        CMString tok;
        for (;;) {
            if (!next(tok)) {
                return;  // 未闭合（防御：块文本已校验，容错收尾）
            }
            if (tok == "$end") {
                break;
            }
            if (tok[0] == '$') {
                continue;
            }
            parse_value_record(tok, is_off ? &touched : nullptr);
        }
        if (!is_off) {
            return;
        }
        for (size_t t = 0; t < events_.size(); ++t) {
            if (touched.count(static_cast<uint32_t>(t)) != 0) {
                continue;
            }
            for (size_t b = 0; b < events_[t].size(); ++b) {
                emit(t, b, WFValue::X);
            }
        }
    }

    void parse_value_record(const CMString& tok,
                            CMUnorderedSet<uint32_t>* touched = nullptr) {
        const char c = tok[0];
        WFValue v = WFValue::X;
        if (wf_scalar_value(c, v)) {
            CMString id = tok.substr(1);
            if (id.empty() && !next(id)) {
                return;
            }
            apply_scalar(id, v, touched);
            return;
        }
        if (c == 'b' || c == 'B') {
            CMString bits = tok.substr(1);
            if (bits.empty() && !next(bits)) {
                return;
            }
            CMString id;
            if (!next(id)) {
                return;
            }
            apply_vector(bits, id, touched);
            return;
        }
        if (c == 'r' || c == 'R' || c == 's' || c == 'S' || c == 'p' ||
            c == 'P') {
            // real 值行 / EVCD 强度·port 记录：按「记录 token + 邻随 id
            // token」消费跳过（WFMG::0012）
            CMString id;
            if (!next(id)) {
                return;
            }
            ++unsupported_record_count_;
        }
        // 其余形态：块文本已经切块扫描校验，防御性跳过
    }

    // 标量速记施于轨道 = 全位同值（不过滤重复行——全部进分片，裁定 V-2）
    void apply_scalar(const CMString& id, WFValue v,
                      CMUnorderedSet<uint32_t>* touched) {
        const size_t t = vars_.find_track(id);
        if (t == WFVarTable::kNoTrack || events_[t].empty()) {
            return;
        }
        if (touched != nullptr) {
            touched->insert(static_cast<uint32_t>(t));
        }
        for (size_t b = 0; b < events_[t].size(); ++b) {
            emit(t, b, v);
        }
    }

    // 向量记录 = 逐位施值（最高位在前 + LSB 右对齐：值串最右字符 = 位
    // 权 2^0 位，短值左侧补 0；不过滤重复行——裁定 V-2）
    void apply_vector(const CMString& bits, const CMString& id,
                      CMUnorderedSet<uint32_t>* touched) {
        const size_t t = vars_.find_track(id);
        if (t == WFVarTable::kNoTrack) {
            return;
        }
        const size_t width = events_[t].size();
        for (const char ch : bits) {
            WFValue dummy = WFValue::X;
            if (!wf_scalar_value(ch, dummy)) {
                return;  // 非法位串字符（防御）
            }
        }
        if (bits.size() > width) {
            return;  // 位串超宽（防御）
        }
        if (touched != nullptr) {
            touched->insert(static_cast<uint32_t>(t));
        }
        for (size_t b = 0; b < width; ++b) {
            WFValue v = WFValue::ZERO;
            if (b < bits.size()) {
                wf_scalar_value(bits[bits.size() - 1 - b], v);
            }
            emit(t, b, v);
        }
    }

    const WFVarTable& vars_;
    uint64_t timescale_fs_;
    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    uint64_t now_fs_ = 0;
};

// 块内局部下降：按段降到叶块节点（段数 − 1 层下降；末段 = 叶块 hasher
// 查询名）。未命中返回 nullptr。
const DSHierNode* descend_block_node(const DSHierTree& tree,
                                     const DSHierNode& leaf,
                                     const CMVector<CMString>& segs) {
    const DSHierNode* node = &leaf;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        const uint32_t child =
            tree.find_child_by_instance_name(node->get_id(), segs[i]);
        if (child == DSHierTree::kNoNode) {
            return nullptr;
        }
        node = &tree.node(child);
    }
    return node;
}

// 单位轨道路径统计（T3 ③用；严格口径沿计数——裁定 7）
struct BitStats {
    uint64_t t0 = 0;
    uint64_t t1 = 0;
    uint64_t tx = 0;
    uint64_t tz = 0;
    uint64_t rise = 0;
    uint64_t fall = 0;

    void add(WFValue v, uint64_t dur) {
        switch (v) {
            case WFValue::ZERO:
                t0 += dur;
                break;
            case WFValue::ONE:
                t1 += dur;
                break;
            case WFValue::X:
                tx += dur;
                break;
            case WFValue::Z:
                tz += dur;
                break;
        }
    }
    void count_transition(WFValue from, WFValue to) {
        // 严格口径（裁定 7）：仅 0→1 / 1→0；x/z 参与的转换不计
        if (from == WFValue::ZERO && to == WFValue::ONE) {
            ++rise;
        } else if (from == WFValue::ONE && to == WFValue::ZERO) {
            ++fall;
        }
    }
};

// 值等价过滤（裁定 V-2 T3 ②）：完整序列上丢弃与前值相同的重复行；首
// 事件无前值不过滤（README 特征 4/5 幂等口径——重复行合并端消除）
void filter_duplicate_events(CMVector<WFEvent>& events) {
    size_t kept = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        if (kept > 0 && events[kept - 1].value_ == events[i].value_) {
            continue;
        }
        events[kept++] = events[i];
    }
    events.resize(kept);
}

// 全程统计（裁定 V-2 T3 ③）：窗口 = [win_start, win_end]（贡献文件偏移
// 对齐后的时间范围——块界概念于统计中不存在，多块拼接 == 整读单块）；
// 序列初值 = 轨道初态 X——前导段〔win_start 至首事件〕归 X（$dumpvars
// 初值行已是序列首事件；无初值行的未转储初值段归 X，README xz.vcd
// n_seed TX=8 口径）；相邻事件间隔 + 尾段〔末事件至 win_end〕归末态。
// events 须已过等值过滤、时间升序。
void accumulate_bit_stats(const CMVector<WFEvent>& events, uint64_t win_start,
                          uint64_t win_end, BitStats& st) {
    uint64_t prev = win_start;
    WFValue v = WFValue::X;
    for (const WFEvent& e : events) {
        if (e.time_fs_ > prev) {
            st.add(v, e.time_fs_ - prev);
        }
        st.count_transition(v, e.value_);
        v = e.value_;
        prev = e.time_fs_;
    }
    if (win_end > prev) {
        st.add(v, win_end - prev);
    }
}

// ── 换算主体（T2；块绑定节点集空 = 纯路径形态）──────────────────────

struct ConvertEnv {
    const WFDesignContext* ctx;
    const WFVarTable* vars;
    const CMVector<CMVector<CMVector<WFEvent>>>* events;  // [track][bit]
    WFFileStats* fstats;
    WFStatsDelta* delta;
    WFEntrySlice* slice;
    const CMVector<uint32_t>* block_nodes;   // 块绑定节点集（可空）
    const CMVector<CMString>* prefix_segs;   // strip_prefix 段表
    // (inst, pin) → 所连网（NETS 连接表反查表，§8 复制范围路由材料）
    const CMUnorderedMap<uint64_t, CMNetId>* pin_nets;
};

// 任一分区 NETS 副本定位网（信号网全量补全——任一副本即完整连接表，
// 同 timing/waveform 既有先例）
CMSharedPtr<const DSNet> find_net_any_partition(const WFDesignContext& ctx,
                                                CMNetId net_id) {
    for (const auto& entry : ctx.all_partition_nets()) {
        auto candidate = entry.second->net_of(net_id).lock();
        if (candidate != nullptr) {
            return candidate;
        }
    }
    return nullptr;
}

// (inst, pin) → 所连网反查表（§8 引脚身份波形复制范围与 T3 §7.3 四级回
// 落共用的连接表形态；条目序首条命中——多网连接取 NETS 首条）
CMUnorderedMap<uint64_t, CMNetId> build_pin_net_map(
    const WFDesignContext& ctx) {
    CMUnorderedMap<uint64_t, CMNetId> out;
    for (const auto& entry : ctx.all_partition_nets()) {
        for (const auto& [net_id, net] : entry.second->nets_) {
            for (const DSNetConnEntry& conn : net->connections_) {
                if (!(conn.is_driver() || conn.is_receiver())) {
                    continue;
                }
                const uint64_t key =
                    (conn.inst_id_.value() << 32) | conn.pin_id_.value();
                out.emplace(key, net_id);  // 首条保留
            }
        }
    }
    return out;
}

// 波形片段累积（裁定 V-2 纯事件分片：身份键 → 逐位事件列表，外层下标
// = 位位序〔与挂载 bits_ 的位下标对齐〕；同键多源 = 多 var 共轨/位名分
// 别命中，按位归并拼接保升序）。零事件位不进分片——空位表入分片会在
// 合并端产出空实体（r5/d6 类零事件挂载形态）。
void add_fragment(ConvertEnv& env, CMPartitionId pid, const WFWaveformKey& key,
                  size_t track, const CMVector<uint32_t>& bits) {
    if (track >= env.events->size()) {
        return;
    }
    const CMVector<CMVector<WFEvent>>& track_bits = (*env.events)[track];
    WFPartitionSlice* part = nullptr;
    for (const uint32_t bit : bits) {
        if (bit >= track_bits.size()) {
            continue;
        }
        const CMVector<WFEvent>& src = track_bits[bit];
        if (src.empty()) {
            continue;
        }
        if (part == nullptr) {
            part = &env.slice->partitions_[pid];
            part->part_id_ = pid;
        }
        auto& dst = part->waveforms_[key];
        if (dst.size() <= bit) {
            dst.resize(bit + 1);
        }
        const size_t mid = dst[bit].size();
        dst[bit].insert(dst[bit].end(), src.begin(), src.end());
        std::inplace_merge(dst[bit].begin(),
                           dst[bit].begin() + static_cast<long>(mid),
                           dst[bit].end(),
                           [](const WFEvent& a, const WFEvent& b) {
                               return a.time_fs_ < b.time_fs_;
                           });
    }
}

// 统计挂载（裁定 V-2：T2 无统计职责——条目只携带身份键 + 位集，统计值
// 由 T3 基于合并后完整序列计算；同 (inst, pin) 重复挂载 = 别名双挂防
// 御，保留首份）
void mount_stats(ConvertEnv& env, CMPartitionId pid, CMInstanceId inst_id,
                 CMPinId pin_id, const WFWaveformKey& key,
                 const CMVector<uint32_t>& bits) {
    WFStatsItem item;
    item.inst_id_ = inst_id;
    item.pin_id_ = pin_id;
    item.waveform_key_ = key;
    item.bits_ = bits;
    WFPartitionSlice& part = env.slice->partitions_[pid];
    part.part_id_ = pid;
    const uint64_t key64 =
        (inst_id.value() << 32) | pin_id.value();  // 组合键值域论证同
                                                   // tm_partition 先例
    part.stats_.emplace(key64, std::move(item));   // 重键保留首份
}

// 引脚身份波形分区复制（§8 扩展范围，裁定 V-5）：自身实例 primary 分区
// 已挂载，此处补挂所连网全部连接位实例的 primary 分区（§7.3 四级回落
// 3/4 级候选波形实体必在本区的每区自足保证）。同分区去重；未放置连接
// 位静默跳过（网侧挂载已计数，此处不重复计 0008）。端口条目不是实例
//（实例编号 = 块自身占位值）——排除，不计未放置（2026-11-24 端口角色
// 裁定：判定只看 is_port()，不用驱动/接收标记推断「是不是端口」）。
void copy_pin_fragments(ConvertEnv& env, CMPartitionId own_pid,
                        const WFWaveformKey& key, size_t track,
                        const CMVector<uint32_t>& bits) {
    const auto& pin_nets = *env.pin_nets;
    const auto it = pin_nets.find((key.inst_id_.value() << 32) |
                                  key.pin_id_.value());
    if (it == pin_nets.end()) {
        return;  // 悬空引脚（无所连网）——仅自身分区
    }
    const CMSharedPtr<const DSNet> net = find_net_any_partition(*env.ctx,
                                                                it->second);
    if (net == nullptr) {
        return;
    }
    CMVector<CMPartitionId> pids{own_pid};
    for (const DSNetConnEntry& conn : net->connections_) {
        if (conn.is_port()) {
            continue;
        }
        if (!(conn.is_driver() || conn.is_receiver())) {
            continue;
        }
        const CMPartitionId pid =
            env.ctx->locate_instance_partition(conn.inst_id_);
        if (!pid.is_valid() ||
            std::find(pids.begin(), pids.end(), pid) != pids.end()) {
            continue;
        }
        add_fragment(env, pid, key, track, bits);
        pids.push_back(pid);
    }
}

// 单网挂载：网 → 全部连接位（driver + receiver；§7 形态①——事件侧去重
// 为一份波形 + 全部连接位引脚各一条统计）。pg 已由调用方判定。连接位分
// 属多区时每区各存本区一份波形片段。命中 ≥1 引脚返回 true（身份键经
// key_out 回传——§7.2 轨道归并材料）。端口条目不是实例（实例编号 = 块
// 自身占位值）——收集连接位与数驱动者时排除、不计未放置实例
//（2026-11-24 端口角色裁定：判定只看 is_port()，不用驱动/接收标记推断
// 「是不是端口」）。
bool attach_net(ConvertEnv& env, CMNetId net_id, size_t track,
                const CMVector<uint32_t>& bits, WFWaveformKey& key_out) {
    const WFWaveformKey key{true, net_id, CMInstanceId{}, CMPinId{}};
    key_out = key;
    const CMSharedPtr<const DSNet> net = find_net_any_partition(*env.ctx,
                                                                net_id);
    if (net == nullptr) {
        ++env.fstats->dangling_net_count_;  // WFMG::0003（design db 无此网）
        return false;
    }
    CMVector<const DSNetConnEntry*> attachable;
    size_t drivers = 0;
    for (const DSNetConnEntry& conn : net->connections_) {
        if (conn.is_port()) {
            continue;  // 端口条目不是实例：排除
        }
        if (conn.is_driver() || conn.is_receiver()) {
            attachable.push_back(&conn);
            if (conn.is_driver()) {
                ++drivers;
            }
        }
    }
    if (attachable.empty()) {
        ++env.fstats->dangling_net_count_;  // 0003（无连接位/仅端口）
        return false;
    }
    if (drivers > 1) {
        ++env.delta->multi_driver_net_count_;  // 多驱动网观测
    }
    bool hit = false;
    CMVector<CMPartitionId> frag_pids;  // 同分区只产一份片段
    for (const DSNetConnEntry* conn : attachable) {
        const CMPartitionId pid =
            env.ctx->locate_instance_partition(conn->inst_id_);
        if (!pid.is_valid()) {
            ++env.fstats->unplaced_instance_count_;  // WFMG::0008
            continue;
        }
        if (std::find(frag_pids.begin(), frag_pids.end(), pid) ==
            frag_pids.end()) {
            add_fragment(env, pid, key, track, bits);
            frag_pids.push_back(pid);
        }
        mount_stats(env, pid, conn->inst_id_, conn->pin_id_, key, bits);
        hit = true;
    }
    return hit;
}

// 单 (inst, pin) 挂载。alias = nullptr → 形态②引脚身份（独立波形实体 +
// §8 复制范围路由）；alias 非空 → §7.2 轨道归并的引脚名引用（统计挂网
// 身份、不另立实体、不复制——网片段已覆盖全部连接位分区）。命中返回
// true。
bool attach_pin(ConvertEnv& env, CMInstanceId inst_id, CMPinId pin_id,
                size_t track, const CMVector<uint32_t>& bits,
                const WFWaveformKey* alias) {
    const CMPartitionId pid = env.ctx->locate_instance_partition(inst_id);
    if (!pid.is_valid()) {
        ++env.fstats->unplaced_instance_count_;  // WFMG::0008
        return false;
    }
    if (alias != nullptr) {
        mount_stats(env, pid, inst_id, pin_id, *alias, bits);
        return true;
    }
    const WFWaveformKey key{false, CMNetId{}, inst_id, pin_id};
    add_fragment(env, pid, key, track, bits);
    mount_stats(env, pid, inst_id, pin_id, key, bits);
    copy_pin_fragments(env, pid, key, track, bits);
    return true;
}

// var 全部位集
CMVector<uint32_t> all_bits_of(const WFVar& var) {
    CMVector<uint32_t> bits(var.width_);
    for (uint32_t i = 0; i < var.width_; ++i) {
        bits[i] = i;
    }
    return bits;
}

// 形态①网换算结果（§7.2 轨道归并材料）：outcome 三态 = 0 未命中（交形
// 态②）、1 命中、2 已解析跳过（pg/悬空/未放置——停止回退，不计命中）；
// single = 本 var 全部落点恰为一个网身份（可作轨道归并键——总线逐位探
// 测跨多网形态不可归并）；node_outcomes = 块绑定逐节点网支 outcome（平
// 行 block_nodes，pass 2 逐节点续行 port/pin 支）。
struct NetAttempt {
    int outcome = 0;
    WFWaveformKey key{};
    bool single = false;
    CMVector<int> node_outcomes;
};

// 形态①网换算（§7：完整名 → 位级两支——总线 var 逐位名探测 / 位名 var
// 基名聚合）。
NetAttempt convert_net_global(ConvertEnv& env, const CMString& name,
                              const WFVar& var) {
    NetAttempt out;
    const size_t track = env.vars->find_track(var.id_code_);
    const DSNetNameMapper& mapper = env.ctx->net_mapper();
    const CMNetId net_id = mapper.get_global_id(name);
    if (net_id.is_valid()) {
        // pg 网条目跳过（§7.5 防御口径——VCD 通常不转储 pg）
        const DSPgNetSet* pg = env.ctx->pg_nets();
        if (pg != nullptr && pg->is_pg(net_id)) {
            ++env.fstats->pg_net_skip_count_;
            out.outcome = 2;
            return out;
        }
        out.single = true;
        out.outcome =
            attach_net(env, net_id, track, all_bits_of(var), out.key) ? 1 : 2;
        return out;
    }
    BitSuffix suffix;
    const bool has_suffix = parse_bit_suffix(name, suffix);
    if (!has_suffix && var.width_ > 1) {
        // 总线 var（单名无位下标）→ 逐位名探测（DEF 逐位独立网形态）
        uint32_t hits = 0;
        CMNetId first_net{};
        bool multi_net = false;
        for (uint32_t i = 0; i < var.width_; ++i) {
            const CMNetId bit_net = mapper.get_global_id(
                make_bit_name(name, name_index_of_bit(var, i)));
            if (!bit_net.is_valid()) {
                continue;
            }
            WFWaveformKey key;
            if (attach_net(env, bit_net, track, {i}, key)) {
                ++hits;
                if (hits == 1) {
                    first_net = bit_net;
                } else if (!(bit_net == first_net)) {
                    multi_net = true;
                }
            }
        }
        if (hits > 0) {
            if (hits < var.width_) {
                ++env.fstats->width_mismatch_count_;  // WFMG::0013（不 fatal）
            }
            out.outcome = 1;
            out.single = !multi_net;
            out.key = WFWaveformKey{true, first_net, CMInstanceId{},
                                    CMPinId{}};
        }
        return out;
    }
    if (has_suffix) {
        // 位名 var → design 单名（无位下标）聚合：该 var 全部位并入同网
        //（位名下标越界 = 位权哨兵，弃挂交形态②）
        const CMNetId base_net = mapper.get_global_id(suffix.base);
        if (base_net.is_valid()) {
            const uint32_t weight = bit_of_name_index(var, suffix.index);
            if (weight < var.width_) {
                out.single = true;
                out.outcome = attach_net(env, base_net, track, {weight},
                                         out.key)
                                  ? 1
                                  : 2;
            }
        }
    }
    return out;
}

// pin 解析（全局 pin 名字空间三支：完整名 / 位名基名聚合 / 总线 var 逐
// 位名探测）。命中（含部分命中）返回 true；alias 语义见 attach_pin。
bool resolve_and_attach_pin(ConvertEnv& env, CMInstanceId inst_id,
                            const CMString& pin_name, const WFVar& var,
                            const WFWaveformKey* alias) {
    const size_t track = env.vars->find_track(var.id_code_);
    const DSPinNameHasher& pin_names = env.ctx->design().pin_names_;
    const CMPinId pin_id = pin_names.get_id(pin_name);
    if (pin_id.is_valid()) {
        return attach_pin(env, inst_id, pin_id, track, all_bits_of(var),
                          alias);
    }
    BitSuffix suffix;
    if (parse_bit_suffix(pin_name, suffix)) {
        const CMPinId base_pin = pin_names.get_id(suffix.base);
        if (base_pin.is_valid()) {
            const uint32_t weight = bit_of_name_index(var, suffix.index);
            if (weight < var.width_) {
                return attach_pin(env, inst_id, base_pin, track, {weight},
                                  alias);
            }
        }
        ++env.fstats->inst_pin_miss_count_;  // WFMG::0002
        return false;
    }
    if (var.width_ > 1) {
        // 总线 var → 逐位 pin 名探测（cell 位级引脚名单形态）
        uint32_t hits = 0;
        for (uint32_t i = 0; i < var.width_; ++i) {
            const CMPinId bit_pin = pin_names.get_id(
                make_bit_name(pin_name, name_index_of_bit(var, i)));
            if (!bit_pin.is_valid()) {
                continue;
            }
            if (attach_pin(env, inst_id, bit_pin, track, {i}, alias)) {
                ++hits;
            }
        }
        if (hits > 0) {
            if (hits < var.width_) {
                ++env.fstats->width_mismatch_count_;  // WFMG::0013
            }
            return true;
        }
    }
    ++env.fstats->inst_pin_miss_count_;  // WFMG::0002
    return false;
}

// 形态②换算（§7：①未命中拆最末段——scope 路径 = 实例层级路径、var 名
// = pin 名；实例 + pin 双查）
bool convert_pin_global(ConvertEnv& env, const CMString& name,
                        const WFVar& var, const WFWaveformKey* alias) {
    const size_t last = name.find_last_of('/');
    if (last == CMString::npos || last + 1 == name.size()) {
        ++env.fstats->inst_pin_miss_count_;  // 形态错（无引脚段）→ 0002
        return false;
    }
    const CMString pin_name(name.data() + last + 1, name.size() - last - 1);
    const CMString inst_path = join_segments(
        split_segments(CMString(name.data(), last), '/'));
    const CMInstanceId inst_id = env.ctx->inst_mapper().get_global_id(inst_path);
    if (!inst_id.is_valid()) {
        ++env.fstats->inst_pin_miss_count_;  // 0002（实例名未命中）
        return false;
    }
    return resolve_and_attach_pin(env, inst_id, pin_name, var, alias);
}

// ── 块绑定换算（§7.1 block_inst local 名偏移 / §7.2 block_cell 建库期
//    复制；对齐 tm_partition 既有口径）────────────────────────────────

// 块内网换算三支（块 hasher 完整名 / 位名基名聚合 / 总线 var 逐位名探
// 测）。返回三态：0 未命中（交下一支）、1 命中、2 已解析跳过（pg——停
// 止后续尝试，不计命中）。hit_keys 非空时收集全部命中落点键（§7.2 轨
// 道归并材料）。
int convert_net_block(ConvertEnv& env, const DSHierNode& leaf,
                      const CMString& name, const WFVar& var,
                      CMVector<WFWaveformKey>* hit_keys) {
    const DSHierTree& tree = env.ctx->design().get_hier_tree();
    const auto hasher = env.ctx->net_hasher_of(leaf.get_block_cell_id());
    if (hasher == nullptr) {
        return 0;
    }
    const size_t track = env.vars->find_track(var.id_code_);
    // 局部名 → 叶块 hasher 查末段（多段名下降后改用下降叶块自己的
    // hasher——层级网语义；单段名 = 绑定块自身 hasher）
    const auto try_local = [&](const CMString& local_name,
                               uint32_t weight) -> int {
        const CMVector<CMString> segs = split_segments(local_name, '/');
        const DSHierNode* final_node = descend_block_node(tree, leaf, segs);
        if (final_node == nullptr) {
            return 0;  // 块内路径未命中
        }
        const auto final_hasher =
            env.ctx->net_hasher_of(final_node->get_block_cell_id());
        if (final_hasher == nullptr) {
            return 0;
        }
        const CMNetId local = final_hasher->get_id(segs.back());
        if (!local.is_valid() || local == 0 ||
            local >= final_node->get_net_count()) {
            return 0;  // 块内未命中（local 0 = 空洞位不登记名）
        }
        // 区间换算（start + local 无 −1，2026-09-14 裁定）
        const CMNetId net_id = final_node->get_net_start() + local;
        if (env.ctx->pg_nets() != nullptr &&
            env.ctx->pg_nets()->is_pg(net_id)) {
            ++env.fstats->pg_net_skip_count_;  // §7.5
            return 2;
        }
        WFWaveformKey key;
        const int r = attach_net(env, net_id, track,
                                 weight < var.width_
                                     ? CMVector<uint32_t>{weight}
                                     : all_bits_of(var),
                                 key)
                           ? 1
                           : 2;
        if (r == 1 && hit_keys != nullptr) {
            hit_keys->push_back(key);
        }
        return r;
    };
    const int full = try_local(name, var.width_);
    if (full != 0) {
        return full;
    }
    BitSuffix suffix;
    if (parse_bit_suffix(name, suffix)) {
        const uint32_t weight = bit_of_name_index(var, suffix.index);
        if (weight < var.width_) {
            return try_local(suffix.base, weight);
        }
        return 0;
    }
    if (var.width_ > 1) {
        uint32_t hits = 0;
        for (uint32_t i = 0; i < var.width_; ++i) {
            const int r = try_local(make_bit_name(name, name_index_of_bit(var, i)), i);
            if (r == 2) {
                return 2;
            }
            if (r == 1) {
                ++hits;
            }
        }
        if (hits > 0) {
            if (hits < var.width_) {
                ++env.fstats->width_mismatch_count_;  // WFMG::0013
            }
            return 1;
        }
    }
    return 0;
}

// 块端口换算（§7.1 归属键 = 块实例自身全局 id；pg 端口跳过）。三态同上。
int convert_port_block(ConvertEnv& env, const DSHierNode& leaf,
                       const CMString& name, const WFVar& var,
                       const WFWaveformKey* alias) {
    if (name.find('/') != CMString::npos) {
        return 0;  // 多段名非端口
    }
    const DSCell& cell = env.ctx->design().get_cell(leaf.get_block_cell_id());
    for (uint32_t i = 0; i < cell.pin_count(); ++i) {
        const DSPin& p = cell.pin_at(i);
        if (!p.is_port() ||
            env.ctx->design().pin_name_of(p.get_pin_id()) != name) {
            continue;
        }
        if (p.get_type() == DSPinType::POWER ||
            p.get_type() == DSPinType::GROUND) {
            ++env.fstats->pg_net_skip_count_;  // §7.5 pg 端口
            return 2;
        }
        const size_t track = env.vars->find_track(var.id_code_);
        return attach_pin(env, leaf.get_self_global_id(), p.get_pin_id(),
                          track, all_bits_of(var), alias)
                   ? 1
                   : 2;
    }
    return 0;
}

// 块内形态②（scope 路径 = 块内局部实例路径 + 全局 pin 名字空间）。三态。
int convert_pin_block(ConvertEnv& env, const DSHierNode& leaf,
                      const CMString& name, const WFVar& var,
                      const WFWaveformKey* alias) {
    const size_t last = name.find_last_of('/');
    if (last == CMString::npos || last + 1 == name.size()) {
        return 0;
    }
    const CMString pin_name(name.data() + last + 1, name.size() - last - 1);
    const DSHierTree& tree = env.ctx->design().get_hier_tree();
    const CMVector<CMString> segs(
        split_segments(CMString(name.data(), last), '/'));
    const DSHierNode* final_node = descend_block_node(tree, leaf, segs);
    if (final_node == nullptr) {
        return 0;
    }
    const auto hasher =
        env.ctx->inst_hasher_of(final_node->get_block_cell_id());
    if (hasher == nullptr) {
        return 0;
    }
    const CMInstanceId local = hasher->get_id(segs.back());
    if (!local.is_valid()) {
        return 0;
    }
    const CMInstanceId inst_id = local_to_global_instance(*final_node, local);
    if (!inst_id.is_valid()) {
        return 0;
    }
    return resolve_and_attach_pin(env, inst_id, pin_name, var, alias) ? 1 : 2;
}

// 块绑定 pass 1：形态①网换算逐节点执行（§7.2 轨道归并材料收集——全部
// 落点键 distinct 恰 1 时可作轨道键；block_cell 定义级多实例化复制形态
// 天然多键不归并）。node_outcomes 平行 block_nodes（pass 2 逐节点续行
// port/pin 支——保留旧「网支已决节点停止」口径）。
NetAttempt convert_net_block_all(ConvertEnv& env, const CMString& name,
                                 const WFVar& var) {
    NetAttempt out;
    const DSHierTree& tree = env.ctx->design().get_hier_tree();
    CMVector<WFWaveformKey> hit_keys;
    for (const uint32_t node_id : *env.block_nodes) {
        const int r = convert_net_block(env, tree.node(node_id), name, var,
                                        &hit_keys);
        out.node_outcomes.push_back(r);
        if (r == 1) {
            out.outcome = 1;
        }
    }
    if (out.outcome == 1) {
        CMVector<WFWaveformKey> distinct;
        for (const WFWaveformKey& k : hit_keys) {
            if (std::find(distinct.begin(), distinct.end(), k) ==
                distinct.end()) {
                distinct.push_back(k);
            }
        }
        out.single = distinct.size() == 1;
        if (!distinct.empty()) {
            out.key = distinct.front();
        }
    }
    return out;
}

// 块绑定 pass 2：port/pin 支逐节点续行（网支 outcome 0 的节点）。任一
// 节点命中返回 true。
bool convert_pin_block_all(ConvertEnv& env, const CMString& name,
                           const WFVar& var,
                           const CMVector<int>& net_outcomes,
                           const WFWaveformKey* alias) {
    const DSHierTree& tree = env.ctx->design().get_hier_tree();
    bool hit = false;
    for (size_t i = 0; i < env.block_nodes->size(); ++i) {
        if (i < net_outcomes.size() && net_outcomes[i] != 0) {
            continue;  // 网支已命中/已解析跳过——本节点停止（旧口径）
        }
        const DSHierNode& leaf = tree.node((*env.block_nodes)[i]);
        int r = convert_port_block(env, leaf, name, var, alias);
        if (r == 0) {
            r = convert_pin_block(env, leaf, name, var, alias);
        }
        if (r == 1) {
            hit = true;
        }
    }
    return hit;
}

}  // namespace

// ── 落库形态构建期接口 ───────────────────────────────────────────────

const WFPinActivity* WFInstanceActivity::find_pin(CMPinId pin_id) const {
    for (const WFPinActivity& p : pins_) {
        if (p.pin_id_ == pin_id) {
            return &p;
        }
    }
    return nullptr;
}

// ── T2 换算上下文 ───────────────────────────────────────────────────

void WFDesignContext::set_design(CMSharedPtr<DSDesign> design) {
    design_ = std::move(design);
    rebuild_mappers();
}

void WFDesignContext::rebuild_mappers() {
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

void WFDesignContext::add_block_names(CMSharedPtr<const DSBlockNames> names) {
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

void WFDesignContext::set_inst_id_map(CMSharedPtr<DSIdPartitionIndex> index) {
    inst_index_ = std::move(index);
}

void WFDesignContext::add_inst_segment(
    CMSharedPtr<DSIdPartitionSegment> segment) {
    if (segment) {
        inst_segments_[segment->id_start_] = std::move(segment);
    }
}

void WFDesignContext::set_pg_nets(CMSharedPtr<DSPgNetSet> pg_nets) {
    pg_nets_ = std::move(pg_nets);
}

void WFDesignContext::add_partition_nets(CMSharedPtr<DSPartitionNets> nets) {
    if (nets) {
        part_nets_[nets->part_id_.value()] = std::move(nets);
    }
}

CMPartitionId WFDesignContext::locate_instance_partition(
    CMInstanceId inst_id) const {
    if (!inst_index_) {
        return CMPartitionId{};
    }
    const uint64_t seg_start = inst_index_->find_segment_start(inst_id.value());
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

CMSharedPtr<const DSInstanceNameHasher> WFDesignContext::inst_hasher_of(
    CMCellId block_cell_id) const {
    const auto it = inst_hashers_.find(block_cell_id);
    return it == inst_hashers_.end() ? CMSharedPtr<const DSInstanceNameHasher>{}
                                     : it->second;
}

CMSharedPtr<const DSNetNameHasher> WFDesignContext::net_hasher_of(
    CMCellId block_cell_id) const {
    const auto it = net_hashers_.find(block_cell_id);
    return it == net_hashers_.end() ? CMSharedPtr<const DSNetNameHasher>{}
                                    : it->second;
}

// ── T2 逐块换算 ─────────────────────────────────────────────────────

WFEntrySlice wf_convert_chunk(const WFDesignContext& ctx,
                              const CMString& path, const WFFileChunk& chunk,
                              const WFVarTable& vars, uint64_t timescale_fs,
                              const WFFileBinding& binding,
                              uint32_t file_index) {
    WFEntrySlice slice;
    slice.file_index_ = file_index;
    slice.binding_kind_ = static_cast<uint32_t>(binding.kind);
    WFFileStats fstats;
    fstats.source_file_ = path;
    fstats.timescale_sec_ = static_cast<double>(timescale_fs) * 1e-15;
    fstats.t_start_fs_ = chunk.t_start_fs_;
    fstats.t_end_fs_ = chunk.t_end_fs_;

    CMString text;
    if (!read_byte_range(path, chunk.start_, chunk.end_, text)) {
        ++fstats.failed_chunk_count_;  // 运行期不可读（环境异常兜底）
        slice.stats_.files_.push_back(std::move(fstats));
        return slice;
    }

    // 绑定定位（一次性；BLOCK_INST = 块实例节点、BLOCK_CELL = block cell
    // 全部实例化节点。定位失败 = 入口校验后环境漂移的防御场景，按块失
    // 败兜底计数——不 raise（worker 任务第三态禁令））
    CMVector<uint32_t> block_nodes;
    const DSHierTree& tree = ctx.design().get_hier_tree();
    if (binding.kind == WFFileBindingKind::BLOCK_INST) {
        const uint32_t node_id = find_node_by_path(tree, binding.block_inst);
        if (node_id == DSHierTree::kNoNode) {
            ++fstats.failed_chunk_count_;
            slice.stats_.files_.push_back(std::move(fstats));
            return slice;
        }
        block_nodes.push_back(node_id);
    } else if (binding.kind == WFFileBindingKind::BLOCK_CELL) {
        const CMCellId block_cell =
            ctx.design().cell_names_.get_id(binding.block_cell);
        if (!block_cell.is_valid()) {
            ++fstats.failed_chunk_count_;
            slice.stats_.files_.push_back(std::move(fstats));
            return slice;
        }
        for (uint32_t i = 0; i < tree.node_count(); ++i) {
            if (tree.node(i).get_block_cell_id() == block_cell) {
                block_nodes.push_back(i);
            }
        }
        if (block_nodes.empty()) {
            ++fstats.failed_chunk_count_;
            slice.stats_.files_.push_back(std::move(fstats));
            return slice;
        }
    }

    // 块字节区间流式事件扫描（裁定 V-2 纯事件——无值表、无过滤、无统计）
    ChunkScanner scanner(vars, timescale_fs);
    scanner.run(text);
    fstats.event_count_ = scanner.total_event_count_;
    fstats.unsupported_count_ = scanner.unsupported_record_count_;
    slice.stats_.x_z_event_count_ = scanner.x_z_event_count_;

    // strip_prefix 段表（§7.4）
    CMVector<CMString> prefix_segs;
    if (!binding.strip_prefix.empty()) {
        prefix_segs = split_segments(binding.strip_prefix, '/');
    }

    // (inst, pin) → 所连网反查表（§8 引脚身份波形复制范围路由材料）
    const CMUnorderedMap<uint64_t, CMNetId> pin_nets = build_pin_net_map(ctx);

    ConvertEnv env;
    env.ctx = &ctx;
    env.vars = &vars;
    env.events = &scanner.events_;
    env.fstats = &fstats;
    env.delta = &slice.stats_;
    env.slice = &slice;
    env.block_nodes = block_nodes.empty() ? nullptr : &block_nodes;
    env.prefix_segs = &prefix_segs;
    env.pin_nets = &pin_nets;

    // 名字换算两遍（§7 两形态 + §7.2 轨道身份归并 + 向量位级匹配 + 块绑
    // 定三形态 + 根 scope 剥除）。剥除顺序 = 根 scope（VCD 根段 = 设计
    // 名）先行、再 strip_prefix（段级精确剥离绑定块前缀——块绑定样张的
    // 名字形如 <设计名>/<块实例路径>/<局部名>，README「实施必读」节口
    // 径）。轨道归并（裁定 V-1）要求先全网支后引脚支的两遍序：同 id 码
    // 轨道多名中网名命中与否，须在引脚名挂载前定案。
    CMVector<CMString> names;      // 剥后名（与 vars.vars_ 平行；空 = strip 未命中）
    CMVector<size_t> var_tracks;   // var → 轨道下标
    names.reserve(vars.vars_.size());
    var_tracks.reserve(vars.vars_.size());
    for (const WFVar& var : vars.vars_) {
        CMString name = strip_root_scope(var.name_, ctx.design_name());
        if (!prefix_segs.empty()) {
            CMString stripped;
            if (!strip_prefix_segments(name, prefix_segs, stripped)) {
                name.clear();
                ++fstats.strip_miss_count_;  // WFMG::0010
            } else {
                name = std::move(stripped);
            }
        }
        names.push_back(std::move(name));
        var_tracks.push_back(vars.find_track(var.id_code_));
    }

    // pass 1：形态①网支（轨道归并键收集——首个单网落点定轨道身份）。
    // strip 未命中 var（names 空）已计 0010，两遍均跳过。纯路径形态①未
    // 命中即计 0001（形态②命中恢复不回冲——plan §9 两计数器各记各的形
    // 态，与首轮实现口径一致）；块绑定总未命中的 0001 留待 pass 2 定计
    //（旧口径：全支未命中才计）。
    CMVector<NetAttempt> attempts;
    attempts.reserve(vars.vars_.size());
    CMUnorderedMap<size_t, WFWaveformKey> track_keys;  // 轨道下标 → 网身份
    for (size_t i = 0; i < vars.vars_.size(); ++i) {
        attempts.push_back(NetAttempt{});
        if (names[i].empty()) {
            continue;  // strip 未命中（0010 已计）
        }
        if (env.block_nodes != nullptr) {
            attempts[i] = convert_net_block_all(env, names[i], vars.vars_[i]);
        } else {
            attempts[i] = convert_net_global(env, names[i], vars.vars_[i]);
            if (attempts[i].outcome == 0) {
                ++fstats.net_name_miss_count_;  // WFMG::0001（形态①）
            }
        }
        if (attempts[i].outcome == 1 && attempts[i].single) {
            const size_t track = var_tracks[i];
            if (track != WFVarTable::kNoTrack) {
                track_keys.emplace(track, attempts[i].key);  // 首个命中定身份
            }
        }
    }

    // pass 2：形态②引脚支 + 轨道归并的引脚名引用挂载（§7.2）。网支已
    // 命中（1）计命中不重复挂载；已解析跳过（2）停止回退；未命中（0）
    // 落引脚支——轨道有网身份时挂网引用（不另立实体），否则按引脚身份
    // 全量挂载（独立实体 + §8 复制范围）。
    for (size_t i = 0; i < vars.vars_.size(); ++i) {
        if (names[i].empty()) {
            continue;
        }
        bool hit = false;
        const size_t track = var_tracks[i];
        const auto tk = track != WFVarTable::kNoTrack
                            ? track_keys.find(track)
                            : track_keys.end();
        const WFWaveformKey* alias =
            tk != track_keys.end() ? &tk->second : nullptr;
        if (attempts[i].outcome == 1) {
            hit = true;  // 网支已挂载（pass 1 完成）
        } else if (attempts[i].outcome == 2) {
            // 已解析跳过（pg/悬空/未放置）——停止回退，不计命中
        } else if (env.block_nodes != nullptr) {
            hit = convert_pin_block_all(env, names[i], vars.vars_[i],
                                        attempts[i].node_outcomes, alias);
            if (!hit) {
                ++fstats.net_name_miss_count_;  // WFMG::0001（块内全支未
                                                // 命中——旧口径）
            }
        } else {
            hit = convert_pin_global(env, names[i], vars.vars_[i], alias);
        }
        if (hit) {
            ++fstats.hit_count_;
        }
    }

    slice.stats_.files_.push_back(std::move(fstats));
    return slice;
}

// ── T3 每分区合并 ───────────────────────────────────────────────────

namespace {

// 逐文件时间范围 + 绑定形态（偏移对齐与场景分派依据；自各块分片的统计
// 片段嗅探——同文件多块取首块起点/末块终点）
struct WFMergeFileRange {
    uint64_t t_start = UINT64_MAX;
    uint64_t t_end = 0;
    WFFileBindingKind kind = WFFileBindingKind::PATH;
};

// 波形合并状态（合并过程局部表，不序列化）：逐位事件序列（T3 ②过滤/
// ③统计的输入）+ 统计窗口（贡献文件偏移对齐后的范围并集）
struct WaveState {
    uint32_t id = 0;
    uint32_t file = UINT32_MAX;
    uint64_t win_start = UINT64_MAX;   // 统计窗口（偏移对齐后）
    uint64_t win_end = 0;
    CMVector<CMVector<WFEvent>> bits;  // 逐位事件（升序；未过滤）
    CMVector<BitStats> bit_stats;      // ③ 产物（与 bits 平行）
};

// 逐位事件带偏移追加（位内偏移对齐后仍非单调 → WFMG::0007 计数——单
// 调游标逐位独立：位片段按位序追加，位间时刻交错属健康形态〔总线单
// 名整挂单网/位直和/块绑定总线〕不计，方案 §9 该计数只管文件间时间
// 轴回退；追加后按位归并保升序——场景 a 跨文件偏移拼接/同文件跨块衔
// 接均保序）
void append_bit_events(WaveState& ws, const CMVector<CMVector<WFEvent>>& frag,
                       uint64_t offset, uint64_t& anomaly_count) {
    if (ws.bits.size() < frag.size()) {
        ws.bits.resize(frag.size());
    }
    for (size_t b = 0; b < frag.size(); ++b) {
        auto& dst = ws.bits[b];
        const size_t mid = dst.size();
        // 位内游标 = 本位已有序列末时刻（归并保升序 → 末元素即游标）
        uint64_t bit_last = mid > 0 ? dst[mid - 1].time_fs_ : 0;
        for (const WFEvent& e : frag[b]) {
            const uint64_t t = e.time_fs_ + offset;
            if (t < bit_last) {
                ++anomaly_count;
            } else {
                bit_last = t;
            }
            dst.push_back(WFEvent{t, e.value_});
        }
        std::inplace_merge(dst.begin(), dst.begin() + static_cast<long>(mid),
                           dst.end(), [](const WFEvent& a, const WFEvent& b) {
                               return a.time_fs_ < b.time_fs_;
                           });
    }
}

// 统计窗口吸收（贡献文件偏移对齐后的时间范围——失败块兜底条目的零窗
// 不参与；窗口 = 贡献文件范围并集，场景 a 分段文件窗口恰好衔接连续）
void absorb_window(WaveState& ws, const WFMergeFileRange& range,
                   uint64_t offset) {
    if (range.t_start == UINT64_MAX) {
        return;  // 该文件无有效块窗（全失败兜底）——不计窗口
    }
    ws.win_start = std::min(ws.win_start, range.t_start + offset);
    ws.win_end = std::max(ws.win_end, range.t_end + offset);
}

// 四级回落解析（§7.3，裁定 V-4）：引脚自身轨道 → 所连网轨道 → 同网
// output 引脚轨道（多个取连接表条目序第一个）→ 连接表第一个有波形引脚
//（不区分方向）；四级全无 = waves.end()（调用方落哨兵）。判定材料 =
// 本区波形实体集合 + NETS 连接表反查表（本地可解——§8 复制范围保证候
// 选实体必在本区）。
CMUnorderedMap<WFWaveformKey, WaveState>::const_iterator resolve_final_wave(
    const WFStatsItem& item,
    const CMUnorderedMap<WFWaveformKey, WaveState>& waves,
    const CMUnorderedMap<uint64_t, CMNetId>& pin_nets,
    const WFDesignContext& ctx) {
    // 1 级：引脚自身轨道（(inst, pin) 名字直接命中的实体）
    const WFWaveformKey own{false, CMNetId{}, item.inst_id_, item.pin_id_};
    auto it = waves.find(own);
    if (it != waves.end()) {
        return it;
    }
    // 2 级：所连网的网名轨道（挂载身份已是网身份优先——本引脚波形数据
    // 的实际来源；否则经 NETS 连接表反查该引脚所连网）
    CMNetId net_id{};
    if (item.waveform_key_.is_net_) {
        net_id = item.waveform_key_.net_id_;
    } else {
        const auto nit =
            pin_nets.find((item.inst_id_.value() << 32) |
                          item.pin_id_.value());
        if (nit != pin_nets.end()) {
            net_id = nit->second;
        }
    }
    CMSharedPtr<const DSNet> net;
    if (net_id.is_valid()) {
        it = waves.find(WFWaveformKey{true, net_id, CMInstanceId{},
                                      CMPinId{}});
        if (it != waves.end()) {
            return it;
        }
        net = find_net_any_partition(ctx, net_id);
    }
    if (net == nullptr) {
        return waves.end();
    }
    // 3/4 级：连接表条目序扫描——3 级先取 driver（output）引脚轨道，
    // 4 级回落不限方向的第一个有波形引脚
    for (int pass = 3; pass <= 4; ++pass) {
        for (const DSNetConnEntry& conn : net->connections_) {
            if (pass == 3 && !conn.is_driver()) {
                continue;
            }
            const WFWaveformKey pk{false, CMNetId{}, conn.inst_id_,
                                   conn.pin_id_};
            auto pit = waves.find(pk);
            if (pit != waves.end()) {
                return pit;
            }
        }
    }
    return waves.end();
}

}  // namespace

void wf_merge_partition(const WFDesignContext& ctx,
                        const CMVector<const WFEntrySlice*>& slices,
                        CMPartitionId pid, WFPartitionWaveform& waveform_out,
                        WFPartitionActivity& activity_out,
                        uint64_t& overlap_override_count,
                        uint64_t& time_anomaly_count) {
    waveform_out = WFPartitionWaveform{};
    waveform_out.part_id_ = pid;
    activity_out = WFPartitionActivity{};
    activity_out.part_id_ = pid;

    // ① 逐文件时间范围 + 绑定形态嗅探（§2.1 偏移对齐依据）
    CMVector<WFMergeFileRange> ranges;
    for (const WFEntrySlice* slice : slices) {
        if (slice == nullptr) {
            continue;
        }
        const size_t fi = slice->file_index_;
        if (fi >= ranges.size()) {
            ranges.resize(fi + 1);
        }
        // 绑定形态按文件统一（编排侧同文件同形态；缺省 PATH）
        if (slice->binding_kind_ != 0) {
            ranges[fi].kind =
                static_cast<WFFileBindingKind>(slice->binding_kind_);
        }
        for (const WFFileStats& f : slice->stats_.files_) {
            if (f.t_start_fs_ == 0 && f.t_end_fs_ == 0) {
                continue;  // 失败块兜底条目（零窗）不参与嗅探
            }
            ranges[fi].t_start = std::min(ranges[fi].t_start, f.t_start_fs_);
            ranges[fi].t_end = std::max(ranges[fi].t_end, f.t_end_fs_);
        }
    }
    // ② 场景 a 偏移对齐（§2.1）：各文件独立 [0,..] 形态（首时间戳 0）
    //    则累计前序纯路径文件时长作偏移；首时间戳非 0 视为已续接全局时
    //    间轴，不再偏移
    CMVector<uint64_t> offsets(ranges.size(), 0);
    uint64_t running = 0;
    for (size_t f = 0; f < ranges.size(); ++f) {
        if (ranges[f].kind != WFFileBindingKind::PATH) {
            continue;
        }
        if (ranges[f].t_start == 0) {
            offsets[f] = running;
        }
        if (ranges[f].t_end > ranges[f].t_start) {
            running += ranges[f].t_end - ranges[f].t_start;
        }
    }

    // ③ 波形 + 统计挂载合并（场景 a 直拼；场景 b·混合后文件覆盖）
    CMUnorderedMap<WFWaveformKey, WaveState> waves;
    struct PinState {
        WFStatsItem item;
        uint32_t file = UINT32_MAX;
    };
    CMUnorderedMap<uint64_t, PinState> pins;  // 键 = inst << 32 | pin
    for (const WFEntrySlice* slice : slices) {
        if (slice == nullptr) {
            continue;
        }
        const auto pit = slice->partitions_.find(pid);
        if (pit == slice->partitions_.end()) {
            continue;  // 本块未触及该分区
        }
        const uint32_t fi = slice->file_index_;
        const auto kind = static_cast<WFFileBindingKind>(slice->binding_kind_);
        const uint64_t offset =
            fi < offsets.size() ? offsets[fi] : 0;
        const WFPartitionSlice& part = pit->second;

        for (const auto& [key, frag] : part.waveforms_) {
            auto wit = waves.find(key);
            if (wit == waves.end()) {
                WaveState ws;
                ws.file = fi;
                append_bit_events(ws, frag, offset, time_anomaly_count);
                absorb_window(ws, ranges[fi], offset);
                waves.emplace(key, std::move(ws));
                continue;
            }
            WaveState& ws = wit->second;
            if (ws.file == fi) {
                append_bit_events(ws, frag, offset, time_anomaly_count);
                absorb_window(ws, ranges[fi], offset);
                continue;
            }
            const bool both_path =
                kind == WFFileBindingKind::PATH &&
                ranges[ws.file].kind == WFFileBindingKind::PATH;
            if (both_path) {
                // 场景 a：同身份键跨文件按文件序偏移拼接
                append_bit_events(ws, frag, offset, time_anomaly_count);
                absorb_window(ws, ranges[fi], offset);
                continue;
            }
            // 场景 b/混合（§2.1）：后文件整体替换先文件 + WFMG::0006
            ws.bits.clear();
            ws.file = fi;
            ws.win_start = UINT64_MAX;
            ws.win_end = 0;
            append_bit_events(ws, frag, offset, time_anomaly_count);
            absorb_window(ws, ranges[fi], offset);
            ++overlap_override_count;
        }

        for (const auto& [skey, item] : part.stats_) {
            auto dit = pins.find(skey);
            if (dit == pins.end()) {
                PinState ps;
                ps.item = item;
                ps.file = fi;
                pins.emplace(skey, std::move(ps));
                continue;
            }
            if (dit->second.file == fi ||
                (kind == WFFileBindingKind::PATH &&
                 ranges[dit->second.file].kind == WFFileBindingKind::PATH)) {
                // 场景 a：挂载同源（键 + 位集一致），保留首份即可——统
                // 计值由键轨迹合并序列统一计算，无需逐项直和
                continue;
            }
            // 场景 b/混合：后文件挂载替换（WFMG::0006 已在波形侧计数
            // ——实体存在且未被本文件重绑时补计，防零事件身份漏计）
            const auto wit = waves.find(item.waveform_key_);
            if (wit == waves.end() || wit->second.file != fi) {
                ++overlap_override_count;
            }
            dit->second.item = item;
            dit->second.file = fi;
        }
    }

    // ④ ② 等值过滤 + ③ 统计计算（逐位完整序列）+ 实体事件组装：
    //    waveform_id 按身份键全序统一重编（id 的预分配依赖分片无序 map
    //    迭代序——跨编译器/标准库 id 标号漂移；waveform_id 是持久化引用
    //    键，分配序收敛为 wf_waveform_key_less 全序，统计侧引用在重编后
    //    经键查表同步绑定，自动对齐）。id = 容器数组下标。
    CMVector<const WFWaveformKey*> key_order;
    key_order.reserve(waves.size());
    for (const auto& entry : waves) {
        key_order.push_back(&entry.first);
    }
    std::sort(key_order.begin(), key_order.end(),
              [](const WFWaveformKey* a, const WFWaveformKey* b) {
                  return wf_waveform_key_less(*a, *b);
              });
    waveform_out.waveforms_.resize(key_order.size());
    for (size_t i = 0; i < key_order.size(); ++i) {
        WaveState& ws = waves.find(*key_order[i])->second;
        ws.id = static_cast<uint32_t>(i);
        CMVector<WFEvent> merged;
        ws.bit_stats.resize(ws.bits.size());
        for (size_t b = 0; b < ws.bits.size(); ++b) {
            filter_duplicate_events(ws.bits[b]);            // ② 等值过滤
            accumulate_bit_stats(ws.bits[b], ws.win_start,  // ③ 全程统计
                                 ws.win_end, ws.bit_stats[b]);
            merged.insert(merged.end(), ws.bits[b].begin(), ws.bits[b].end());
        }
        std::stable_sort(merged.begin(), merged.end(),
                         [](const WFEvent& a, const WFEvent& b) {
                             return a.time_fs_ < b.time_fs_;
                         });
        WFWaveform& w = waveform_out.waveforms_[i];
        w.waveform_id_ = ws.id;
        w.events_ = std::move(merged);
        if (!w.events_.empty()) {
            w.t_start_fs_ = w.events_.front().time_fs_;
            w.t_end_fs_ = w.events_.back().time_fs_;
        }
    }

    // ⑤ 四级回落解析（§7.3）+ 统计绑最终引用：每引脚统计 = 最终引用波
    //    形在其挂载位集（回落换键时 = 引用实体全位）上的统计直和；四级
    //    全无 = 哨兵 + 统计全零（未覆盖信号——消费侧走 default 元数据）。
    const CMUnorderedMap<uint64_t, CMNetId> pin_nets = build_pin_net_map(ctx);
    for (auto& [skey, ps] : pins) {
        const WFStatsItem& item = ps.item;
        const auto final_it = resolve_final_wave(item, waves, pin_nets, ctx);
        WFPinActivity pin;
        pin.pin_id_ = item.pin_id_;
        if (final_it == waves.end()) {
            activity_out.items_[item.inst_id_].pins_.push_back(
                std::move(pin));  // 全零 + 哨兵（缺省值即目标形态）
            continue;
        }
        const WaveState& ws = final_it->second;
        pin.waveform_id_ = ws.id;
        // 统计位集：挂载键即最终键 → 挂载位集；回落换键 → 引用实体全位
        const bool same_key = item.waveform_key_ == final_it->first;
        for (size_t b = 0; b < ws.bit_stats.size(); ++b) {
            if (same_key &&
                std::find(item.bits_.begin(), item.bits_.end(),
                          static_cast<uint32_t>(b)) == item.bits_.end()) {
                continue;
            }
            const BitStats& st = ws.bit_stats[b];
            pin.t0_fs_ += st.t0;
            pin.t1_fs_ += st.t1;
            pin.tx_fs_ += st.tx;
            pin.tz_fs_ += st.tz;
            pin.rise_count_ += st.rise;
            pin.fall_count_ += st.fall;
        }
        activity_out.items_[item.inst_id_].pins_.push_back(std::move(pin));
    }
}

// ── T4 汇总 ─────────────────────────────────────────────────────────

WFSummary wf_merge_summary(const CMVector<const WFStatsDelta*>& deltas,
                           uint64_t waveform_count,
                           uint64_t overlap_override_count,
                           uint64_t time_anomaly_count,
                           uint64_t invalid_binding_count,
                           uint64_t override_entry_count,
                           const WFSettingsMeta& settings) {
    WFSummary s;
    s.waveform_count_ = waveform_count;
    s.overlap_override_count_ = overlap_override_count;
    s.time_anomaly_count_ = time_anomaly_count;
    s.invalid_binding_count_ = invalid_binding_count;
    s.override_entry_count_ = override_entry_count;
    s.default_toggle_rate_ = settings.default_toggle_rate_;
    s.default_static_probability_ = settings.default_static_probability_;
    s.duration_sec_ = settings.duration_sec_;
    s.x_z_handling_ = settings.x_z_handling_;
    // 按文件名归并逐文件表（同文件多块：计数累加、首块定名/档位/起点、
    // 末块终点）
    CMUnorderedMap<CMString, size_t> file_index;
    for (const WFStatsDelta* delta : deltas) {
        if (delta == nullptr) {
            continue;
        }
        for (const WFFileStats& f : delta->files_) {
            auto it = file_index.find(f.source_file_);
            if (it == file_index.end()) {
                file_index.emplace(f.source_file_, s.files_.size());
                s.files_.push_back(f);
                continue;
            }
            WFFileStats& dst = s.files_[it->second];
            dst.t_start_fs_ = std::min(dst.t_start_fs_, f.t_start_fs_);
            dst.t_end_fs_ = std::max(dst.t_end_fs_, f.t_end_fs_);
            dst.event_count_ += f.event_count_;
            dst.hit_count_ += f.hit_count_;
            dst.net_name_miss_count_ += f.net_name_miss_count_;
            dst.inst_pin_miss_count_ += f.inst_pin_miss_count_;
            dst.dangling_net_count_ += f.dangling_net_count_;
            dst.pg_net_skip_count_ += f.pg_net_skip_count_;
            dst.unplaced_instance_count_ += f.unplaced_instance_count_;
            dst.strip_miss_count_ += f.strip_miss_count_;
            dst.unsupported_count_ += f.unsupported_count_;
            dst.width_mismatch_count_ += f.width_mismatch_count_;
            dst.failed_chunk_count_ += f.failed_chunk_count_;
        }
        s.multi_driver_net_count_ += delta->multi_driver_net_count_;
        s.x_z_event_count_ += delta->x_z_event_count_;
    }
    // 逐文件族全局汇总 + 全局 DURATION（Σ文件时长——率/概率归一化唯一基
    // 准，plan §5.2）
    for (const WFFileStats& f : s.files_) {
        s.total_event_count_ += f.event_count_;
        s.total_hit_count_ += f.hit_count_;
        s.net_name_miss_count_ += f.net_name_miss_count_;
        s.inst_pin_miss_count_ += f.inst_pin_miss_count_;
        s.dangling_net_count_ += f.dangling_net_count_;
        s.pg_net_skip_count_ += f.pg_net_skip_count_;
        s.unplaced_instance_count_ += f.unplaced_instance_count_;
        s.strip_miss_count_ += f.strip_miss_count_;
        s.unsupported_count_ += f.unsupported_count_;
        s.width_mismatch_count_ += f.width_mismatch_count_;
        s.failed_chunk_count_ += f.failed_chunk_count_;
        if (f.t_end_fs_ > f.t_start_fs_) {
            s.total_duration_fs_ += f.t_end_fs_ - f.t_start_fs_;
        }
    }
    return s;
}

// ── settings-only 物化 ──────────────────────────────────────────────

WFPartitionWaveform wf_empty_partition_waveform(CMPartitionId pid) {
    WFPartitionWaveform out;
    out.part_id_ = pid;
    return out;
}

namespace {

// settings 换算公式（plan §4）：T1 = p × D、T0 = (1−p) × D、rise =
// rate × D × rise_fraction、fall = rate × D × (1−rise_fraction)；非整
// 值四舍五入取整。replace = false（settings-only 物化）：同 (inst, pin)
// 重复覆盖保留首份；replace = true（混合模式覆盖，裁定 IV-2）：已有条
// 目替换统计值、waveform_id 引用保留（TX/TZ 归零——换算公式口径），无
// 条目新建（哨兵引用，plan §8）。
void upsert_pin(WFPartitionActivity& out, CMInstanceId inst_id,
                CMPinId pin_id, const WFActivityOverride& ov,
                double duration_sec, bool replace) {
    const double p =
        std::clamp(ov.static_probability_, 0.0, 1.0);
    const double rf = std::clamp(ov.rise_fraction_, 0.0, 1.0);
    WFPinActivity pin;
    pin.pin_id_ = pin_id;
    pin.t1_fs_ = static_cast<uint64_t>(std::llround(p * duration_sec * 1e15));
    pin.t0_fs_ =
        static_cast<uint64_t>(std::llround((1.0 - p) * duration_sec * 1e15));
    pin.rise_count_ = static_cast<uint64_t>(
        std::llround(ov.toggle_rate_ * duration_sec * rf));
    pin.fall_count_ = static_cast<uint64_t>(
        std::llround(ov.toggle_rate_ * duration_sec * (1.0 - rf)));
    WFInstanceActivity& inst = out.items_[inst_id];
    for (WFPinActivity& existing : inst.pins_) {
        if (!(existing.pin_id_ == pin_id)) {
            continue;
        }
        if (!replace) {
            return;  // 保留首份（settings-only 重复覆盖防御）
        }
        existing.t0_fs_ = pin.t0_fs_;   // 混合模式：统计值替换
        existing.t1_fs_ = pin.t1_fs_;   //（waveform_id 引用保留——裁定 IV-2）
        existing.tx_fs_ = 0;
        existing.tz_fs_ = 0;
        existing.rise_count_ = pin.rise_count_;
        existing.fall_count_ = pin.fall_count_;
        return;
    }
    pin.waveform_id_ = kWfNoWaveform;  // 新建条目全哨兵（plan §8）
    inst.pins_.push_back(std::move(pin));
}

// 覆盖条目落点解析（两形态推导，wf_materialize_activity /
// wf_apply_overrides 共用单点）：形态① net 名 → 该网全部连接位展开；
// 形态② inst pin 层级路径名直配；pg 网与无本分区成员落点不计。对每个
// 分区成员 (inst, pin) 落点调用 sink，返回落点数。
template <typename Sink>
uint64_t resolve_override_targets(const WFDesignContext& ctx,
                                  const CMUnorderedSet<uint64_t>& members,
                                  const WFActivityOverride& ov, Sink&& sink) {
    uint64_t hits = 0;
    const DSPgNetSet* pg = ctx.pg_nets();
    const CMNetId net_id = ctx.net_mapper().get_global_id(ov.name_);
    if (net_id.is_valid()) {
        if (pg != nullptr && pg->is_pg(net_id)) {
            return 0;  // pg 网无活动语义
        }
        for (const auto& entry : ctx.all_partition_nets()) {
            auto net = entry.second->net_of(net_id).lock();
            if (net == nullptr) {
                continue;
            }
            for (const DSNetConnEntry& conn : net->connections_) {
                if (!(conn.is_driver() || conn.is_receiver()) ||
                    members.count(conn.inst_id_.value()) == 0) {
                    continue;
                }
                sink(conn.inst_id_, conn.pin_id_);
                ++hits;
            }
            break;  // 信号网 NETS 全量补全——首个含此网副本即完整连接表
        }
        return hits;
    }
    const CMString& name = ov.name_;
    const size_t last = name.find_last_of('/');
    if (last == CMString::npos || last + 1 == name.size()) {
        return 0;
    }
    const CMPinId pin_id =
        ctx.design().pin_names_.get_id(
            CMString(name.data() + last + 1, name.size() - last - 1));
    const CMInstanceId inst_id = ctx.inst_mapper().get_global_id(
        join_segments(split_segments(CMString(name.data(), last), '/')));
    if (pin_id.is_valid() && inst_id.is_valid() &&
        members.count(inst_id.value()) != 0) {
        sink(inst_id, pin_id);
        ++hits;
    }
    return hits;
}

}  // namespace

WFPartitionActivity wf_materialize_activity(
    const WFDesignContext& ctx, CMPartitionId pid,
    const CMVector<CMInstanceId>& inst_ids,
    const CMVector<WFActivityOverride>& overrides, uint64_t duration_fs,
    uint64_t& unresolved_count, CMVector<CMString>& resolved_names) {
    WFPartitionActivity out;
    out.part_id_ = pid;
    unresolved_count = 0;
    resolved_names.clear();
    const double duration_sec = static_cast<double>(duration_fs) * 1e-15;
    CMUnorderedSet<uint64_t> members;
    for (const CMInstanceId id : inst_ids) {
        members.insert(id.value());
    }
    for (const WFActivityOverride& ov : overrides) {
        const uint64_t hits = resolve_override_targets(
            ctx, members, ov,
            [&](CMInstanceId inst_id, CMPinId pin_id) {
                upsert_pin(out, inst_id, pin_id, ov, duration_sec, false);
            });
        if (hits > 0) {
            resolved_names.push_back(ov.name_);
        } else {
            ++unresolved_count;  // pg / 无连接位 / 无本区实例成员 / 双未命中
        }
    }
    return out;
}

uint64_t wf_apply_overrides(const WFDesignContext& ctx, CMPartitionId pid,
                            const CMVector<CMInstanceId>& inst_ids,
                            WFPartitionActivity& activity,
                            const CMVector<WFActivityOverride>& overrides,
                            uint64_t duration_fs,
                            CMVector<CMString>& applied_names) {
    activity.part_id_ = pid;
    applied_names.clear();
    uint64_t applied = 0;
    const double duration_sec = static_cast<double>(duration_fs) * 1e-15;
    CMUnorderedSet<uint64_t> members;
    for (const CMInstanceId id : inst_ids) {
        members.insert(id.value());
    }
    for (const WFActivityOverride& ov : overrides) {
        const uint64_t hits = resolve_override_targets(
            ctx, members, ov,
            [&](CMInstanceId inst_id, CMPinId pin_id) {
                upsert_pin(activity, inst_id, pin_id, ov, duration_sec, true);
            });
        if (hits > 0) {
            applied += hits;
            applied_names.push_back(ov.name_);
        }
    }
    return applied;
}

}  // namespace fly
