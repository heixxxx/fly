#include <emir/design/cpp/ds_types.h>

#include <message/cpp/message_macros.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <utility>

namespace fly {

// —— 网 USE 解析（2026-09-13 全量补收裁定；ds_types.h 声明）——
// DEF NET USE 语句规范全集八值精确匹配（大小写保留，namemap 同口径）；
// 未知值/空文本 → SIGNAL 兜底 + known 出参上报（调用方计数 + DSGN::0026
// 提醒，不 raise——dev-rules §7 同 fake cell/未定义 via 的先例语义）。

namespace {

struct DSNetUseEntry {
    const char* name;
    DSNetUse use;
};

constexpr DSNetUseEntry kNetUseTable[] = {
    {"SIGNAL", DSNetUse::SIGNAL}, {"POWER", DSNetUse::POWER},
    {"GROUND", DSNetUse::GROUND}, {"CLOCK", DSNetUse::CLOCK},
    {"TIEOFF", DSNetUse::TIEOFF}, {"ANALOG", DSNetUse::ANALOG},
    {"RESET", DSNetUse::RESET},   {"SCAN", DSNetUse::SCAN},
};

}  // namespace

DSNetUse ds_parse_net_use(const char* text, bool* known) {
    if (known != nullptr) {
        *known = false;
    }
    if (text == nullptr) {
        return DSNetUse::SIGNAL;
    }
    for (const DSNetUseEntry& e : kNetUseTable) {
        if (std::strcmp(text, e.name) == 0) {
            if (known != nullptr) {
                *known = true;
            }
            return e.use;
        }
    }
    return DSNetUse::SIGNAL;  // 未知值兜底（dev-rules §7，不 raise）
}

const char* ds_net_use_name(DSNetUse use) {
    const auto index = static_cast<size_t>(use);
    if (index < sizeof(kNetUseTable) / sizeof(kNetUseTable[0])) {
        return kNetUseTable[index].name;
    }
    return "SIGNAL";  // 越域整型兜底（防御；正常路径不可达）
}

// —— DSStack ——

CMLayerId DSStack::add_layer(DSLayer&& layer) {
    CMString name = layer.name_;  // move 前留名建索引
    layers_.push_back(std::move(layer));
    const uint32_t index = static_cast<uint32_t>(layers_.size() - 1);
    // 层 id 分配回填（= 层表下标；随序列化持久化，读取不再依赖下标）
    layers_[index].set_id(CMLayerId{index});
    // R7 ㊸：name → id 收编进 DSLayerNameHasher（序列化持久化，不再
    // 惰性重建）；重名保留首个（emplace 幂等语义）
    layer_names_.emplace(name);
    return CMLayerId{index};
}

CMLayerId DSStack::find_layer(const CMString& name) const {
    // R7 ㊴：直查序列化 hasher（原 layer_index_ 惰性重建已删除）；hasher
    // 底座强类型化后 get_id 即返回 CMLayerId（值域不变）
    return layer_names_.get_id(name);
}

CMLayerId ds_resolve_layer_id(const CMString& name, const DSStack& stack) {
    const CMLayerId id = stack.find_layer(name);
    if (!id.is_valid()) {
        // 层引用未定义兜底（dev-rules §7：不 raise）：DSGN::0010 提醒 +
        // 返回哨兵，条目级丢弃决策与计数由调用方按条目类型执行
        MSG("DSGN::0010", 0,
            "undefined layer reference '{}' — entry dropped", name);
    }
    return id;
}

CMLayerId DSStack::find_or_add_layer(DSLayer&& layer) {
    const CMLayerId existing = find_layer(layer.name_);
    if (existing.is_valid()) {
        return existing;
    }
    return add_layer(std::move(layer));
}

// —— DSPinTables（2026-09-16 裁定 3：按 (cell id, pin id) 组织——同名
// pin 跨 cell 共享 id 后表是 (cell, pin) 属性）——

void DSPinTables::add_internal_power_tables(
    CMCellId cell_id, CMPinId pin_id, CMVector<CMLookupTable>&& tables) {
    internal_power_tables_[cell_id][pin_id] = std::move(tables);
}

void DSPinTables::add_timing_tables(CMCellId cell_id, CMPinId pin_id,
                                    CMVector<CMLookupTable>&& tables) {
    timing_tables_[cell_id][pin_id] = std::move(tables);
}

bool DSPinTables::pin_has_tables(CMCellId cell_id, CMPinId pin_id) const {
    const auto cit = internal_power_tables_.find(cell_id);
    if (cit != internal_power_tables_.end() && cit->second.contains(pin_id)) {
        return true;
    }
    const auto cit2 = timing_tables_.find(cell_id);
    return cit2 != timing_tables_.end() && cit2->second.contains(pin_id);
}

const CMVector<CMLookupTable>* DSPinTables::internal_power_tables_of(
    CMCellId cell_id, CMPinId pin_id) const {
    const auto cit = internal_power_tables_.find(cell_id);
    if (cit == internal_power_tables_.end()) {
        return nullptr;
    }
    auto it = cit->second.find(pin_id);
    return it == cit->second.end() ? nullptr : &it->second;
}

const CMVector<CMLookupTable>* DSPinTables::timing_tables_of(
    CMCellId cell_id, CMPinId pin_id) const {
    const auto cit = timing_tables_.find(cell_id);
    if (cit == timing_tables_.end()) {
        return nullptr;
    }
    auto it = cit->second.find(pin_id);
    return it == cit->second.end() ? nullptr : &it->second;
}

// —— DSPinGeometry（2026-09-16 裁定 3：按 (cell id, pin id) 组织）——

void DSPinGeometry::add_geometry(CMCellId cell_id, CMPinId pin_id,
                                 DSShapeRef&& ref) {
    cell_pin_geometry_[cell_id][pin_id].push_back(std::move(ref));
}

void DSPinGeometry::add_geometries(CMCellId cell_id, CMPinId pin_id,
                                   CMVector<DSShapeRef>&& geos) {
    auto& vec = cell_pin_geometry_[cell_id][pin_id];
    vec.insert(vec.end(), std::make_move_iterator(geos.begin()),
               std::make_move_iterator(geos.end()));
}

bool DSPinGeometry::pin_has_geometry(CMCellId cell_id, CMPinId pin_id) const {
    const auto cit = cell_pin_geometry_.find(cell_id);
    return cit != cell_pin_geometry_.end() && cit->second.contains(pin_id);
}

const CMVector<DSShapeRef>* DSPinGeometry::geometry_of(
    CMCellId cell_id, CMPinId pin_id) const {
    const auto cit = cell_pin_geometry_.find(cell_id);
    if (cit == cell_pin_geometry_.end()) {
        return nullptr;
    }
    auto it = cit->second.find(pin_id);
    return it == cit->second.end() ? nullptr : &it->second;
}

// —— DSDesign ——

CMCellId DSDesign::add_cell(DSCell&& cell) {
    const CMCellId id = cell_names_.emplace(cell.name_);
    cells_.push_back(std::move(cell));
    return id;
}

CMViaCellId DSDesign::add_via_cell(DSViaCell&& via) {
    const CMViaCellId id = via_cell_names_.emplace(via.name_);
    via_cells_.push_back(std::move(via));
    return id;
}

CMPinId DSDesign::register_pin(const CMString& pin_name) {
    // 2026-09-16 裁定 3：键 = 裸 pin 名，同名保留首份返回既有 id（幂等
    // 分配）；未命中分配新 id（= 已登记名数）并双写
    const CMPinId existing = pin_names_.get_id(pin_name);
    if (existing.is_valid()) {
        return existing;
    }
    const CMPinId id{static_cast<CMPinId::int_type>(pin_names_.size())};
    pin_names_.assign(pin_name, id);
    return id;
}

// 按 id 直接落位（S5a 汇总专用，fake cell id 保持任务内分配值 ⑳）：
// 稀疏 resize 占位（id = 下标语义不变；空洞为空名占位 cell），hasher
// 同步注册。仅汇总任务串行调用。
void DSDesign::add_cell_at(CMCellId cell_id, DSCell&& cell) {
    if (cell_id >= cells_.size()) {
        cells_.resize(cell_id.value() + 1);
    }
    cell_names_.assign(cell.name_, cell_id);
    cells_[cell_id.value()] = std::move(cell);
}

const DSCell* DSDesign::find_cell(const CMString& name) const {
    const CMCellId id = cell_names_.get_id(name);
    if (!id.is_valid()) {
        return nullptr;
    }
    assert(id < cells_.size());
    return &cells_[id.value()];
}

const DSViaCell* DSDesign::find_via_cell(const CMString& name) const {
    const CMViaCellId id = via_cell_names_.get_id(name);
    if (!id.is_valid()) {
        return nullptr;
    }
    assert(id < via_cells_.size());
    return &via_cells_[id.value()];
}

CMString DSDesign::pin_name_of(CMPinId pin_id) const {
    // pin hasher 直查（2026-09-16 裁定 3：键即裸 pin 名）；未登记/空洞
    // 返回空串
    if (!pin_id.is_valid() || pin_id >= pin_names_.name_table_.size()) {
        return {};
    }
    return pin_names_.get_name(pin_id);
}

DSCell& DSDesign::get_cell(CMCellId cell_id) {
    assert(cell_id < cells_.size());
    DSCell& cell = cells_[cell_id.value()];
    // ⑰：指针注入（CMSharedPtr 拷贝即指针共享，表/几何数据零拷贝）。
    // 容器字段为空时同样注入（置空表示该 cell 无表/几何数据）。
    cell.set_pin_tables(pin_tables_);
    cell.set_pin_geometry(pin_geometry_);
    return cell;
}

const DSCell& DSDesign::get_cell(CMCellId cell_id) const {
    // 注入需改写 cell 的运行时字段（不序列化，逻辑 const——同
    // LIBLibrary::find_cell 的 const_cast 先例）
    return const_cast<DSDesign*>(this)->get_cell(cell_id);
}

CMVector<DSShapeRef> DSDesign::cell_pin_geometries(CMCellId cell_id) const {
    assert(cell_id < cells_.size());
    CMVector<DSShapeRef> out;
    if (pin_geometry_ == nullptr) {
        return out;
    }
    for (const auto& p : cells_[cell_id.value()].pins_) {
        const auto* vec =
            pin_geometry_->geometry_of(cell_id, p.get_pin_id());
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

void DSDensityGrid::accumulate_layer_shape(CMLayerId layer_id,
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

int64_t DSDensityGrid::layer_total(CMLayerId layer_id,
                                   bool via_channel) const {
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

void DSNetBuildData::add_connection(CMNetId net_id, DSNetConnection&& conn) {
    auto& vec = connections_[net_id];
    if (!vec) {
        vec = std::make_shared<CMVector<DSNetConnection>>();
    }
    vec->push_back(std::move(conn));
    ++stats_.connection_count;
}

void DSNetBuildData::add_wire(CMNetId net_id, DSNetWire&& wire) {
    auto& vec = wires_[net_id];
    if (!vec) {
        vec = std::make_shared<CMVector<DSNetWire>>();
    }
    vec->push_back(std::move(wire));
    ++stats_.wire_count;
}

void DSNetBuildData::add_rect(CMNetId net_id, DSNetRect&& rect) {
    auto& vec = rects_[net_id];
    if (!vec) {
        vec = std::make_shared<CMVector<DSNetRect>>();
    }
    vec->push_back(std::move(rect));
    ++stats_.rect_count;
}

CMViaInstanceId DSNetBuildData::add_via_instance(CMNetId net_id,
                                                 DSViaInstance&& via) {
    const CMViaInstanceId id = next_via_instance_id_++;
    via_instances_[id] = std::make_shared<DSViaInstance>(std::move(via));
    auto& ids = net_via_ids_[net_id];
    if (!ids) {
        ids = std::make_shared<CMVector<CMViaInstanceId>>();
    }
    ids->push_back(id);
    ++stats_.via_instance_count;
    return id;
}

CMWeakPtr<const CMVector<DSNetConnection>> DSNetBuildData::connections_of(
    CMNetId net_id) const {
    auto it = connections_.find(net_id);
    return it == connections_.end() ? CMWeakPtr<const CMVector<DSNetConnection>>{}
                                    : CMWeakPtr<const CMVector<DSNetConnection>>{it->second};
}

CMWeakPtr<const CMVector<DSNetWire>> DSNetBuildData::wires_of(
    CMNetId net_id) const {
    auto it = wires_.find(net_id);
    return it == wires_.end() ? CMWeakPtr<const CMVector<DSNetWire>>{}
                              : CMWeakPtr<const CMVector<DSNetWire>>{it->second};
}

CMWeakPtr<const CMVector<DSNetRect>> DSNetBuildData::rects_of(
    CMNetId net_id) const {
    auto it = rects_.find(net_id);
    return it == rects_.end() ? CMWeakPtr<const CMVector<DSNetRect>>{}
                              : CMWeakPtr<const CMVector<DSNetRect>>{it->second};
}

CMWeakPtr<const CMVector<CMViaInstanceId>> DSNetBuildData::via_ids_of(
    CMNetId net_id) const {
    auto it = net_via_ids_.find(net_id);
    return it == net_via_ids_.end()
               ? CMWeakPtr<const CMVector<CMViaInstanceId>>{}
               : CMWeakPtr<const CMVector<CMViaInstanceId>>{it->second};
}

CMWeakPtr<const DSViaInstance> DSNetBuildData::via_instance_at(
    CMViaInstanceId via_id) const {
    auto it = via_instances_.find(via_id);
    return it == via_instances_.end() ? CMWeakPtr<const DSViaInstance>{}
                                      : CMWeakPtr<const DSViaInstance>{it->second};
}

// —— DSBlockBuildData（S5a per-DEF 产物）——

void DSBlockBuildData::init_placeholder(const CMString& block_name,
                                        CMCellId block_cell_id) {
    if (!instances_.empty()) {
        return;  // 幂等：占位已就位
    }
    ensure_names();  // 解析期惰性创建运行时 hasher（㊵②）
    block_name_ = block_name;
    auto placeholder = std::make_shared<DSInstance>();
    placeholder->set_cell_id(block_cell_id);
    instances_.emplace(CMInstanceId{0}, std::move(placeholder));  // ⑧ 占位
    next_instance_id_ = CMInstanceId{1};
}

CMInstanceId DSBlockBuildData::add_instance(DSInstance&& inst,
                                            const CMString& name) {
    // ㊳ 64 位 local id（⑧ 从 1 起）；实例名登记双向 hasher（R7 ㊱：
    // DSInstance 不存 name）。重名防御性覆盖（DEF 语义保证唯一）。
    // hasher 底座强类型化（审计 B-4）：assign 的 id 参数 = CMInstanceId，
    // 值域与裸值形态逐位同宽
    const CMInstanceId id = next_instance_id_++;
    instance_names_->assign(name, id);
    instances_.emplace(id, std::make_shared<DSInstance>(std::move(inst)));
    return id;
}

CMNetId DSBlockBuildData::register_net(const CMString& name) {
    // 重名保留首份（同 DEF 内 NETS/SPECIALNETS 跨段重名兜底，⑨ local
    // id 从 1 起）
    ensure_names();
    const CMNetId existing = net_names_->get_id(name);
    if (existing.is_valid()) {
        return existing;
    }
    const CMNetId id = next_net_id_++;
    net_names_->assign(name, id);
    return id;
}

CMWeakPtr<const DSInstance> DSBlockBuildData::find_instance(
    CMInstanceId id) const {
    auto it = instances_.find(id);
    if (it == instances_.end()) {
        return {};
    }
    return CMWeakPtr<const DSInstance>{it->second};
}

CMWeakPtr<const DSInstance> DSBlockBuildData::find_instance_by_name(
    const CMString& name) const {
    // ㊱：经双向 instance hasher 查名（需已注入 DSBlockNames）
    if (!instance_names_) {
        return {};
    }
    const CMInstanceId id = instance_names_->get_id(name);
    return id.is_valid() ? find_instance(id) : CMWeakPtr<const DSInstance>{};
}

std::optional<CMString> DSBlockBuildData::net_name_at(CMNetId local_id) const {
    // 域判别走 name_domain（R8d 形态感知：形态一 = 偏移表规模、形态二 =
    // id→rank 表规模——封口后偏移表已释放，直接读会恒判越界）
    if (!net_names_ || !local_id.is_valid() ||
        local_id >= net_names_->name_domain()) {
        return std::nullopt;
    }
    // 空洞返回空串（语义同 R7 空名占位——与 nullopt 可区分）
    return net_names_->get_name(local_id);
}


// —— DSHierTree（S6 层级树；⑧⑨⑮；R7 ㊳ global id 64 位强类型）——

// 区间反查的公共二分：nodes_ 为 DFS 前序（= 下标序），同 kind 的 start
// 序单调不减（零长区间可能并列）；取「最后一个 start ≤ id」的节点后校
// 验半开区间，零长（via 占位）与越界返回 kNoNode。StartT = 各实体的强
// 类型 id 区间字段（CMInstanceId/CMNetId/CMViaInstanceId），count 保持
// 裸整型（计数不是编号），id 传裸值（机器内比较经强类型比较重载）
template <typename StartT>
uint32_t hier_block_of(const CMVector<DSHierNode>& nodes,
                       StartT DSHierNode::*start,
                       uint64_t DSHierNode::*count,
                       decltype(std::declval<StartT>().value()) id) {
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
    if (n.*count > 0 && id < (n.*start) + n.*count) {
        return n.get_id();
    }
    return DSHierTree::kNoNode;
}

uint32_t DSHierTree::block_of_instance(CMInstanceId global_id) const {
    return hier_block_of(nodes_, &DSHierNode::instance_start_,
                         &DSHierNode::instance_count_, global_id.value());
}

uint32_t DSHierTree::block_of_net(CMNetId global_id) const {
    return hier_block_of(nodes_, &DSHierNode::net_start_,
                         &DSHierNode::net_count_, global_id.value());
}

uint32_t DSHierTree::block_of_via_instance(CMViaInstanceId global_id) const {
    return hier_block_of(nodes_, &DSHierNode::via_start_,
                         &DSHierNode::via_count_, global_id.value());
}

std::pair<CMInstanceId, uint64_t> DSHierTree::instance_range(
    uint32_t node_id) const {
    if (node_id >= nodes_.size()) {
        return {CMInstanceId{0}, 0};
    }
    return {nodes_[node_id].instance_start_, nodes_[node_id].instance_count_};
}

std::pair<CMNetId, uint64_t> DSHierTree::net_range(uint32_t node_id) const {
    if (node_id >= nodes_.size()) {
        return {CMNetId{0}, 0};
    }
    return {nodes_[node_id].net_start_, nodes_[node_id].net_count_};
}

std::pair<CMViaInstanceId, uint64_t> DSHierTree::via_range(
    uint32_t node_id) const {
    if (node_id >= nodes_.size()) {
        return {CMViaInstanceId{0}, 0};
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
    // 递归以 name 打印（⑮）：缩进 2 空格/层 + 三类区间。root 实例名恒
    // 空串（2026-09-16 裁定 2）——打印用 "(top)" 占位，不改存储
    const std::function<void(uint32_t, int)> emit =
        [&](uint32_t id, int depth) {
            const DSHierNode& n = nodes_[id];
            for (int i = 0; i < depth; ++i) {
                out += "  ";
            }
            const CMString shown_name =
                id == 0 ? CMString("(top)") : n.get_instance_name();
            out += n.get_block_cell_name() + " as " + shown_name +
                   " id=" + std::to_string(n.get_id()) + " inst=[" +
                   std::to_string(n.instance_start_.value()) + "," +
                   std::to_string(
                       (n.instance_start_ + n.instance_count_).value()) +
                   ") net=[" + std::to_string(n.net_start_.value()) + "," +
                   std::to_string((n.net_start_ + n.net_count_).value()) +
                   ") via=[" + std::to_string(n.via_start_.value()) + "," +
                   std::to_string((n.via_start_ + n.via_count_).value()) +
                   ")\n";
            for (const uint32_t child : n.get_children_ids()) {
                emit(child, depth + 1);
            }
        };
    if (!nodes_.empty()) {
        emit(0, 0);
    }
    return out;
}

CMInstanceId DSHierTree::global_instance_id(uint32_t node_id,
                                            CMInstanceId local_id) const {
    if (node_id >= nodes_.size()) {
        return CMInstanceId{};  // 哨兵（未用 local/越界）
    }
    const DSHierNode& n = nodes_[node_id];
    if (local_id == CMInstanceId{0}) {
        return n.get_self_global_id();  // ⑧ local 0 = block 自身占位
    }
    if (local_id >= n.instance_count_) {
        return CMInstanceId{};
    }
    return n.instance_start_ + local_id;
}

CMNetId DSHierTree::global_net_id(uint32_t node_id, CMNetId local_id) const {
    if (node_id >= nodes_.size() || local_id == CMNetId{0} ||
        local_id >= nodes_[node_id].net_count_) {
        return CMNetId{};  // local 0 = 空洞位（每块一个、不映射真网——
                           // root 块空洞位 = global 0 = OBS 专属位）
    }
    // global = start + local（2026-09-14 裁定：区间长度含 local 0 空洞位，
    // 直接相加无 −1；真网占 [start+1, start+count)，与 instance 的
    // start + local 形态同构）
    return nodes_[node_id].net_start_ + local_id;
}

CMViaInstanceId DSHierTree::global_via_instance_id(
    uint32_t node_id, CMViaInstanceId local_id) const {
    if (node_id >= nodes_.size() || local_id == CMViaInstanceId{0} ||
        local_id > nodes_[node_id].via_count_) {
        return CMViaInstanceId{};
    }
    // start + (local − 1)：同类减法返回裸差值，先算差再回强类型平移
    return nodes_[node_id].via_start_ + (local_id.value() - 1);
}

}  // namespace fly
