#include <emir/design/cpp/ds_types.h>

#include <message/cpp/message_macros.h>

#include <algorithm>
#include <cassert>
#include <functional>
#include <utility>

namespace fly {

// —— DSStack ——

uint32_t DSStack::add_layer(DSLayer&& layer) {
    CMString name = layer.name_;  // move 前留名建索引
    layers_.push_back(std::move(layer));
    const uint32_t id = static_cast<uint32_t>(layers_.size() - 1);
    // 层 id 分配回填（= 层表下标；随序列化持久化，读取不再依赖下标）
    layers_[id].set_id(id);
    // R7 ㊸：name → id 收编进 DSLayerNameHasher（序列化持久化，不再
    // 惰性重建）；重名保留首个（emplace 幂等语义）
    layer_names_.emplace(name);
    return id;
}

uint32_t DSStack::find_layer(const CMString& name) const {
    // R7 ㊸：直查序列化 hasher（原 layer_index_ 惰性重建已删除）
    return layer_names_.get_id(name);
}

uint32_t ds_resolve_layer_id(const CMString& name, const DSStack& stack) {
    const uint32_t id = stack.find_layer(name);
    if (id == DSStack::kNoLayer) {
        // 层引用未定义兜底（dev-rules §7：不 raise）：DSGN::0010 提醒 +
        // 返回哨兵，条目级丢弃决策与计数由调用方按条目类型执行
        MSG("DSGN::0010", 0,
            "undefined layer reference '{}' — entry dropped", name);
    }
    return id;
}

uint32_t DSStack::find_or_add_layer(DSLayer&& layer) {
    const uint32_t existing = find_layer(layer.name_);
    if (existing != kNoLayer) {
        return existing;
    }
    return add_layer(std::move(layer));
}

// —— DSPinTables（R4：按全局 pin id 组织）——

void DSPinTables::add_internal_power_tables(
    uint32_t pin_id, CMVector<CMLookupTable>&& tables) {
    internal_power_tables_[pin_id] = std::move(tables);
}

void DSPinTables::add_timing_tables(uint32_t pin_id,
                                    CMVector<CMLookupTable>&& tables) {
    timing_tables_[pin_id] = std::move(tables);
}

bool DSPinTables::pin_has_tables(uint32_t pin_id) const {
    return internal_power_tables_.contains(pin_id) ||
           timing_tables_.contains(pin_id);
}

const CMVector<CMLookupTable>* DSPinTables::internal_power_tables_of(
    uint32_t pin_id) const {
    auto it = internal_power_tables_.find(pin_id);
    return it == internal_power_tables_.end() ? nullptr : &it->second;
}

const CMVector<CMLookupTable>* DSPinTables::timing_tables_of(
    uint32_t pin_id) const {
    auto it = timing_tables_.find(pin_id);
    return it == timing_tables_.end() ? nullptr : &it->second;
}

// —— DSPinGeometry（R4：按全局 pin id 组织）——

void DSPinGeometry::add_geometry(uint32_t pin_id, DSShapeRef&& ref) {
    pin_geometry_[pin_id].push_back(std::move(ref));
}

void DSPinGeometry::add_geometries(uint32_t pin_id,
                                   CMVector<DSShapeRef>&& geos) {
    auto& vec = pin_geometry_[pin_id];
    vec.insert(vec.end(), std::make_move_iterator(geos.begin()),
               std::make_move_iterator(geos.end()));
}

bool DSPinGeometry::pin_has_geometry(uint32_t pin_id) const {
    return pin_geometry_.contains(pin_id);
}

const CMVector<DSShapeRef>* DSPinGeometry::geometry_of(
    uint32_t pin_id) const {
    auto it = pin_geometry_.find(pin_id);
    return it == pin_geometry_.end() ? nullptr : &it->second;
}

// —— DSDesign ——

uint32_t DSDesign::add_cell(DSCell&& cell) {
    const uint32_t id = cell_names_.emplace(cell.name_);
    cells_.push_back(std::move(cell));
    return id;
}

uint32_t DSDesign::add_via_cell(DSViaCell&& via) {
    const uint32_t id = via_cell_names_.emplace(via.name_);
    via_cells_.push_back(std::move(via));
    return id;
}

void DSDesign::register_pin(const CMString& cell_name,
                            const CMString& pin_name, uint32_t pin_id) {
    // pin id 全局平铺单调分配（D1）；hasher assign 指定 id 双写
    // （容忍 id 空洞，重名键覆盖——重挂语义）
    pin_names_.assign(cell_name + "/" + pin_name, pin_id);
}

// 按 id 直接落位（S5a 汇总专用，fake cell id 保持任务内分配值 ⑳）：
// 稀疏 resize 占位（id = 下标语义不变；空洞为空名占位 cell），hasher
// 同步注册。仅汇总任务串行调用。
void DSDesign::add_cell_at(uint32_t cell_id, DSCell&& cell) {
    if (cell_id >= cells_.size()) {
        cells_.resize(cell_id + 1);
    }
    cell_names_.assign(cell.name_, cell_id);
    cells_[cell_id] = std::move(cell);
}

const DSCell* DSDesign::find_cell(const CMString& name) const {
    const uint32_t id = cell_names_.get_id(name);
    if (!DSCellNameHasher::is_valid_id(id)) {
        return nullptr;
    }
    assert(id < cells_.size());
    return &cells_[id];
}

const DSViaCell* DSDesign::find_via_cell(const CMString& name) const {
    const uint32_t id = via_cell_names_.get_id(name);
    if (!DSViaCellNameHasher::is_valid_id(id)) {
        return nullptr;
    }
    assert(id < via_cells_.size());
    return &via_cells_[id];
}

CMString DSDesign::pin_name_of(uint32_t pin_id) const {
    // 组合键 "cell_name/pin_name" 反查取 pin 名段（㊱：pin 自身不存
    // name）；未登记/空洞返回空串
    if (!DSPinNameHasher::is_valid_id(pin_id) ||
        pin_id >= pin_names_.name_table_.size()) {
        return {};
    }
    const CMString& key = pin_names_.get_name(pin_id);
    const auto pos = key.rfind('/');
    return pos == CMString::npos ? key : key.substr(pos + 1);
}

DSCell& DSDesign::get_cell(uint32_t cell_id) {
    assert(cell_id < cells_.size());
    DSCell& cell = cells_[cell_id];
    // ⑰：指针注入（CMSharedPtr 拷贝即指针共享，表/几何数据零拷贝）。
    // 容器字段为空时同样注入（置空表示该 cell 无表/几何数据）。
    cell.set_pin_tables(pin_tables_);
    cell.set_pin_geometry(pin_geometry_);
    return cell;
}

const DSCell& DSDesign::get_cell(uint32_t cell_id) const {
    // 注入需改写 cell 的运行时字段（不序列化，逻辑 const——同
    // LIBLibrary::find_cell 的 const_cast 先例）
    return const_cast<DSDesign*>(this)->get_cell(cell_id);
}

CMVector<DSShapeRef> DSDesign::cell_pin_geometries(uint32_t cell_id) const {
    assert(cell_id < cells_.size());
    CMVector<DSShapeRef> out;
    if (pin_geometry_ == nullptr) {
        return out;
    }
    for (const auto& p : cells_[cell_id].pins_) {
        const auto* vec = pin_geometry_->geometry_of(p.get_pin_id());
        if (vec != nullptr) {
            out.insert(out.end(), vec->begin(), vec->end());
        }
    }
    return out;
}

// —— DSDensityGrid（S5a 实例面积通道）——

void DSDensityGrid::configure(int32_t origin_x, int32_t origin_y,
                              int32_t bin_width, int32_t bin_height,
                              uint32_t cols, uint32_t rows) {
    assert(bin_width > 0 && bin_height > 0);
    origin_x_ = origin_x;
    origin_y_ = origin_y;
    bin_width_ = bin_width;
    bin_height_ = bin_height;
    cols_ = cols;
    rows_ = rows;
    counts_.assign(static_cast<size_t>(cols) * rows, 0);
}

void DSDensityGrid::accumulate_footprint(
    const GEORect& footprint) {
    if (cols_ == 0 || rows_ == 0 || bin_width_ <= 0 || bin_height_ <= 0) {
        return;  // 未配置（DIEAREA 缺失兜底）：密度通道空转
    }
    assert(footprint.get_x_high() >= footprint.get_x_low() &&
           footprint.get_y_high() >= footprint.get_y_low());
    // 半开区间交叠的格下标范围（整数 floor 除，负数向 −∞ 取整——
    // footprint 越出格网原点时下标为负，下方 clamp 截断）：
    //   格 c 覆盖 [ox + c·bw, ox + (c+1)·bw)；交叠 ⇔
    //   c ∈ [floor((fp.x_low−ox)/bw), floor((fp.x_high−ox−1)/bw)]
    const auto floor_div = [](int64_t a, int64_t b) {
        const int64_t q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    };
    const int64_t col_lo = std::max<int64_t>(
        0, floor_div(footprint.get_x_low() - origin_x_, bin_width_));
    const int64_t col_hi = std::min<int64_t>(
        cols_ - 1,
        floor_div(static_cast<int64_t>(footprint.get_x_high()) - 1 - origin_x_,
                  bin_width_));
    const int64_t row_lo = std::max<int64_t>(
        0, floor_div(footprint.get_y_low() - origin_y_, bin_height_));
    const int64_t row_hi = std::min<int64_t>(
        rows_ - 1,
        floor_div(static_cast<int64_t>(footprint.get_y_high()) - 1 - origin_y_,
                  bin_height_));
    for (int64_t r = row_lo; r <= row_hi; ++r) {
        for (int64_t c = col_lo; c <= col_hi; ++c) {
            // 交叠前提校验（半开区间；共享边界线不算——GEORect::overlaps
            // 同语义；clamp 后逐格仍需确认，防 footprint 完全落在格网外）
            counts_[static_cast<size_t>(r) * cols_ + c] += 1;
        }
    }
}

int64_t DSDensityGrid::cell_count(uint32_t col, uint32_t row) const {
    if (col >= cols_ || row >= rows_) {
        return 0;
    }
    return counts_[static_cast<size_t>(row) * cols_ + col];
}

int64_t DSDensityGrid::total_count() const {
    int64_t total = 0;
    for (const int64_t v : counts_) {
        total += v;
    }
    return total;
}

// —— DSDensityGrid ⑥ 逐层分列通道（S5b 网内容）——

void DSDensityGrid::accumulate_layer_shape(uint32_t layer_id,
                                           bool via_channel,
                                           const GEORect& shape) {
    if (cols_ == 0 || rows_ == 0 || bin_width_ <= 0 || bin_height_ <= 0) {
        return;  // 未配置（DIEAREA 缺失兜底）：密度通道空转
    }
    auto& channel = via_channel ? via_layer_counts_[layer_id]
                                : metal_layer_counts_[layer_id];
    if (channel.empty()) {
        channel.assign(static_cast<size_t>(cols_) * rows_, 0);
    }
    // 交叠格下标口径与 accumulate_footprint 一致（半开区间、floor 除、
    // 越出原点截断）
    const auto floor_div = [](int64_t a, int64_t b) {
        const int64_t q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    };
    const int64_t col_lo = std::max<int64_t>(
        0, floor_div(shape.get_x_low() - origin_x_, bin_width_));
    const int64_t col_hi = std::min<int64_t>(
        cols_ - 1,
        floor_div(static_cast<int64_t>(shape.get_x_high()) - 1 - origin_x_,
                  bin_width_));
    const int64_t row_lo = std::max<int64_t>(
        0, floor_div(shape.get_y_low() - origin_y_, bin_height_));
    const int64_t row_hi = std::min<int64_t>(
        rows_ - 1,
        floor_div(static_cast<int64_t>(shape.get_y_high()) - 1 - origin_y_,
                  bin_height_));
    for (int64_t r = row_lo; r <= row_hi; ++r) {
        for (int64_t c = col_lo; c <= col_hi; ++c) {
            channel[static_cast<size_t>(r) * cols_ + c] += 1;
        }
    }
}

int64_t DSDensityGrid::layer_total(uint32_t layer_id, bool via_channel) const {
    const auto& map = via_channel ? via_layer_counts_ : metal_layer_counts_;
    auto it = map.find(layer_id);
    if (it == map.end()) {
        return 0;
    }
    int64_t total = 0;
    for (const int64_t v : it->second) {
        total += v;
    }
    return total;
}

int64_t DSDensityGrid::channel_total(bool via_channel) const {
    const auto& map = via_channel ? via_layer_counts_ : metal_layer_counts_;
    int64_t total = 0;
    for (const auto& entry : map) {
        for (const int64_t v : entry.second) {
            total += v;
        }
    }
    return total;
}

// —— DSNetBuildData（S5b per-DEF 网内容产物；R7 ㊳ via/net id 64 位）——

void DSNetBuildData::add_connection(uint64_t net_id, DSNetConnection&& conn) {
    connections_[net_id].push_back(std::move(conn));
    ++stats_.connection_count;
}

void DSNetBuildData::add_wire(uint64_t net_id, DSNetWire&& wire) {
    wires_[net_id].push_back(std::move(wire));
    ++stats_.wire_count;
}

void DSNetBuildData::add_rect(uint64_t net_id, DSNetRect&& rect) {
    rects_[net_id].push_back(std::move(rect));
    ++stats_.rect_count;
}

uint64_t DSNetBuildData::add_via_instance(uint64_t net_id,
                                          DSViaInstance&& via) {
    const uint64_t id = next_via_instance_id_++;
    via_instances_.emplace(id, std::move(via));
    net_via_ids_[net_id].push_back(id);
    ++stats_.via_instance_count;
    return id;
}

const CMVector<DSNetConnection>* DSNetBuildData::connections_of(
    uint64_t net_id) const {
    auto it = connections_.find(net_id);
    return it == connections_.end() ? nullptr : &it->second;
}

const CMVector<DSNetWire>* DSNetBuildData::wires_of(uint64_t net_id) const {
    auto it = wires_.find(net_id);
    return it == wires_.end() ? nullptr : &it->second;
}

const CMVector<DSNetRect>* DSNetBuildData::rects_of(uint64_t net_id) const {
    auto it = rects_.find(net_id);
    return it == rects_.end() ? nullptr : &it->second;
}

const CMVector<uint64_t>* DSNetBuildData::via_ids_of(uint64_t net_id) const {
    auto it = net_via_ids_.find(net_id);
    return it == net_via_ids_.end() ? nullptr : &it->second;
}

const DSViaInstance* DSNetBuildData::via_instance_at(uint64_t via_id) const {
    auto it = via_instances_.find(via_id);
    return it == via_instances_.end() ? nullptr : &it->second;
}

// —— DSBlockBuildData（S5a per-DEF 产物）——

void DSBlockBuildData::init_placeholder(const CMString& block_name,
                                        uint32_t block_cell_id) {
    if (!instances_.empty()) {
        return;  // 幂等：占位已就位
    }
    ensure_names();  // 解析期惰性创建运行时 hasher（㊵②）
    block_name_ = block_name;
    DSInstance placeholder;
    placeholder.set_cell_id(block_cell_id);
    instances_.emplace(0, std::move(placeholder));  // ⑧ local 0 占位
    next_instance_id_ = 1;
}

uint64_t DSBlockBuildData::add_instance(DSInstance&& inst,
                                        const CMString& name) {
    // ㊳ 64 位 local id（⑧ 从 1 起）；实例名登记双向 hasher（R7 ㊱：
    // DSInstance 不存 name）。重名防御性覆盖（DEF 语义保证唯一）。
    const uint64_t id = next_instance_id_++;
    instance_names_->assign(name, id);
    instances_.emplace(id, std::move(inst));
    return id;
}

uint64_t DSBlockBuildData::register_net(const CMString& name) {
    // 重名保留首份（同 DEF 内 NETS/SPECIALNETS 跨段重名兜底，⑨ local
    // id 从 1 起）
    ensure_names();
    const uint64_t existing = net_names_->get_id(name);
    if (DSNetNameHasher::is_valid_id(existing)) {
        return existing;
    }
    const uint64_t id = next_net_id_++;
    net_names_->assign(name, id);
    return id;
}

const DSInstance* DSBlockBuildData::find_instance(uint64_t id) const {
    auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : &it->second;
}

const DSInstance* DSBlockBuildData::find_instance_by_name(
    const CMString& name) const {
    // ㊱：经双向 instance hasher 查名（需已注入 DSBlockNames）
    if (!instance_names_) {
        return nullptr;
    }
    const uint64_t id = instance_names_->get_id(name);
    return DSInstanceNameHasher::is_valid_id(id) ? find_instance(id)
                                                 : nullptr;
}

std::optional<CMString> DSBlockBuildData::net_name_at(uint64_t local_id) const {
    // 域判别走 name_domain（R8d 形态感知：形态一 = 偏移表规模、形态二 =
    // id→rank 表规模——封口后偏移表已释放，直接读会恒判越界）
    if (!net_names_ || !DSNetNameHasher::is_valid_id(local_id) ||
        local_id >= net_names_->name_domain()) {
        return std::nullopt;
    }
    // 空洞返回空串（语义同 R7 空名占位——与 nullopt 可区分）
    return net_names_->get_name(local_id);
}


// —— DSHierTree（S6 层级树；⑧⑨⑮；R7 ㊳ global id 64 位）——

// 区间反查的公共二分：nodes_ 为 DFS 前序（= 下标序），同 kind 的 start
// 序单调不减（零长区间可能并列）；取「最后一个 start ≤ id」的节点后校
// 验半开区间，零长（via 占位）与越界返回 kNoNode。
static uint32_t hier_block_of(const CMVector<DSHierNode>& nodes,
                              uint64_t DSHierNode::*start,
                              uint64_t DSHierNode::*count, uint64_t id) {
    size_t lo = 0;
    size_t hi = nodes.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (nodes[mid].*start <= id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return DSHierTree::kNoNode;
    }
    const DSHierNode& n = nodes[lo - 1];
    if (n.*count > 0 && id < n.*start + n.*count) {
        return n.get_id();
    }
    return DSHierTree::kNoNode;
}

uint32_t DSHierTree::block_of_instance(uint64_t global_id) const {
    return hier_block_of(nodes_, &DSHierNode::instance_start_,
                         &DSHierNode::instance_count_, global_id);
}

uint32_t DSHierTree::block_of_net(uint64_t global_id) const {
    return hier_block_of(nodes_, &DSHierNode::net_start_,
                         &DSHierNode::net_count_, global_id);
}

uint32_t DSHierTree::block_of_via_instance(uint64_t global_id) const {
    return hier_block_of(nodes_, &DSHierNode::via_start_,
                         &DSHierNode::via_count_, global_id);
}

std::pair<uint64_t, uint64_t> DSHierTree::instance_range(
    uint32_t node_id) const {
    if (node_id >= nodes_.size()) {
        return {0, 0};
    }
    return {nodes_[node_id].instance_start_, nodes_[node_id].instance_count_};
}

std::pair<uint64_t, uint64_t> DSHierTree::net_range(uint32_t node_id) const {
    if (node_id >= nodes_.size()) {
        return {0, 0};
    }
    return {nodes_[node_id].net_start_, nodes_[node_id].net_count_};
}

std::pair<uint64_t, uint64_t> DSHierTree::via_range(uint32_t node_id) const {
    if (node_id >= nodes_.size()) {
        return {0, 0};
    }
    return {nodes_[node_id].via_start_, nodes_[node_id].via_count_};
}

uint32_t DSHierTree::find_child_by_instance_name(uint32_t node_id,
                                                 const CMString& name) const {
    assert(node_id < nodes_.size());
    // 线性扫直系 children（R7：DSNameMapperT 树下降辅助；层级扇出小）
    for (const uint32_t child : nodes_[node_id].children_ids_) {
        if (nodes_[child].instance_name_ == name) {
            return child;
        }
    }
    return kNoNode;
}

CMString DSHierTree::format_tree() const {
    CMString out;
    // 递归以 name 打印（⑮）：缩进 2 空格/层 + 三类区间
    const std::function<void(uint32_t, int)> emit =
        [&](uint32_t id, int depth) {
            const DSHierNode& n = nodes_[id];
            for (int i = 0; i < depth; ++i) {
                out += "  ";
            }
            out += n.get_block_cell_name() + " as " + n.get_instance_name() +
                   " id=" + std::to_string(n.get_id()) + " inst=[" +
                   std::to_string(n.instance_start_) + "," +
                   std::to_string(n.instance_start_ + n.instance_count_) +
                   ") net=[" + std::to_string(n.net_start_) + "," +
                   std::to_string(n.net_start_ + n.net_count_) + ") via=[" +
                   std::to_string(n.via_start_) + "," +
                   std::to_string(n.via_start_ + n.via_count_) + ")\n";
            for (const uint32_t child : n.get_children_ids()) {
                emit(child, depth + 1);
            }
        };
    if (!nodes_.empty()) {
        emit(0, 0);
    }
    return out;
}

uint64_t DSHierTree::global_instance_id(uint32_t node_id,
                                        uint64_t local_id) const {
    if (node_id >= nodes_.size()) {
        return kNoNode;
    }
    const DSHierNode& n = nodes_[node_id];
    if (local_id == 0) {
        return n.get_self_global_id();  // ⑧ local 0 = block 自身占位
    }
    if (local_id >= n.instance_count_) {
        return kNoNode;
    }
    return n.instance_start_ + local_id;
}

uint64_t DSHierTree::global_net_id(uint32_t node_id, uint64_t local_id) const {
    if (node_id >= nodes_.size() || local_id == 0 ||
        local_id > nodes_[node_id].net_count_) {
        return kNoNode;  // net local id 从 1 起，0 保留未用
    }
    return nodes_[node_id].net_start_ + local_id - 1;
}

uint64_t DSHierTree::global_via_instance_id(uint32_t node_id,
                                            uint64_t local_id) const {
    if (node_id >= nodes_.size() || local_id == 0 ||
        local_id > nodes_[node_id].via_count_) {
        return kNoNode;
    }
    return nodes_[node_id].via_start_ + local_id - 1;
}

}  // namespace fly
