// _fly_emir_design.so — emir design 模块的 Python 绑定入口（T3 骨架）。
// 导出 DSDesign/DSStack 等数据结构（FLY_EXPORT_SERIALIZE_PICKLE 支持
// pickle 落 db）。容器一律只读 count/按索引访问（def_rw 拷贝语义陷阱，
// 裁定 15 先例）；几何字段（GEORect/DSShapeRef）骨架期以 tuple 透出，
// 不绑通用 geometry 库类型（后续批次按需补 EXGEORect）；运行时注入字段
// （pin_tables_/pin_geometry_，裁定 ⑰/⑱）不经 Python 面——表数值消费
// 在 C++ 计算引擎，Python 编排层仅需结构完备性可观测。
// R5：port/block 复用（EXDSPort/EXDSBlock 删除——port 走 EXDSPin 的
// is_port、block 走 EXDSCell 的 is_block_cell + bbox/polygon）；VIARULE
// 删除（EXDSViaRule 删除，展开产物为 EXDSViaCell）。
// R6：EXDSInstance（pos/orient 二元组只读面）。
// R7 name 体系收敛（㊱㊳㊴㊸㊹㊻）：①EXDSPin/EXDSInstance 删 name 面
//（name 分层存储，查询经 hasher/mapper）；②EXDSBlockNames（㊵② 伴生
// 对象：instance/net 两 hasher，DSBlockNames_<i> 独立落盘的 Python 面）
// + EXDSNameMapper（㊻ 注入式轻壳 DSNameMapperT<uint64_t>，运行时构造
// 不落盘；DSInstanceNameMapper/DSNetNameMapper 同型异 kind）+
// ds_make_name_mapper 统一组装工厂；③id 64 位化——instance/net/via
// instance id 与层级树区间/self_global_id 的接口参数一律 uint64_t。
// 责任链（DSInstancePipeline/DSNetPipeline 含 unique_ptr 不可绑定）：
// 节点装配在 C++ 工厂 ds_make_components_pipeline/ds_make_nets_pipeline，
// 解析入口内部消费。
#include <export/cpp/export_macros.h>
#include <emir/design/cpp/ds_def_adapter.h>
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_instance_pipeline.h>
#include <emir/design/cpp/ds_lef_adapter.h>
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_name_hasher.h>
#include <emir/design/cpp/ds_name_mapper.h>
#include <emir/design/cpp/ds_net_pipeline.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/design/cpp/ds_union.h>
#include <emir/lib/cpp/lib_types.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <utility>

namespace nb = nanobind;

namespace {

// GEORectT<T> 以四元组透出（骨架期约定：x_low/y_low/x_high/y_high）
template <typename T>
nb::object rect_to_tuple(const fly::GEORectT<T>& r) {
    return nb::make_tuple(r.get_x_low(), r.get_y_low(), r.get_x_high(),
                          r.get_y_high());
}

// DSShapeRef 以二元组透出：(layer_id, (x_low, y_low, x_high, y_high))
nb::object geometry_ref_to_tuple(const fly::DSShapeRef& g) {
    return nb::make_tuple(
        g.get_layer_id(),
        nb::make_tuple(g.get_rect().get_x_low(), g.get_rect().get_y_low(),
                       g.get_rect().get_x_high(), g.get_rect().get_y_high()));
}

}  // namespace

FLY_EXPORT_MODULE(_fly_emir_design) {

FLY_EXPORT_CLASS(fly::DSLayer, "EXDSLayer")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("name", &fly::DSLayer::name_)
    FLY_EXPORT_READONLY_ATTR("id", &fly::DSLayer::id_)
    FLY_EXPORT_READONLY_ATTR("type", &fly::DSLayer::type_)
    FLY_EXPORT_READONLY_ATTR("direction", &fly::DSLayer::direction_)
    FLY_EXPORT_READONLY_ATTR("default_width", &fly::DSLayer::default_width_)
    FLY_EXPORT_READONLY_ATTR("pitch", &fly::DSLayer::pitch_)
    FLY_EXPORT_READONLY_ATTR("min_area", &fly::DSLayer::min_area_)
    FLY_EXPORT_READONLY_PROPERTY("spacing_count", [](const fly::DSLayer& l) {
        return static_cast<int>(l.spacing_.size());
    })
    FLY_EXPORT_DEF("spacing_at", [](const fly::DSLayer& l, uint32_t i) {
        return l.spacing_.at(i);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSLayer);

FLY_EXPORT_CLASS(fly::DSStack, "EXDSStack")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("dbu_basis", &fly::DSStack::dbu_basis_)
    FLY_EXPORT_READONLY_ATTR("dbu_per_micron", &fly::DSStack::dbu_per_micron_)
    FLY_EXPORT_READONLY_ATTR("manufacturing_grid",
                             &fly::DSStack::manufacturing_grid_)
    FLY_EXPORT_READONLY_PROPERTY("layer_count", [](const fly::DSStack& s) {
        return static_cast<int>(s.layer_count());
    })
    FLY_EXPORT_DEF("layer_by_id", [](const fly::DSStack& s, uint32_t id) -> const fly::DSLayer& {
        return s.layer_by_id(id);
    }, nb::rv_policy::reference_internal)
    // 旧名保留，语义同 layer_by_id
    FLY_EXPORT_DEF("layer_at", [](const fly::DSStack& s, uint32_t id) -> const fly::DSLayer& {
        return s.layer_at(id);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("find_layer", [](const fly::DSStack& s, const CMString& name) {
        // 未命中返回 None（kNoLayer 不透出到 Python 面）
        uint32_t id = s.find_layer(name);
        if (id == fly::DSStack::kNoLayer) return std::optional<uint32_t>();
        return std::optional<uint32_t>(id);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSStack);

FLY_EXPORT_CLASS(fly::DSPin, "EXDSPin")
    FLY_EXPORT_INIT()
    // R7 ㊱：DSPin 不存 name（name 分层存储在 DSDesign 的 pin hasher，
    // 组合键 "cell_name/pin_name"；经 DSDesign::pin_name_of 反查）
    FLY_EXPORT_READONLY_ATTR("type", &fly::DSPin::type_)
    FLY_EXPORT_READONLY_ATTR("direction", &fly::DSPin::direction_)
    // R4：全局平铺 pin id + 放置状态（P3：仅 port 场景有效）
    FLY_EXPORT_READONLY_ATTR("pin_id", &fly::DSPin::pin_id_)
    FLY_EXPORT_READONLY_ATTR("placement_status", &fly::DSPin::placement_status_)
    // R5：port 复用标记（㉙）
    FLY_EXPORT_READONLY_PROPERTY("is_port", [](const fly::DSPin& p) {
        return p.is_port();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPin);

FLY_EXPORT_CLASS(fly::DSCell, "EXDSCell")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("name", &fly::DSCell::name_)
    FLY_EXPORT_READONLY_ATTR("library_name", &fly::DSCell::library_name_)
    // ㉞：尺寸由 bbox 派生（width_/height_ 字段已删除）
    FLY_EXPORT_READONLY_PROPERTY("width", [](const fly::DSCell& c) {
        return c.width();
    })
    FLY_EXPORT_READONLY_PROPERTY("height", [](const fly::DSCell& c) {
        return c.height();
    })
    // ㉞：放置包围盒（含左下角坐标）四元组 + 真实多边形点集
    FLY_EXPORT_READONLY_PROPERTY("bbox", [](const fly::DSCell& c) {
        return rect_to_tuple(c.get_bbox());
    })
    FLY_EXPORT_READONLY_PROPERTY("polygon", [](const fly::DSCell& c) {
        nb::list pts;
        for (const auto& p : c.get_polygon().points_) {
            pts.append(nb::make_tuple(p.get_x(), p.get_y()));
        }
        return pts;
    })
    FLY_EXPORT_READONLY_ATTR("origin_x", &fly::DSCell::origin_x_)
    FLY_EXPORT_READONLY_ATTR("origin_y", &fly::DSCell::origin_y_)
    // class 为 Python 关键字，暴露名取 cell_class
    FLY_EXPORT_READONLY_ATTR("cell_class", &fly::DSCell::class_)
    FLY_EXPORT_READONLY_ATTR("site", &fly::DSCell::site_)
    // block 场景字段（㉙；is_block_cell 判别后使用）
    FLY_EXPORT_READONLY_ATTR("def_path", &fly::DSCell::def_path_)
    FLY_EXPORT_READONLY_ATTR("def_units_per_micron",
                             &fly::DSCell::def_units_per_micron_)
    // ㉗/㉙ flags 只读面
    FLY_EXPORT_READONLY_PROPERTY("is_fake_cell", [](const fly::DSCell& c) {
        return c.is_fake_cell();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_lef_cell", [](const fly::DSCell& c) {
        return c.is_lef_cell();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_lib_cell", [](const fly::DSCell& c) {
        return c.is_lib_cell();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_macro_cell", [](const fly::DSCell& c) {
        return c.is_macro_cell();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_block_cell", [](const fly::DSCell& c) {
        return c.is_block_cell();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_polygon", [](const fly::DSCell& c) {
        return c.is_polygon();
    })
    // ⑰ 运行时注入字段（get_pin_tables/get_pin_geometry 指针共享）
    FLY_EXPORT_DEF("get_pin_tables", [](const fly::DSCell& c) {
        return c.get_pin_tables();
    })
    FLY_EXPORT_DEF("get_pin_geometry", [](const fly::DSCell& c) {
        return c.get_pin_geometry();
    })
    FLY_EXPORT_READONLY_PROPERTY("pin_count", [](const fly::DSCell& c) {
        return static_cast<int>(c.pin_count());
    })
    FLY_EXPORT_DEF("pin_at", [](const fly::DSCell& c, uint32_t i) -> const fly::DSPin& {
        return c.pin_at(i);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_READONLY_PROPERTY("obs_count", [](const fly::DSCell& c) {
        return static_cast<int>(c.obs_count());
    })
    FLY_EXPORT_DEF("obs_at", [](const fly::DSCell& c, uint32_t i) {
        return geometry_ref_to_tuple(c.obs_at(i));
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSCell);

FLY_EXPORT_CLASS(fly::DSInstance, "EXDSInstance")
    FLY_EXPORT_INIT()
    // R7 ㊱：DSInstance 不存 name（实例名在 DSInstanceNameHasher，
    // 随 DSBlockNames_<i> 伴生对象落盘；查询经 EXDSNameMapper）
    FLY_EXPORT_READONLY_ATTR("cell_id", &fly::DSInstance::cell_id_)
    // R6：pos/orient 二元组（pos = cell 原坐标系 (0,0) 点的全局位置）
    FLY_EXPORT_READONLY_PROPERTY("pos_x", [](const fly::DSInstance& i) {
        return i.get_transform().get_offset().get_x();
    })
    FLY_EXPORT_READONLY_PROPERTY("pos_y", [](const fly::DSInstance& i) {
        return i.get_transform().get_offset().get_y();
    })
    FLY_EXPORT_READONLY_PROPERTY("orient", [](const fly::DSInstance& i) {
        return static_cast<int>(i.get_transform().get_orient());
    })
    FLY_EXPORT_READONLY_ATTR("placement_status",
                             &fly::DSInstance::placement_status_)
    FLY_EXPORT_READONLY_ATTR("weight", &fly::DSInstance::weight_)
    // S9：分区归属 primary 位（解析产物恒复位；分区副本置位）
    FLY_EXPORT_READONLY_PROPERTY("is_primary", [](const fly::DSInstance& i) {
        return i.is_primary();
    })
    // S9：电源引脚预展开坐标（D18；解析产物恒空）
    FLY_EXPORT_READONLY_PROPERTY("power_pin_count",
                                 [](const fly::DSInstance& i) {
        return static_cast<int>(i.power_pin_count());
    })
    FLY_EXPORT_DEF("power_pin_at", [](const fly::DSInstance& i, size_t k) {
        const fly::DSPowerPin& pp = i.power_pin_at(k);
        return nb::make_tuple(pp.pin_id_, pp.pos_.get_x(), pp.pos_.get_y());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSInstance);

// 电源引脚预展开坐标（S9 分区副本；D18）
FLY_EXPORT_CLASS(fly::DSPowerPin, "EXDSPowerPin")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("pin_id", &fly::DSPowerPin::pin_id_)
    FLY_EXPORT_READONLY_PROPERTY("pos", [](const fly::DSPowerPin& pp) {
        return nb::make_tuple(pp.pos_.get_x(), pp.pos_.get_y());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPowerPin);

// S5b 网内容只读面（最小暴露：统计 + 按 net/via id 检索；连接项以
// 二元组、wire 以 (layer, width, 点列) 透出，不下沉 Python 几何类型）
FLY_EXPORT_CLASS(fly::DSNetStats, "EXDSNetStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("net_count", &fly::DSNetStats::net_count)
    FLY_EXPORT_READONLY_ATTR("connection_count",
                             &fly::DSNetStats::connection_count)
    FLY_EXPORT_READONLY_ATTR("wire_count", &fly::DSNetStats::wire_count)
    FLY_EXPORT_READONLY_ATTR("rect_count", &fly::DSNetStats::rect_count)
    FLY_EXPORT_READONLY_ATTR("via_instance_count",
                             &fly::DSNetStats::via_instance_count)
    FLY_EXPORT_READONLY_ATTR("skipped_via_count",
                             &fly::DSNetStats::skipped_via_count)
    FLY_EXPORT_READONLY_ATTR("skipped_net_count",
                             &fly::DSNetStats::skipped_net_count)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSNetStats);

FLY_EXPORT_CLASS(fly::DSNetBuildData, "EXDSNetBuildData")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("block_name", &fly::DSNetBuildData::block_name_)
    FLY_EXPORT_READONLY_PROPERTY("stats", [](const fly::DSNetBuildData& n)
                                     -> const fly::DSNetStats& {
        return n.stats_;
    })
    // 密度（⑥ 逐层分列通道，EXDSDensityGrid 的 metal/via 面）
    FLY_EXPORT_READONLY_PROPERTY("density",
                                 [](const fly::DSNetBuildData& n)
                                     -> const fly::DSDensityGrid& {
        return n.density_;
    })
    FLY_EXPORT_READONLY_PROPERTY("via_instance_total",
                                 [](const fly::DSNetBuildData& n) {
        return static_cast<int>(n.via_instances_.size());
    })
    FLY_EXPORT_DEF("connections_of",
                   [](const fly::DSNetBuildData& n, uint64_t net_id) {
        nb::list out;
        const auto* conns = n.connections_of(net_id);
        if (conns != nullptr) {
            for (const auto& c : *conns) {
                out.append(nb::make_tuple(c.get_instance_name(),
                                          c.get_pin_name()));
            }
        }
        return out;
    })
    FLY_EXPORT_DEF("wires_of", [](const fly::DSNetBuildData& n,
                                  uint64_t net_id) {
        nb::list out;
        const auto* wires = n.wires_of(net_id);
        if (wires != nullptr) {
            for (const auto& w : *wires) {
                nb::list pts;
                for (const auto& p : w.points_) {
                    pts.append(nb::make_tuple(p.get_x(), p.get_y()));
                }
                out.append(nb::make_tuple(w.layer_id_, w.width_, pts));
            }
        }
        return out;
    })
    FLY_EXPORT_DEF("rects_of", [](const fly::DSNetBuildData& n,
                                  uint64_t net_id) {
        nb::list out;
        const auto* rects = n.rects_of(net_id);
        if (rects != nullptr) {
            for (const auto& r : *rects) {
                out.append(nb::make_tuple(r.layer_id_,
                                          rect_to_tuple(r.rect_)));
            }
        }
        return out;
    })
    FLY_EXPORT_DEF("via_ids_of", [](const fly::DSNetBuildData& n,
                                    uint64_t net_id) {
        nb::list out;
        const auto* ids = n.via_ids_of(net_id);
        if (ids != nullptr) {
            for (const uint64_t id : *ids) out.append(id);
        }
        return out;
    })
    FLY_EXPORT_DEF("via_instance_at", [](const fly::DSNetBuildData& n,
                                         uint64_t via_id) {
        const fly::DSViaInstance* v = n.via_instance_at(via_id);
        if (v == nullptr) return std::optional<nb::tuple>();
        return std::optional(nb::make_tuple(v->via_cell_id_,
                                            v->pos_.get_x(),
                                            v->pos_.get_y()));
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSNetBuildData);

// S6 层级树只读面（⑮ 四接口 + ⑨ 换算；未命中 id 一律返回 None，不透出
// kNoNode 哨兵；区间以 (start, end) 闭端点对透出——end = start + count）
FLY_EXPORT_CLASS(fly::DSHierNode, "EXDSHierNode")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("id", &fly::DSHierNode::id_)
    FLY_EXPORT_READONLY_ATTR("parent_id", &fly::DSHierNode::parent_id_)
    FLY_EXPORT_READONLY_ATTR("block_cell_name",
                             &fly::DSHierNode::block_cell_name_)
    FLY_EXPORT_READONLY_ATTR("instance_name", &fly::DSHierNode::instance_name_)
    // ⑧ 该 block instance 自身的全局 instance id（local 0 映射目标）
    FLY_EXPORT_READONLY_ATTR("self_global_id", &fly::DSHierNode::self_global_id_)
    FLY_EXPORT_READONLY_PROPERTY("instance_range",
                                 [](const fly::DSHierNode& n) {
        return nb::make_tuple(n.instance_start_,
                              n.instance_start_ + n.instance_count_);
    })
    FLY_EXPORT_READONLY_PROPERTY("net_range", [](const fly::DSHierNode& n) {
        return nb::make_tuple(n.net_start_, n.net_start_ + n.net_count_);
    })
    FLY_EXPORT_READONLY_PROPERTY("via_range", [](const fly::DSHierNode& n) {
        return nb::make_tuple(n.via_start_, n.via_start_ + n.via_count_);
    })
    FLY_EXPORT_READONLY_PROPERTY("children", [](const fly::DSHierNode& n) {
        nb::list out;
        for (const uint32_t c : n.get_children_ids()) out.append(c);
        return out;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSHierNode);

FLY_EXPORT_CLASS(fly::DSHierTree, "EXDSHierTree")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("design_name", &fly::DSHierTree::design_name_)
    FLY_EXPORT_READONLY_PROPERTY("node_count", [](const fly::DSHierTree& t) {
        return static_cast<int>(t.node_count());
    })
    FLY_EXPORT_DEF("node", [](const fly::DSHierTree& t, uint32_t id)
                       -> const fly::DSHierNode& {
        return t.node(id);
    }, nb::rv_policy::reference_internal)
    // ① 区间反查（R7 ㊳：global id 64 位）
    FLY_EXPORT_DEF("block_of_instance", [](const fly::DSHierTree& t,
                                           uint64_t global_id) {
        uint32_t n = t.block_of_instance(global_id);
        if (n == fly::DSHierTree::kNoNode) return std::optional<uint32_t>();
        return std::optional<uint32_t>(n);
    })
    FLY_EXPORT_DEF("block_of_net", [](const fly::DSHierTree& t,
                                      uint64_t global_id) {
        uint32_t n = t.block_of_net(global_id);
        if (n == fly::DSHierTree::kNoNode) return std::optional<uint32_t>();
        return std::optional<uint32_t>(n);
    })
    FLY_EXPORT_DEF("block_of_via_instance", [](const fly::DSHierTree& t,
                                               uint64_t global_id) {
        uint32_t n = t.block_of_via_instance(global_id);
        if (n == fly::DSHierTree::kNoNode) return std::optional<uint32_t>();
        return std::optional<uint32_t>(n);
    })
    // ② 范围查：(start, count)
    FLY_EXPORT_DEF("instance_range", [](const fly::DSHierTree& t, uint32_t id) {
        return t.instance_range(id);
    })
    FLY_EXPORT_DEF("net_range", [](const fly::DSHierTree& t, uint32_t id) {
        return t.net_range(id);
    })
    FLY_EXPORT_DEF("via_range", [](const fly::DSHierTree& t, uint32_t id) {
        return t.via_range(id);
    })
    // ③ 父与直系 children
    FLY_EXPORT_DEF("parent", [](const fly::DSHierTree& t, uint32_t id) {
        return t.parent(id);
    })
    FLY_EXPORT_DEF("children", [](const fly::DSHierTree& t, uint32_t id) {
        nb::list out;
        for (const uint32_t c : t.children(id)) out.append(c);
        return out;
    })
    // ④ 以 name 打印缩进层级文本
    FLY_EXPORT_DEF("format_tree", [](const fly::DSHierTree& t) {
        return t.format_tree();
    })
    // ⑨ global id 换算（local 0 → 节点自身 global id；R7 ㊳ 64 位）
    FLY_EXPORT_DEF("global_instance_id", [](const fly::DSHierTree& t,
                                            uint32_t node_id,
                                            uint64_t local_id) {
        uint64_t g = t.global_instance_id(node_id, local_id);
        if (g == fly::DSHierTree::kNoNode) return std::optional<uint64_t>();
        return std::optional<uint64_t>(g);
    })
    FLY_EXPORT_DEF("global_net_id", [](const fly::DSHierTree& t,
                                       uint32_t node_id, uint64_t local_id) {
        uint64_t g = t.global_net_id(node_id, local_id);
        if (g == fly::DSHierTree::kNoNode) return std::optional<uint64_t>();
        return std::optional<uint64_t>(g);
    })
    FLY_EXPORT_DEF("global_via_instance_id", [](const fly::DSHierTree& t,
                                                uint32_t node_id,
                                                uint64_t local_id) {
        uint64_t g = t.global_via_instance_id(node_id, local_id);
        if (g == fly::DSHierTree::kNoNode) return std::optional<uint64_t>();
        return std::optional<uint64_t>(g);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSHierTree);

FLY_EXPORT_CLASS(fly::DSDensityGrid, "EXDSDensityGrid")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("origin_x", &fly::DSDensityGrid::origin_x_)
    FLY_EXPORT_READONLY_ATTR("origin_y", &fly::DSDensityGrid::origin_y_)
    FLY_EXPORT_READONLY_ATTR("bin_width", &fly::DSDensityGrid::bin_width_)
    FLY_EXPORT_READONLY_ATTR("bin_height", &fly::DSDensityGrid::bin_height_)
    FLY_EXPORT_READONLY_ATTR("cols", &fly::DSDensityGrid::cols_)
    FLY_EXPORT_READONLY_ATTR("rows", &fly::DSDensityGrid::rows_)
    // S8 合并消费的计数量（实例面积通道）
    FLY_EXPORT_READONLY_PROPERTY("total_count", [](const fly::DSDensityGrid& g) {
        return static_cast<int64_t>(g.total_count());
    })
    FLY_EXPORT_DEF("cell_count", [](const fly::DSDensityGrid& g, uint32_t col,
                                    uint32_t row) {
        return static_cast<int64_t>(g.cell_count(col, row));
    })
    // ⑥ 逐层分列通道（S5b 网内容：金属/通孔分类分层保存）
    FLY_EXPORT_READONLY_PROPERTY("metal_total", [](const fly::DSDensityGrid& g) {
        return static_cast<int64_t>(g.metal_total());
    })
    FLY_EXPORT_READONLY_PROPERTY("via_total", [](const fly::DSDensityGrid& g) {
        return static_cast<int64_t>(g.via_total());
    })
    FLY_EXPORT_DEF("layer_total", [](const fly::DSDensityGrid& g, uint32_t layer_id,
                                     bool via_channel) {
        return static_cast<int64_t>(g.layer_total(layer_id, via_channel));
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSDensityGrid);

// S8 分区描述（core/extend 双区域只读面；core/extend 以四元组透出——
// 骨架期几何约定同上）；随 DSDesign.partitions_ 序列化持久化
FLY_EXPORT_CLASS(fly::DSSubPartition, "EXDSSubPartition")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("partition_id", &fly::DSSubPartition::partition_id_)
    // 分区网格坐标（S9 分区对象命名 PART_{xp}_{yp}/ 用）
    FLY_EXPORT_READONLY_ATTR("xp", &fly::DSSubPartition::xp_)
    FLY_EXPORT_READONLY_ATTR("yp", &fly::DSSubPartition::yp_)
    FLY_EXPORT_READONLY_PROPERTY("core_rect", [](const fly::DSSubPartition& p) {
        return rect_to_tuple(p.core_rect_);
    })
    FLY_EXPORT_READONLY_PROPERTY("extend_rect",
                                 [](const fly::DSSubPartition& p) {
        return rect_to_tuple(p.extend_rect_);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSSubPartition);

FLY_EXPORT_CLASS(fly::DSInstanceStats, "EXDSInstanceStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("instance_count",
                             &fly::DSInstanceStats::instance_count)
    FLY_EXPORT_READONLY_ATTR("unplaced_count",
                             &fly::DSInstanceStats::unplaced_count)
    FLY_EXPORT_READONLY_ATTR("fake_cell_count",
                             &fly::DSInstanceStats::fake_cell_count)
    // per-cell 引用计数（cell id → 实例数）
    FLY_EXPORT_READONLY_PROPERTY("per_cell_count",
                                 [](const fly::DSInstanceStats& s) {
        return static_cast<int>(s.per_cell_counts_.size());
    })
    FLY_EXPORT_DEF("per_cell_count_of", [](const fly::DSInstanceStats& s,
                                           uint32_t cell_id) -> std::optional<uint64_t> {
        auto it = s.per_cell_counts_.find(cell_id);
        if (it == s.per_cell_counts_.end()) return std::nullopt;
        return it->second;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSInstanceStats);

FLY_EXPORT_CLASS(fly::DSBlockBuildData, "EXDSBlockBuildData")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("block_name", &fly::DSBlockBuildData::block_name_)
    // local instance 表（⑧：id 0 = block 自身占位；R7 ㊳ 64 位）
    FLY_EXPORT_READONLY_PROPERTY("instance_total", [](const fly::DSBlockBuildData& b) {
        return static_cast<int>(b.instance_total());
    })
    FLY_EXPORT_READONLY_PROPERTY("leaf_count", [](const fly::DSBlockBuildData& b) {
        return static_cast<int>(b.stats_.instance_count);
    })
    FLY_EXPORT_DEF("get_instance", [](const fly::DSBlockBuildData& b,
                                      uint64_t id) -> const fly::DSInstance& {
        return b.instances_.at(id);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("find_instance_by_name",
                   [](const fly::DSBlockBuildData& b, const CMString& name)
                       -> const fly::DSInstance* {
        return b.find_instance_by_name(name);
    }, nb::rv_policy::reference_internal)
    // local net 名空间（③ NetNameOnly；R7 ㊱ 经 net hasher 双向查询——
    // hasher 随 DSBlockNames_<i> 伴生对象落盘，需先 attach_names 注入）
    FLY_EXPORT_READONLY_PROPERTY("net_count", [](const fly::DSBlockBuildData& b) {
        return static_cast<int>(b.net_count());
    })
    FLY_EXPORT_DEF("net_id_by_name", [](const fly::DSBlockBuildData& b,
                                        const CMString& name) {
        if (!b.net_names_) return std::optional<uint64_t>();
        const uint64_t id = b.net_names_->get_id(name);
        if (id == fly::DSNetNameHasher::kInvalidId) {
            return std::optional<uint64_t>();
        }
        return std::optional<uint64_t>(id);
    })
    FLY_EXPORT_DEF("net_name_by_id", [](const fly::DSBlockBuildData& b,
                                        uint64_t id) {
        // R8b：hasher id→name 侧 arena 化——net_name_at 返回 optional
        //（nullopt = 未注入/越界；空串 = 空洞，语义同 R7）
        const std::optional<CMString> n = b.net_name_at(id);
        if (!n.has_value()) return std::optional<CMString>();
        return std::optional<CMString>(*n);
    })
    // ㊵② 名字伴生对象注入口（读回 DSBlockNames_<i> 后共享注入——
    // CMSharedPtr 拷贝即共享计数，零数据拷贝零 move；hasher 归属与伴生
    // 对象共享、全程只读消费）
    FLY_EXPORT_DEF("attach_names", [](fly::DSBlockBuildData& b,
                                      fly::DSBlockNames& names) {
        b.set_instance_names(names.instance_names_);
        b.set_net_names(names.net_names_);
    })
    // ㊵② 名字伴生对象产出（解析后落盘 DSBlockNames_<i> 用——两 hasher
    // 以共享指针与 block 名组装；与 block_data 的运行时 hasher 同一实例、
    // 零数据拷贝）
    FLY_EXPORT_DEF("extract_names", [](const fly::DSBlockBuildData& b) {
        fly::DSBlockNames names;
        names.block_name_ = b.block_name_;
        names.instance_names_ = b.instance_names_;
        names.net_names_ = b.net_names_;
        return names;
    })
    // R8d 落盘封口（裁定 55：alpha 键 lcp_name_arena 传入点——解析完成
    // 后对 instance/net 两 hasher id→name 侧做 LCP 后缀压缩封口，
    // DSBlockNames_<i> 落盘即封口形态；false = 形态一默认零变化）
    FLY_EXPORT_DEF("finalize_names_for_save",
                   [](fly::DSBlockBuildData& b, bool lcp_enabled) {
        b.finalize_names_for_save(lcp_enabled);
    })
    // S4 obstruction 回填（2026-09-13 D17 修订：S4 头扫描以
    // (layer_id, (xl, yl, xh, yh)) 元组透出，flow 转入 per-DEF 产物供
    // S9 入分区 geometry net 0 + OBS 位）
    FLY_EXPORT_DEF("set_obstructions", [](fly::DSBlockBuildData& b,
                                          nb::list obs) {
        for (nb::handle item : obs) {
            const nb::tuple tup = nb::cast<nb::tuple>(item);
            const nb::tuple rect = nb::cast<nb::tuple>(tup[1]);
            fly::DSShapeRef ref;
            ref.layer_id_ = nb::cast<uint32_t>(tup[0]);
            ref.set_rect(GEORect(nb::cast<int32_t>(rect[0]),
                                 nb::cast<int32_t>(rect[1]),
                                 nb::cast<int32_t>(rect[2]),
                                 nb::cast<int32_t>(rect[3])));
            b.obstructions_.push_back(std::move(ref));
        }
    })
    FLY_EXPORT_READONLY_PROPERTY("fake_cell_count",
                                 [](const fly::DSBlockBuildData& b) {
        return static_cast<int>(b.fake_cells_.size());
    })
    FLY_EXPORT_READONLY_PROPERTY("density",
                                 [](const fly::DSBlockBuildData& b)
                                     -> const fly::DSDensityGrid& {
        return b.density_;
    })
    FLY_EXPORT_READONLY_PROPERTY("stats",
                                 [](const fly::DSBlockBuildData& b)
                                     -> const fly::DSInstanceStats& {
        return b.stats_;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSBlockBuildData);

// ㊵② per-DEF local 名空间伴生对象（DSBlockNames_<i> 独立落盘的 Python
// 面）：instance/net 两 hasher（R7 双向）+ block 名冗余。hasher 本体
//（DSNameHasherT）不上 Python 面——查询经本对象的组合键接口；业务
//「不拿 name」的场景不加载本对象（⑰/⑱ 按需加载）。
FLY_EXPORT_CLASS(fly::DSBlockNames, "EXDSBlockNames")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("block_name", &fly::DSBlockNames::block_name_)
    FLY_EXPORT_READONLY_PROPERTY("instance_hasher_size",
                                 [](const fly::DSBlockNames& n) {
        return static_cast<int>(n.instance_names_->size());
    })
    FLY_EXPORT_READONLY_PROPERTY("net_hasher_size",
                                 [](const fly::DSBlockNames& n) {
        return static_cast<int>(n.net_names_->size());
    })
    // instance 名 ↔ local id（local id 从 1 起、0 = 占位不入表）
    FLY_EXPORT_DEF("instance_id_by_name", [](const fly::DSBlockNames& n,
                                             const CMString& name) {
        const uint64_t id = n.instance_names_->get_id(name);
        if (id == fly::DSInstanceNameHasher::kInvalidId) {
            return std::optional<uint64_t>();
        }
        return std::optional<uint64_t>(id);
    })
    FLY_EXPORT_DEF("instance_name_by_id", [](const fly::DSBlockNames& n,
                                             uint64_t id) {
        // R8d：域判别形态感知（封口后偏移表已释放——读 id→rank 表规模）
        if (id >= n.instance_names_->name_domain()) {
            return std::optional<CMString>();
        }
        return std::optional<CMString>(n.instance_names_->get_name(id));
    })
    // net 名 ↔ local id（local id 从 1 起、0 保留未用）
    FLY_EXPORT_DEF("net_id_by_name", [](const fly::DSBlockNames& n,
                                        const CMString& name) {
        const uint64_t id = n.net_names_->get_id(name);
        if (id == fly::DSNetNameHasher::kInvalidId) {
            return std::optional<uint64_t>();
        }
        return std::optional<uint64_t>(id);
    })
    FLY_EXPORT_DEF("net_name_by_id", [](const fly::DSBlockNames& n,
                                        uint64_t id) {
        // R8d：域判别形态感知（封口后偏移表已释放——读 id→rank 表规模）
        if (id >= n.net_names_->name_domain()) {
            return std::optional<CMString>();
        }
        return std::optional<CMString>(n.net_names_->get_name(id));
    })
    // R8d 形态观测（alpha 传递链验证面）：两 hasher 是否均处 LCP 封口形态
    FLY_EXPORT_READONLY_PROPERTY("names_lcp_form",
                                 [](const fly::DSBlockNames& n) {
        return n.instance_names_->is_lcp_form() &&
               n.net_names_->is_lcp_form();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSBlockNames);

// COMPONENTS/网内容责任链（DSInstancePipeline/DSNetPipeline 含
// unique_ptr 成员，nanobind 类型注册要求可拷贝故不上 Python 面）：节点
// 装配在 C++ 工厂 ds_make_components_pipeline/ds_make_nets_pipeline，
// 解析入口内部装配消费；链扩展（新功能=加节点）在 C++ 侧进行。

FLY_EXPORT_CLASS(fly::DSViaCell, "EXDSViaCell")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("name", &fly::DSViaCell::name_)
    FLY_EXPORT_READONLY_ATTR("bottom_layer_id", &fly::DSViaCell::bottom_layer_id_)
    FLY_EXPORT_READONLY_ATTR("top_layer_id", &fly::DSViaCell::top_layer_id_)
    // ⑥ 通孔密度通道分层键（UINT32_MAX = 未判定）
    FLY_EXPORT_READONLY_ATTR("cut_layer_id", &fly::DSViaCell::cut_layer_id_)
    FLY_EXPORT_READONLY_PROPERTY("cut_rect_count", [](const fly::DSViaCell& v) {
        return static_cast<int>(v.cut_rect_count());
    })
    FLY_EXPORT_DEF("cut_rect_at", [](const fly::DSViaCell& v, uint32_t i) {
        const auto& r = v.cut_rect_at(i);
        return nb::make_tuple(r.get_x_low(), r.get_y_low(), r.get_x_high(),
                              r.get_y_high());
    })
    FLY_EXPORT_READONLY_PROPERTY("bottom_enclosure_count",
                                 [](const fly::DSViaCell& v) {
        return static_cast<int>(v.bottom_enclosure_count());
    })
    FLY_EXPORT_DEF("bottom_enclosure_at", [](const fly::DSViaCell& v, uint32_t i) {
        const auto& r = v.bottom_enclosure_at(i);
        return nb::make_tuple(r.get_x_low(), r.get_y_low(), r.get_x_high(),
                              r.get_y_high());
    })
    FLY_EXPORT_READONLY_PROPERTY("top_enclosure_count",
                                 [](const fly::DSViaCell& v) {
        return static_cast<int>(v.top_enclosure_count());
    })
    FLY_EXPORT_DEF("top_enclosure_at", [](const fly::DSViaCell& v, uint32_t i) {
        const auto& r = v.top_enclosure_at(i);
        return nb::make_tuple(r.get_x_low(), r.get_y_low(), r.get_x_high(),
                              r.get_y_high());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSViaCell);

FLY_EXPORT_CLASS(fly::DSPinTables, "EXDSPinTables")
    FLY_EXPORT_INIT()
    // R4：按全局 pin id 检索（原 cell id 键改名 pin 维度）
    FLY_EXPORT_DEF("pin_has_tables", [](const fly::DSPinTables& t, uint32_t pin_id) {
        return t.pin_has_tables(pin_id);
    })
    FLY_EXPORT_READONLY_PROPERTY("pin_count", [](const fly::DSPinTables& t) {
        return static_cast<int>(t.internal_power_tables_.size() +
                                t.timing_tables_.size());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPinTables);

FLY_EXPORT_CLASS(fly::DSPinGeometry, "EXDSPinGeometry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_DEF("pin_has_geometry",
                   [](const fly::DSPinGeometry& g, uint32_t pin_id) {
        return g.pin_has_geometry(pin_id);
    })
    FLY_EXPORT_DEF("geometry_count_of",
                   [](const fly::DSPinGeometry& g, uint32_t pin_id) {
        const auto* vec = g.geometry_of(pin_id);
        return vec ? static_cast<int>(vec->size()) : 0;
    })
    FLY_EXPORT_DEF("geometry_of",
                   [](const fly::DSPinGeometry& g, uint32_t pin_id) {
        nb::list out;
        const auto* vec = g.geometry_of(pin_id);
        if (vec != nullptr) {
            for (const auto& geo : *vec) {
                out.append(geometry_ref_to_tuple(geo));
            }
        }
        return out;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPinGeometry);

FLY_EXPORT_CLASS(fly::DSLefParseStats, "EXDSLefParseStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("layer_count", &fly::DSLefParseStats::layer_count)
    FLY_EXPORT_READONLY_ATTR("skipped_layer_count",
                             &fly::DSLefParseStats::skipped_layer_count)
    FLY_EXPORT_READONLY_ATTR("macro_count", &fly::DSLefParseStats::macro_count)
    FLY_EXPORT_READONLY_ATTR("skipped_macro_count",
                             &fly::DSLefParseStats::skipped_macro_count)
    FLY_EXPORT_READONLY_ATTR("pin_count", &fly::DSLefParseStats::pin_count)
    FLY_EXPORT_READONLY_ATTR("via_count", &fly::DSLefParseStats::via_count)
    FLY_EXPORT_READONLY_ATTR("via_conflict_count",
                             &fly::DSLefParseStats::via_conflict_count)
    FLY_EXPORT_READONLY_ATTR("viarule_count",
                             &fly::DSLefParseStats::viarule_count)
    FLY_EXPORT_READONLY_ATTR("skipped_geometry_count",
                             &fly::DSLefParseStats::skipped_geometry_count)
    FLY_EXPORT_READONLY_ATTR("parse_failed_count",
                             &fly::DSLefParseStats::parse_failed_count);

FLY_EXPORT_CLASS(fly::DSDefParseStats, "EXDSDefParseStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("block_count", &fly::DSDefParseStats::block_count)
    FLY_EXPORT_READONLY_ATTR("port_count", &fly::DSDefParseStats::port_count)
    FLY_EXPORT_READONLY_ATTR("skipped_port_count",
                             &fly::DSDefParseStats::skipped_port_count)
    FLY_EXPORT_READONLY_ATTR("via_count", &fly::DSDefParseStats::via_count)
    FLY_EXPORT_READONLY_ATTR("viarule_via_count",
                             &fly::DSDefParseStats::viarule_via_count)
    FLY_EXPORT_READONLY_ATTR("via_conflict_count",
                             &fly::DSDefParseStats::via_conflict_count)
    FLY_EXPORT_READONLY_ATTR("die_area_count",
                             &fly::DSDefParseStats::die_area_count)
    FLY_EXPORT_READONLY_ATTR("obstruction_count",
                             &fly::DSDefParseStats::obstruction_count)
    FLY_EXPORT_READONLY_ATTR("skipped_polygon_obstruction_count",
                             &fly::DSDefParseStats::
                                 skipped_polygon_obstruction_count);

FLY_EXPORT_CLASS(fly::DSDefComponentsStats, "EXDSDefComponentsStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("component_count",
                             &fly::DSDefComponentsStats::component_count)
    FLY_EXPORT_READONLY_ATTR("net_count",
                             &fly::DSDefComponentsStats::net_count)
    FLY_EXPORT_READONLY_ATTR("skipped_net_count",
                             &fly::DSDefComponentsStats::skipped_net_count);

FLY_EXPORT_CLASS(fly::DSDefNetsStats, "EXDSDefNetsStats")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("net_count", &fly::DSDefNetsStats::net_count)
    FLY_EXPORT_READONLY_ATTR("connection_count",
                             &fly::DSDefNetsStats::connection_count)
    FLY_EXPORT_READONLY_ATTR("wire_count", &fly::DSDefNetsStats::wire_count)
    FLY_EXPORT_READONLY_ATTR("rect_count", &fly::DSDefNetsStats::rect_count)
    FLY_EXPORT_READONLY_ATTR("via_instance_count",
                             &fly::DSDefNetsStats::via_instance_count)
    FLY_EXPORT_READONLY_ATTR("skipped_via_count",
                             &fly::DSDefNetsStats::skipped_via_count)
    FLY_EXPORT_READONLY_ATTR("skipped_net_count",
                             &fly::DSDefNetsStats::skipped_net_count)
    FLY_EXPORT_READONLY_ATTR("batch_count", &fly::DSDefNetsStats::batch_count);

FLY_EXPORT_CLASS(fly::DSDesign, "EXDSDesign")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("cell_count", [](const fly::DSDesign& d) {
        return static_cast<int>(d.cells_.size());
    })
    // 统一入口（⑰）：返回的 cell 已按指针注入 pin_tables_/pin_geometry_
    FLY_EXPORT_DEF("get_cell", [](fly::DSDesign& d, uint32_t cell_id) -> fly::DSCell& {
        return d.get_cell(cell_id);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("find_cell", [](fly::DSDesign& d, const CMString& name) -> fly::DSCell* {
        return d.cells_.empty() ? nullptr
                                : const_cast<fly::DSCell*>(d.find_cell(name));
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_READONLY_PROPERTY("via_cell_count", [](const fly::DSDesign& d) {
        return static_cast<int>(d.via_cells_.size());
    })
    FLY_EXPORT_DEF("find_via_cell",
                   [](const fly::DSDesign& d, const CMString& name) -> const fly::DSViaCell* {
        return d.find_via_cell(name);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("via_cell_id_by_name",
                   [](const fly::DSDesign& d, const CMString& name) {
        // R7 ㊲：经 via cell hasher 直查（未命中 None）
        const uint32_t id = d.via_cell_names_.get_id(name);
        if (id == fly::DSViaCellNameHasher::kInvalidId) {
            return std::optional<uint32_t>();
        }
        return std::optional<uint32_t>(id);
    })
    // S6 层级树（⑬ 挂容器）：只读面 + 构建产物写定口（正式 DSDesign
    // 写定前嵌树，容器唯一写定原则）
    FLY_EXPORT_DEF("get_hier_tree", [](const fly::DSDesign& d)
                                    -> const fly::DSHierTree& {
        return d.get_hier_tree();
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("set_hier_tree", [](fly::DSDesign& d,
                                       const fly::DSHierTree& t) {
        d.set_hier_tree(t);
    })
    // S8 分区矩形表（core/extend 双区域；S8 任务写定）
    FLY_EXPORT_READONLY_PROPERTY("partition_count", [](const fly::DSDesign& d) {
        return static_cast<int>(d.partition_count());
    })
    FLY_EXPORT_DEF("partition_at", [](const fly::DSDesign& d, size_t i)
                       -> const fly::DSSubPartition& {
        return d.partition_at(i);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("set_partitions", [](fly::DSDesign& d,
                                        CMVector<fly::DSSubPartition> parts) {
        d.set_partitions(std::move(parts));
    })
    // name ↔ id 查询（R7 ㊲：经 hasher 双向底座；未命中 None / 空洞
    // 返回 None——不透出 kInvalidId 哨兵到 Python 面）
    FLY_EXPORT_DEF("cell_id_by_name",
                   [](const fly::DSDesign& d, const CMString& name) {
        const uint32_t id = d.cell_names_.get_id(name);
        if (id == fly::DSCellNameHasher::kInvalidId) {
            return std::optional<uint32_t>();
        }
        return std::optional<uint32_t>(id);
    })
    FLY_EXPORT_DEF("cell_name_by_id", [](const fly::DSDesign& d, uint32_t id) {
        if (id >= d.cell_names_.name_table_.size()) {
            return std::optional<CMString>();
        }
        const CMString& name = d.cell_names_.get_name(id);
        if (name.empty()) return std::optional<CMString>();  // 空洞
        return std::optional<CMString>(name);
    })
    FLY_EXPORT_DEF("pin_id_by_name",
                   [](const fly::DSDesign& d, const CMString& cell_name,
                     const CMString& pin_name) {
        // pin 组合键（D1："cell_name/pin_name"）经 pin hasher 直查
        const uint32_t id = d.pin_names_.get_id(cell_name + "/" + pin_name);
        if (id == fly::DSPinNameHasher::kInvalidId) {
            return std::optional<uint32_t>();
        }
        return std::optional<uint32_t>(id);
    })
    // R7 ㊱：pin 名反查（DSPin 不存 name；经 pin hasher 组合键取 pin 名段）
    FLY_EXPORT_DEF("pin_name_of", [](const fly::DSDesign& d, uint32_t pin_id) {
        const CMString name = d.pin_name_of(pin_id);
        if (name.empty()) return std::optional<CMString>();
        return std::optional<CMString>(name);
    })
    FLY_EXPORT_DEF("fake_cell_ids", [](const fly::DSDesign& d) {
        nb::list out;
        for (uint32_t id : d.fake_cell_ids_) out.append(id);
        return out;
    })
    // 构建期接口（汇总/测试/脚本侧装配）
    FLY_EXPORT_DEF("add_cell", [](fly::DSDesign& d, fly::DSCell c) {
        return d.add_cell(std::move(c));
    })
    FLY_EXPORT_DEF("add_via_cell", [](fly::DSDesign& d, fly::DSViaCell v) {
        return d.add_via_cell(std::move(v));
    })
    // ⑱ 统一加载注入面：pin 表 / pin 几何独立对象挂容器专用字段
    FLY_EXPORT_DEF("set_pin_tables",
                   [](fly::DSDesign& d,
                      std::shared_ptr<fly::DSPinTables> t) {
        d.set_pin_tables(std::move(t));
    })
    FLY_EXPORT_DEF("get_pin_tables", [](fly::DSDesign& d) {
        return d.get_pin_tables();
    })
    FLY_EXPORT_DEF("set_pin_geometry",
                   [](fly::DSDesign& d,
                      std::shared_ptr<fly::DSPinGeometry> g) {
        d.set_pin_geometry(std::move(g));
    })
    FLY_EXPORT_DEF("get_pin_geometry", [](fly::DSDesign& d) {
        return d.get_pin_geometry();
    })
    // S3：lib cell ↔ lef cell 结构 merge（返回匹配 cell 数）
    FLY_EXPORT_DEF("merge_lib", [](fly::DSDesign& d,
                                   const fly::LIBLibrary& lib) {
        return d.merge_lib(lib);
    })
    // R4：cell 维度几何便利聚合（按 cell 的 pin id 逐个取出拼合）
    FLY_EXPORT_DEF("cell_pin_geometries", [](fly::DSDesign& d, uint32_t cell_id) {
        nb::list out;
        for (const auto& g : d.cell_pin_geometries(cell_id)) {
            out.append(geometry_ref_to_tuple(g));
        }
        return out;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSDesign);

// ── 解析入口（T4/T5）与汇总 merge（T6）────────────────────────────

// S1：tech lef → (via 集合含 VIARULE 展开模板, stats)；stack 就地填充
FLY_EXPORT_FUNCTION("ds_parse_tech_lef", [](const CMString& path,
                                            fly::DSStack& stack) {
    CMVector<fly::DSViaCell> vias;
    const auto stats = fly::ds_parse_tech_lef(path, stack, vias);
    return nb::make_tuple(nb::cast(vias), nb::cast(stats));
});

// S2：cell lef → (临时 design, pin 几何, via 集合, stats)
FLY_EXPORT_FUNCTION("ds_parse_cell_lef", [](const CMString& path,
                                            const fly::DSStack& stack) {
    fly::DSDesign design;
    fly::DSPinGeometry geoms;
    CMVector<fly::DSViaCell> vias;
    const auto stats =
        fly::ds_parse_cell_lef(path, stack, design, geoms, vias);
    return nb::make_tuple(nb::cast(std::move(design)), nb::cast(std::move(geoms)),
                          nb::cast(vias), nb::cast(stats));
});

// S4+S4b：DEF 头扫描 → (block cell 集合, port pin 名序列, port 几何,
// via 集合, obstruction 集合, stats)。R7 ㊱：DSPin 不存 name——port 名按
// 下标与 block_cells[0].pins_ 对齐由汇总传进全局 pin hasher。obstruction
// 以 (layer_id, (xl, yl, xh, yh)) 元组透出（DSShapeRef 不上 Python 面，
// 骨架期几何约定同 DSCell.obs_at；回填经 EXDSBlockBuildData 的
// set_obstructions）
FLY_EXPORT_FUNCTION("ds_parse_def_header",
                    [](const CMString& path, const fly::DSStack& stack) {
    CMVector<fly::DSCell> block_cells;
    CMVector<CMString> port_names;
    fly::DSPinGeometry port_geoms;
    CMVector<fly::DSViaCell> vias;
    CMVector<fly::DSShapeRef> obstructions;
    fly::DSDefParseStats stats;
    fly::ds_parse_def_header(path, stack, block_cells, port_names,
                             port_geoms, vias, obstructions, stats);
    nb::list obs_out;
    for (const auto& obs : obstructions) {
        obs_out.append(geometry_ref_to_tuple(obs));
    }
    return nb::make_tuple(nb::cast(std::move(block_cells)),
                          nb::cast(std::move(port_names)),
                          nb::cast(std::move(port_geoms)),
                          nb::cast(std::move(vias)), obs_out,
                          nb::cast(stats));
});

// S2 汇总：返回抛弃的重名 macro 数
FLY_EXPORT_FUNCTION("ds_merge_cell_lef",
                    [](fly::DSDesign& dst, const fly::DSDesign& src_part,
                       fly::DSPinGeometry& dst_geom,
                       const fly::DSPinGeometry& src_geom) {
    return fly::ds_merge_cell_lef(dst, src_part, dst_geom, src_geom);
});

// S4/S4b 汇总：返回抛弃的重名（block cell + via cell）数。port_names 与
// block_cells 的 pins_ 下标对齐（R7 ㊱ name 边界传递）
FLY_EXPORT_FUNCTION("ds_merge_def_header",
                    [](fly::DSDesign& dst,
                       const CMVector<fly::DSCell>& block_cells,
                       const CMVector<CMString>& port_names,
                       fly::DSPinGeometry& dst_geom,
                       const fly::DSPinGeometry& port_geoms,
                       const CMVector<fly::DSViaCell>& def_vias) {
    return fly::ds_merge_def_header(dst, block_cells, port_names, dst_geom,
                                    port_geoms, def_vias);
});

// S5a：COMPONENTS 责任链 ∥ 网名扫描（同一遍 DEF 读取，责任链在 C++
// 侧内部装配）→ stats；block_data 就地填充
FLY_EXPORT_FUNCTION("ds_parse_def_components",
                    [](const CMString& path, const fly::DSStack& stack,
                       const fly::DSDesign& design,
                       fly::DSBlockBuildData& block_data,
                       int32_t density_bin_dbu) {
    fly::DSDefComponentsStats stats;
    fly::ds_parse_def_components(path, stack, design, block_data, stats,
                                 density_bin_dbu);
    return nb::cast(stats);
});

// S5b：网内容责任链 ∥ 分批多阶段（③ 批界 net_batch_size；⑨ local net
// id 对齐 block_data 的网名空间）→ stats；net_data 就地填充
FLY_EXPORT_FUNCTION("ds_parse_def_nets",
                    [](const CMString& path, const fly::DSStack& stack,
                       const fly::DSDesign& design,
                       const fly::DSBlockBuildData& block_data,
                       fly::DSNetBuildData& net_data, int32_t density_bin_dbu,
                       int net_batch_size) {
    fly::DSDefNetsStats stats;
    fly::ds_parse_def_nets(path, stack, design, block_data, net_data, stats,
                           density_bin_dbu, net_batch_size);
    return nb::cast(stats);
});

// S6：层级树构建 + 起始编号分配（⑧⑨⑮；blocks/nets 为 per-DEF 产物
// 列表按 def_paths 序对齐，借引用不拷贝；via 计数取 S5b 统计；多根/
// 零根/错位 → C++ 侧 raise，双空列表 → 空树）
FLY_EXPORT_FUNCTION("ds_build_hier_tree",
                    [](nb::list blocks, nb::list nets,
                       const fly::DSDesign& design) {
    fly::CMVector<const fly::DSBlockBuildData*> block_ptrs;
    for (nb::handle item : blocks) {
        block_ptrs.push_back(&nb::cast<const fly::DSBlockBuildData&>(item));
    }
    fly::CMVector<const fly::DSNetBuildData*> net_ptrs;
    for (nb::handle item : nets) {
        net_ptrs.push_back(&nb::cast<const fly::DSNetBuildData&>(item));
    }
    return nb::cast(fly::ds_build_hier_tree(block_ptrs, net_ptrs, design));
});

// S5a 汇总：fake cell 并入（返回并入数）；block_data 的实例引用随顺延
// 结果就地重映射
FLY_EXPORT_FUNCTION("ds_merge_block_build",
                    [](fly::DSDesign& dst,
                       fly::DSBlockBuildData& block_data) {
    return fly::ds_merge_block_build(dst, block_data);
});

// S8 密度合并：层级树 + per-DEF 产物（blocks/nets 借指针列表）→ 全局
// 密度图（格值分摊 D10 A + 三通道独立分列）
FLY_EXPORT_FUNCTION("ds_merge_global_density",
                    [](const fly::DSHierTree& tree, nb::list blocks,
                       nb::list nets) {
    fly::CMVector<const fly::DSBlockBuildData*> block_ptrs;
    for (nb::handle item : blocks) {
        block_ptrs.push_back(&nb::cast<const fly::DSBlockBuildData&>(item));
    }
    fly::CMVector<const fly::DSNetBuildData*> net_ptrs;
    for (nb::handle item : nets) {
        net_ptrs.push_back(&nb::cast<const fly::DSNetBuildData&>(item));
    }
    return nb::cast(fly::ds_merge_global_density(tree, block_ptrs, net_ptrs));
});

// S8 分区决策：合成负载（通道比重散参构造 DSDensityWeights，逐层系数
// 本期不暴露——裁定 ⑥ 接口保留在 C++ 结构）+ 三键优先级
// target_partitions > partition_count > partition_target_density →
// 分区表
FLY_EXPORT_FUNCTION("ds_decide_partitions",
                    [](const fly::DSDensityGrid& global,
                       const fly::DSStack& stack, double w_instance,
                       double w_metal, double w_via,
                       const CMString& target_partitions,
                       int partition_count, int64_t target_density) {
    fly::DSDensityWeights weights;
    weights.instance_ = w_instance;
    weights.metal_ = w_metal;
    weights.via_ = w_via;
    return nb::cast(fly::ds_decide_partitions(
        global, stack, weights, target_partitions, partition_count,
        target_density));
});

// ── S7 跨块连接归并（并查集；2026-09-13 裁定：仅 port 相连网、两层树、
// 单对象）────────────────────────────────────────────────────────────

// S7 正式产物只读面（find 恒一步——不在表 = 自身；members 升序含 root
// 自身；class_count 含单成员悬空类）
FLY_EXPORT_CLASS(fly::DSNetUnion, "EXDSNetUnion")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("dangling_count",
                             &fly::DSNetUnion::dangling_count_)
    FLY_EXPORT_READONLY_PROPERTY("class_count", [](const fly::DSNetUnion& u) {
        return static_cast<int>(u.class_count());
    })
    FLY_EXPORT_DEF("find", [](const fly::DSNetUnion& u,
                              uint64_t net_global_id) {
        return u.find(net_global_id);
    })
    FLY_EXPORT_DEF("members", [](const fly::DSNetUnion& u, uint64_t root) {
        nb::list out;
        const auto* m = u.members(root);
        if (m != nullptr) {
            for (const uint64_t id : *m) out.append(id);
        }
        return out;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSNetUnion);

// S7 per-DEF 局部收集产物（临时对象，汇总合并后 remove；观测面仅规模
// 计数——边/端口明细消费在 C++ 汇总）
FLY_EXPORT_CLASS(fly::DSNetUnionSlice, "EXDSNetUnionSlice")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("block_name", &fly::DSNetUnionSlice::block_name_)
    FLY_EXPORT_READONLY_PROPERTY("edge_count",
                                 [](const fly::DSNetUnionSlice& s) {
        return static_cast<int>(s.edges_.size());
    })
    FLY_EXPORT_READONLY_PROPERTY("port_net_count",
                                 [](const fly::DSNetUnionSlice& s) {
        return static_cast<int>(s.port_net_ids_.size());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSNetUnionSlice);

// S7 局部收集：每父块 DEF 一调用（child_nets = 该 def 引用的各子定义网
// 产物列表，编排侧经 ds_net_union_child_indexes 定位）
FLY_EXPORT_FUNCTION("ds_collect_net_union_slice",
                    [](const fly::DSHierTree& tree,
                       const fly::DSNetBuildData& parent_nets,
                       nb::list child_nets) {
    fly::CMVector<const fly::DSNetBuildData*> child_ptrs;
    for (nb::handle item : child_nets) {
        child_ptrs.push_back(&nb::cast<const fly::DSNetBuildData&>(item));
    }
    return nb::cast(
        fly::ds_collect_net_union_slice(tree, parent_nets, child_ptrs));
});

// S7 全局汇总：合并全部局部边集 → 两层化 + root 规范化 + 悬空计数
FLY_EXPORT_FUNCTION("ds_build_net_union",
                    [](const fly::DSHierTree& tree, nb::list slices) {
    fly::CMVector<const fly::DSNetUnionSlice*> slice_ptrs;
    for (nb::handle item : slices) {
        slice_ptrs.push_back(&nb::cast<const fly::DSNetUnionSlice&>(item));
    }
    return nb::cast(fly::ds_build_net_union(tree, slice_ptrs));
});

// S7 编排辅助：def 序号 → 其引用的子定义序号集（block_names = def_paths
// 序 block 名清单；树扫描一遍，slice 任务据此只读所需子定义网产物）
FLY_EXPORT_FUNCTION("ds_net_union_child_indexes",
                    [](const fly::DSHierTree& tree, nb::list block_names,
                       int index) {
    fly::CMVector<CMString> names;
    for (nb::handle item : block_names) {
        names.push_back(nb::cast<CMString>(item));
    }
    return nb::cast(fly::ds_net_union_child_indexes(
        tree, names, static_cast<uint32_t>(index)));
});

// ── S9 flatten 展平 + 分区保存（2026-09-13 裁定补记①-⑤；分片中间形态
// 与四类正式对象共用数据结构）────────────────────────────────────────

// 分区连接项（INST_CONNECTIONS / NET_CONNECTIONS 共用条目形态；只读面）
FLY_EXPORT_CLASS(fly::DSPartConnection, "EXDSPartConnection")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("instance_global_id",
                             &fly::DSPartConnection::instance_global_id_)
    FLY_EXPORT_READONLY_ATTR("net_global_id",
                             &fly::DSPartConnection::net_global_id_)
    FLY_EXPORT_READONLY_ATTR("pin_name", &fly::DSPartConnection::pin_name_)
    FLY_EXPORT_READONLY_PROPERTY("is_port", [](const fly::DSPartConnection& c) {
        return c.is_port();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPartConnection);

// 分区几何条目（net wire/rect 图形 + via 展开图形 + DEF obstruction）
FLY_EXPORT_CLASS(fly::DSGeomEntry, "EXDSGeomEntry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("layer_id", &fly::DSGeomEntry::layer_id_)
    FLY_EXPORT_READONLY_PROPERTY("rect", [](const fly::DSGeomEntry& e) {
        return rect_to_tuple(e.rect_);
    })
    FLY_EXPORT_READONLY_ATTR("via_cell_id", &fly::DSGeomEntry::via_cell_id_)
    FLY_EXPORT_READONLY_PROPERTY("is_via", [](const fly::DSGeomEntry& e) {
        return e.is_via();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_obs", [](const fly::DSGeomEntry& e) {
        return e.is_obs();
    })
    FLY_EXPORT_READONLY_PROPERTY("is_primary", [](const fly::DSGeomEntry& e) {
        return e.is_primary();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSGeomEntry);

// /GEOMETRY：net global id → 条目集 + 跨分区网集合（net 0 = OBS 桶）
FLY_EXPORT_CLASS(fly::DSPartitionGeometry, "EXDSPartitionGeometry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("net_count",
                                 [](const fly::DSPartitionGeometry& g) {
        return static_cast<int>(g.nets_.size());
    })
    FLY_EXPORT_DEF("entries_of", [](const fly::DSPartitionGeometry& g,
                                    uint64_t net_global_id) {
        nb::list out;
        const auto* entries = g.entries_of(net_global_id);
        if (entries != nullptr) {
            for (const auto& e : *entries) out.append(e);
        }
        return out;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("is_crossing", [](const fly::DSPartitionGeometry& g,
                                     uint64_t net_global_id) {
        return g.is_crossing(net_global_id);
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPartitionGeometry);

// /INSTANCES：instance global id → DSInstance 副本
FLY_EXPORT_CLASS(fly::DSPartInstances, "EXDSPartInstances")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("size", [](const fly::DSPartInstances& p) {
        return static_cast<int>(p.size());
    })
    FLY_EXPORT_DEF("get", [](const fly::DSPartInstances& p,
                             uint64_t global_id)
                       -> const fly::DSInstance& {
        return p.items_.at(global_id);
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("ids", [](const fly::DSPartInstances& p) {
        nb::list out;
        for (const auto& [gid, _] : p.items_) out.append(gid);
        return out;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPartInstances);

// /INST_CONNECTIONS：instance global id → 连接项列表（跟随 instance 副本）
FLY_EXPORT_CLASS(fly::DSPartInstConnections, "EXDSPartInstConnections")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("size",
                                 [](const fly::DSPartInstConnections& p) {
        return static_cast<int>(p.size());
    })
    FLY_EXPORT_DEF("connections_of", [](const fly::DSPartInstConnections& p,
                                        uint64_t global_id) {
        nb::list out;
        auto it = p.items_.find(global_id);
        if (it != p.items_.end()) {
            for (const auto& c : it->second) out.append(c);
        }
        return out;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPartInstConnections);

// /NET_CONNECTIONS：net global id → 连接项列表（跟随 net 副本）
FLY_EXPORT_CLASS(fly::DSPartNetConnections, "EXDSPartNetConnections")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("size",
                                 [](const fly::DSPartNetConnections& p) {
        return static_cast<int>(p.size());
    })
    FLY_EXPORT_DEF("connections_of", [](const fly::DSPartNetConnections& p,
                                        uint64_t net_global_id) {
        nb::list out;
        auto it = p.items_.find(net_global_id);
        if (it != p.items_.end()) {
            for (const auto& c : it->second) out.append(c);
        }
        return out;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPartNetConnections);

// 四类聚合容器（分片中间形态 + 合并工作形态；Python 面 = 规模观测 +
// 编排侧分片合并）
FLY_EXPORT_CLASS(fly::DSPartitionProduct, "EXDSPartitionProduct")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_PROPERTY("instance_count",
                                 [](const fly::DSPartitionProduct& p) {
        return static_cast<int>(p.instances_.size());
    })
    FLY_EXPORT_READONLY_PROPERTY("geometry_net_count",
                                 [](const fly::DSPartitionProduct& p) {
        return static_cast<int>(p.geometry_.nets_.size());
    })
    FLY_EXPORT_DEF("merge_from", [](fly::DSPartitionProduct& p,
                                    const fly::DSPartitionProduct& src) {
        p.merge_from(src);
    })
    // 四类成员只读访问（merge 任务按类拆写四类正式对象；引用零拷贝）
    FLY_EXPORT_DEF("geometry", [](const fly::DSPartitionProduct& p)
                               -> const fly::DSPartitionGeometry& {
        return p.geometry_;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("instances", [](const fly::DSPartitionProduct& p)
                                -> const fly::DSPartInstances& {
        return p.instances_;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("inst_connections", [](const fly::DSPartitionProduct& p)
                                       -> const fly::DSPartInstConnections& {
        return p.inst_connections_;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_DEF("net_connections", [](const fly::DSPartitionProduct& p)
                                      -> const fly::DSPartNetConnections& {
        return p.net_connections_;
    }, nb::rv_policy::reference_internal)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::DSPartitionProduct);

// S9 展开算法（每 block 定义一调用；分片列表 [(partition_id, product)]。
// 分区表取自 design 容器（S8 写定 partitions_）；pin_geoms = None 时不做
// 电源引脚预展开）。SliceList 别名行置于宏外——模板实参逗号会拆分宏形参。
using DSSliceList = CMVector<std::pair<uint32_t, fly::DSPartitionProduct>>;
FLY_EXPORT_FUNCTION("ds_flatten_block",
                    [](const fly::DSHierTree& tree,
                       const fly::DSBlockBuildData& block,
                       const fly::DSNetBuildData& nets,
                       const fly::DSBlockNames& names,
                       const fly::DSDesign& design,
                       const fly::DSPinGeometry* pin_geoms) {
    DSSliceList slices = fly::ds_flatten_block(tree, block, nets, names,
                                               design, pin_geoms,
                                               design.partitions_);
    nb::list out;
    for (auto& [pid, product] : slices) {
        out.append(nb::make_tuple(pid, nb::cast(std::move(product))));
    }
    return out;
});

// ── R7 全局 name 组装（㊻ 注入式轻壳 + ㊵② 统一组装工厂）────────────

// DSNameMapperT<uint64_t>（EXDSNameMapper；DSInstanceNameMapper/
// DSNetNameMapper 为同型 using 别名，维度经 kind 运行时区分——区间
// 换算公式不同）。hasher 注入主口 = block cell id、便利口 = cell 名；
// 未命中查询返回 None（不透出 kInvalidId 哨兵）。无 pickle 面（㊻：
// 注入式轻壳不序列化不落盘——运行时经工厂/注入构造；树引用为 design
// 内观察，Python 侧须同时持有 design 引用）。
FLY_EXPORT_CLASS(fly::DSInstanceNameMapper, "EXDSNameMapper")
    FLY_EXPORT_INIT()
    // 注入主口：block 标识 = cell id（hasher 级共享注入——从伴生对象取
    // CMSharedPtr const 化、零拷贝；重复注入同键覆盖；names=None 撤销）
    FLY_EXPORT_DEF("set_block_hasher_by_cell_id",
                   [](fly::DSInstanceNameMapper& m, uint32_t cell_id,
                      const fly::DSBlockNames* names, int kind) {
        if (names == nullptr) {
            m.set_block_hasher(
                cell_id,
                CMSharedPtr<const fly::DSNameHasherT<uint64_t>>());
            return;
        }
        m.set_block_hasher(
            cell_id,
            kind == 0 ? fly::CMSharedPtr<const fly::DSInstanceNameHasher>(
                            names->instance_names_)
                      : fly::CMSharedPtr<const fly::DSNetNameHasher>(
                            names->net_names_));
    })
    // 注入便利口：block cell 名经树解析（取首个同名 block 定义）
    FLY_EXPORT_DEF("set_block_hasher_by_cell_name",
                   [](fly::DSInstanceNameMapper& m, const CMString& cell_name,
                      const fly::DSBlockNames* names, int kind) {
        if (names == nullptr) {
            m.set_block_hasher(
                cell_name,
                CMSharedPtr<const fly::DSNameHasherT<uint64_t>>());
            return;
        }
        m.set_block_hasher(
            cell_name,
            kind == 0 ? fly::CMSharedPtr<const fly::DSInstanceNameHasher>(
                            names->instance_names_)
                      : fly::CMSharedPtr<const fly::DSNetNameHasher>(
                            names->net_names_));
    })
    FLY_EXPORT_READONLY_PROPERTY("injected_count",
                                 [](const fly::DSInstanceNameMapper& m) {
        return static_cast<int>(m.injected_count());
    })
    // 正向组装：完整层级实例名路径（'/' 分隔）→ global id
    FLY_EXPORT_DEF("get_global_id", [](const fly::DSInstanceNameMapper& m,
                                       const CMString& full_hier_name) {
        const uint64_t id = m.get_global_id(full_hier_name);
        if (id == fly::DSInstanceNameMapper::kInvalidId) {
            return std::optional<uint64_t>();
        }
        return std::optional<uint64_t>(id);
    })
    // 反向组装：global id → 完整层级路径（未命中/未注入 → None）
    FLY_EXPORT_DEF("get_full_name", [](const fly::DSInstanceNameMapper& m,
                                       uint64_t global_id) {
        const CMString name = m.get_full_name(global_id);
        if (name.empty()) return std::optional<CMString>();
        return std::optional<CMString>(name);
    });

// ㊵②+㊻ 统一组装工厂（Python 统一加载 API 的 C++ 底座）：遍历
// DSBlockNames 集，block 名经容器 cell hasher 解析 cell id 注入。kind：
// 0 = INSTANCE、1 = NET。返回的 mapper 持 design 内树的观察指针（design
// 生命周期覆盖之——Python 侧同时持有 design 与 mapper 引用）。
FLY_EXPORT_FUNCTION("ds_make_name_mapper",
                    [](const fly::DSDesign& design, nb::list names,
                       int kind) {
    fly::CMVector<const fly::DSBlockNames*> name_ptrs;
    for (nb::handle item : names) {
        name_ptrs.push_back(&nb::cast<const fly::DSBlockNames&>(item));
    }
    return nb::cast(fly::ds_make_name_mapper(
        design, name_ptrs,
        kind == 0 ? fly::DSNameMapperKind::INSTANCE
                  : fly::DSNameMapperKind::NET));
});
}
