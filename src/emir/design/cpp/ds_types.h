#pragma once

// =============================================================================
// design db 的 C++ 数据结构（design-db-s1-s4b 实施计划 §2 + 第二阶段
// R4/R5 重构 + R7 name 体系收敛）：DSStack（层堆叠，独立对象）→
// DSCell/DSViaCell → DSDesign（容器）+ DSPinTables/DSPinGeometry（运行时
// 注入对象）+ S5a/S5b per-DEF 产物 + S6 层级树。
//
// R7（㊱㊳㊴㊸㊹㊻）：①name 存储分层——DSPin/DSInstance 不存 name
// （layer/cell/via cell 双存保留）；②id 位宽分组——instance/net/via
// instance id 与层级树区间/self_global_id = uint64_t（instance 可达
// 10⁹ 级），cell/pin/via cell/layer id 维持 uint32_t（十万级以内），
// 坐标 int32 不受影响（物理量 vs 数量）；③无效 id 哨兵 = id 类型最大
// 值（kInvalidId，is_valid_id 纯值判定）；④local 名空间 = hasher 底座
// （ds_name_hasher.h 的 DSNameHasherT<IdT> + 实体语义别名），全局组装
// 见 ds_name_mapper.h（DSNameMapperT 注入式轻壳）。
//
// port 复用 DSPin（㉙：CM_FLAGS port 位 + placement_status_）、block 复用
// DSCell（㉙：block_cell 位 + bbox_/polygon_/def_path_ 等 block 场景字段，
// DSBlock/DSPort 类型已删除）；VIARULE 不单独保存（㉚：解析期按规则默认
// 参数直接展开为模板 DSViaCell）。
//
// 命名遵循裁定 12（DS 前缀类名）；存储形态遵循裁定 ㉒（小对象直接存、
// 注入用 CMSharedPtr、禁止裸指针）；属性访问经 CM_PROPERTY 生成五件套
// （容器字段除外——走专门 add/count/at 接口，规避大容器 set 拷贝）；
// 全部序列化字段经 FLY_SERIALIZE（运行时字段显式排除，裁定 ⑱/㊵②）。
//
// 枚举字段按设计以 uint8_t 存储（赋值处 static_cast，序列化走整型
// 路径）；坐标 int32（DBU）、中间量/面积 int64、geometry 实例化集合
// {int32, int64, double}。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <common/types/cpp/flags_macro.h>
#include <common/types/cpp/property_macro.h>
#include <container/cpp/container_aliases.h>
#include <container/cpp/lookup_table.h>
#include <emir/design/cpp/ds_name_hasher.h>
#include <geometry/cpp/geometry_types.h>
#include <geometry/cpp/transform.h>

#include <cassert>
#include <cstdint>
#include <optional>

namespace fly {

// —— 枚举（uint8_t 底层；字段以 uint8_t 存储，跨序列化即整数）——

// 层类型：布线层 / 切割层
enum class DSLayerType : uint8_t { ROUTING, CUT };
// 方向（布线层用）：水平 / 垂直 / 无
enum class DSDirection : uint8_t { HORIZONTAL, VERTICAL, NONE };
// pin 类型：信号 / 电源 / 地
enum class DSPinType : uint8_t { SIGNAL, POWER, GROUND };
// pin 方向：输入 / 输出 / 双向
enum class DSPinDirection : uint8_t { INPUT, OUTPUT, INOUT };
// pin 放置状态（P3 裁定：仅 port 场景有效；NONE = 非 port pin 的默认
// 无效值，FIXED/COVER/PLACED = DEF PINS 放置状态）
enum class DSPinPlacementStatus : uint8_t { NONE = 0, FIXED, COVER, PLACED };
// instance 放置状态（S5a COMPONENTS；独立枚举与 DEF 语义对齐——设计
// 说明：不复用 DSPinPlacementStatus 的 NONE 值域，因其「非 port pin
// 无效值」语义与 instance 的 UNPLACED（DEF 无放置子句的合法状态，
// D14 兜底计数）不同；数值序保持 FIXED/COVER/PLACED 与之一致。defi 的
// DEFI_COMPONENT_*（1..5，含 6.0 SOFTFIXED）由适配层映射）
enum class DSPlacementStatus : uint8_t { UNPLACED = 0, FIXED, COVER, PLACED };

// 运行时注入对象前置声明（⑰：指针共享非拷贝）
class DSPinTables;
class DSPinGeometry;
// lib 库容器前置声明（S3 merge_lib 用；完整定义在 emir/lib）
class LIBLibrary;

// —— 2.1 Stack（独立类型，裁定 ⑭：不进 DSDesign，独立对象持久化）——

// 单层（tech lef LAYER 语句）
class DSLayer {
public:
    // 层名（如 M1、VIA1）
    CMString name_;
    // 层 id（add_layer 分配回填、随序列化持久化；当前与 layers_ 下标
    // 巧合一致，但下标不再作为约定——下游引用一律以 id 为准，读取走
    // DSStack::layer_by_id）
    uint32_t id_ = 0;
    // 布线层 / 切割层（DSLayerType）
    uint8_t type_ = static_cast<uint8_t>(DSLayerType::ROUTING);
    // 水平 / 垂直 / 无（DSDirection，布线层用）
    uint8_t direction_ = static_cast<uint8_t>(DSDirection::NONE);
    // 默认线宽（DBU）
    int32_t default_width_ = 0;
    // 布线 pitch（DBU）
    int32_t pitch_ = 0;
    // 间距表（DBU；轻量小容器，CM_PROPERTY 全套）
    CMVector<int32_t> spacing_;
    // 最小面积（DBU²）
    int64_t min_area_ = 0;

    CM_PROPERTY(name)
    CM_PROPERTY(id)
    CM_PROPERTY(type)
    CM_PROPERTY(direction)
    CM_PROPERTY(default_width)
    CM_PROPERTY(pitch)
    CM_PROPERTY(spacing)
    CM_PROPERTY(min_area)

    FLY_SERIALIZE(name_, id_, type_, direction_, default_width_, pitch_,
                  spacing_, min_area_)
};

// 层堆叠：自底向上堆叠顺序（routing/cut 交错，stack 顺序即层表序）。
// 层 id 显式保存在 DSLayer.id_（add_layer 分配回填、随序列化持久化；
// 现与 layers_ 下标巧合一致，但下标不再作为约定——按 id 读取走
// layer_by_id）；邻接关系经堆叠顺序导出。
class DSStack {
public:
    // DBU 基准说明（换算系数 dbu_per_micron_）
    CMString dbu_basis_;
    // 1 µm = N DBU。裁定 ㉝：全局基准恒 1000（不再跟随 tech lef 的
    // DATABASE MICRONS 声明值）；各 lef/def 的几何值按各自单位换算到
    // 本基准后入库，换算一律取 kGlobalDbuPerMicron 单一权威来源
    int32_t dbu_per_micron_ = kGlobalDbuPerMicron;
    // 全局 DBU 基准（㉝ 恒定值；LEF 几何为 µm 浮点恒乘此值，DEF 坐标按
    // v × 本值 / def_units 换算；lef 间 DBU 声明不一致不再校验不 raise）
    static constexpr int32_t kGlobalDbuPerMicron = 1000;
    // 制造网格
    int32_t manufacturing_grid_ = 0;
    // 自底向上层表
    CMVector<DSLayer> layers_;

    // name → 层 id 的双向 hasher（R7 ㊸：原 layer_index_ 惰性索引收编
    // 进 hasher 体系——DSLayerNameHasher 序列化持久化，find_layer 直查
    // 不再惰性重建；layer 量小无负担）
    DSLayerNameHasher layer_names_;

    // find_layer 未命中的层 id（R7 ㊴ 收编：值不变 = 类型最大值，与
    // DSLayerNameHasher::kInvalidId 等值，指向 hasher 哨兵口径）
    static constexpr uint32_t kNoLayer = DSLayerNameHasher::kInvalidId;

    // 构建期接口。add_layer 追加并分配回填层 id（重名由调用方负责——
    // lef 间重复层的 DSGN 语义在适配层处理；name 索引重名保留首个）；
    // find_or_add_layer 重名保留首份返回既有 id。
    uint32_t add_layer(DSLayer&& layer);
    uint32_t find_layer(const CMString& name) const;      // 未命中 kNoLayer
    uint32_t find_or_add_layer(DSLayer&& layer);

    size_t layer_count() const { return layers_.size(); }
    // 按 id 取层（层 id 读取的权威接口）；越界为调用方契约错误
    // （debug 断言）
    const DSLayer& layer_by_id(uint32_t layer_id) const {
        assert(layer_id < layers_.size());
        return layers_[layer_id];
    }
    // 旧名保留（Python 导出面沿用），语义同 layer_by_id
    const DSLayer& layer_at(uint32_t layer_id) const {
        return layer_by_id(layer_id);
    }

    CM_PROPERTY(dbu_basis)
    CM_PROPERTY(dbu_per_micron)
    CM_PROPERTY(manufacturing_grid)

    FLY_SERIALIZE(dbu_basis_, dbu_per_micron_, manufacturing_grid_, layers_,
                  layer_names_)
};

// 层名解析共享入口（dev-rules §7 兜底）：命中返回层 id；未命中发
// DSGN::0010 提醒（配额限流由 MessageRegistry try_emit 天然保证）并
// 返回 DSStack::kNoLayer——条目级丢弃决策与计数由调用方按条目类型执行
//（S1/S2/S4/S5b 全部层引用点经此解析，模块内不再有层引用 raise 路径）。
uint32_t ds_resolve_layer_id(const CMString& name, const DSStack& stack);

// —— 几何引用（design 业务结构）——

// 几何引用——带 layer id 的最小几何引用（下游 port/OBS 逐层几何，
// layer id = DSLayer::id_，由 stack add_layer 分配、现与层表下标巧合
// 一致）。原 container/geometry 的 CMGeometryRef 随 geometry 独立模块化
// （R1）迁入 design：业务结构用 DS 前缀（GEO 前缀保留给纯几何类型，见
// DEVELOPMENT_GUIDELINES.md Section 2.2）。预留多边形变体扩展点：现阶段
// 矩形是唯一几何需求，故取 { layer_id + 矩形 } 最小实现；后续需引用多
// 边形等任意图形时，在此引入图形变体（tag + variant 或等价机制）并同步
// 升级序列化字段表。
struct DSShapeRef {
    // 所属层 id（stack 层表下标）
    uint32_t layer_id_ = 0;
    // 几何矩形（DBU，int32 坐标）
    GEORect rect_;

    CM_PROPERTY(layer_id)
    CM_PROPERTY(rect)

    FLY_SERIALIZE(layer_id_, rect_)
};

// —— 2.2 cell / pin ——

// 简化 pin（裁定 ⑰）：基础属性 + R4 的全局 id 与放置状态 + R5 的 port
// 复用标记（㉙：port = DSPin，不再有独立 DSPort 类型）。
// R7 ㊱ name 分层存储：**DSPin 不存 name**（跨 cell 同名 pin 过多）——
// pin 名仅在 DSDesign 的 DSPinNameHasher（键 = "cell_name/pin_name"），
// pin 身份 = 全局平铺 pin_id_（D1）；名字查询经 DSDesign::pin_name_of
// （或 pin hasher 组合键反查）。
class DSPin {
public:
    // 信号 / 电源 / 地（DSPinType；lef USE）
    uint8_t type_ = static_cast<uint8_t>(DSPinType::SIGNAL);
    // 输入 / 输出 / 双向（DSPinDirection）
    uint8_t direction_ = static_cast<uint8_t>(DSPinDirection::INPUT);
    // 全局平铺 pin id（D1，与 DSDesign pin hasher 同源）：建库链路
    // （S2 cell lef / S4 DEF port）在 register_pin 分配全局 id 时同步
    // 回填；merge 重排后回填发生在重挂后（ds_merge_cell_lef）
    uint32_t pin_id_ = 0;
    // 放置状态（DSPinPlacementStatus；P3：仅 port 场景有效，S4 的 DEF
    // PINS 放置状态解析填入）
    uint8_t placement_status_ =
        static_cast<uint8_t>(DSPinPlacementStatus::NONE);
    // 来源/种类标记（㉙）：port 位 = DEF PINS 的 block 级引脚；后续 pin
    // flags 扩展位顺延
    CM_FLAGS(uint8_t, port)

    CM_PROPERTY(type)
    CM_PROPERTY(direction)
    CM_PROPERTY(pin_id)
    CM_PROPERTY(placement_status)

    FLY_SERIALIZE(type_, direction_, pin_id_, placement_status_, flags_)
};

// 单一 cell 结构（裁定 ⑯：lef macro + block cell 同空间；㉙ block 复用
// DSCell，DSBlock 类型已删除）
class DSCell {
public:
    CMString name_;
    // lib 来源库名（可空 = lef 无 lib 对应；S3 merge 填充）
    CMString library_name_;
    // 放置包围盒（DBU，㉞：GEORect 直接存、含左下角坐标——左下角可非
    // (0,0)，如 die 中心原点 DEF；lef cell = (0,0)-(SIZE)；block cell =
    // DIEAREA 矩形）。尺寸由 bbox 派生（width()/height()），不再存
    // width_/height_ 字段
    GEORect bbox_;
    // lef ORIGIN（DBU）。block 场景 = −diearea 左下角（P7 溯源语义：
    // origin_ 与 LEF ORIGIN 同位——放置参考点 = 原坐标系 −origin_ 点）
    int32_t origin_x_ = 0;
    int32_t origin_y_ = 0;
    // CORE/BLOCK/...
    CMString class_;
    CMString site_;
    // block 场景字段（㉙，flags 的 block_cell 位判别后使用）：来源 DEF
    // 完整路径（可追溯，dev-rules §7）+ DEF UNITS DIST MICRONS 值
    CMString def_path_;
    int32_t def_units_per_micron_ = 0;
    // 来源与种类标记（㉗ 示例集 + ㉞ is_polygon 位）：
    //   fake_cell（⑲ fake 兜底 cell，S5a 机制生成）/ std_cell /
    //   lef_cell / lib_cell（S3 merge 匹配）/ macro_cell /
    //   block_cell（S4 头扫描合成）/ polygon（DIEAREA 真实多边形，见下）
    CM_FLAGS(uint32_t, fake_cell, std_cell, lef_cell, lib_cell, macro_cell,
             block_cell, polygon)
    // 简化 pin 集（lib+lef 并集，⑯ merge 产出；block 的 port 即此处的
    // port 位 pin，㉙）
    CMVector<DSPin> pins_;
    // 禁布区几何（D19 入库；逐层带 layer id）
    CMVector<DSShapeRef> obs_;
    // DIEAREA 真实多边形（㉞ 双存之 polygon 侧，DBU：is_polygon 位为真
    // 时存全点集；2 点矩形 DIEAREA 时为空、is_polygon 复位）。bbox 恒存
    // （业务流程一律用 bbox，polygon 仅供精确几何场景）
    GEOPolygon polygon_;

    // 两个不序列化字段（⑰/⑱）：lib 功耗/时序表 + lef 逐层 pin 几何，
    // 运行时由 DSDesign::get_cell 按指针注入（共享非拷贝），不落库
    CMSharedPtr<DSPinTables> pin_tables_;
    CMSharedPtr<DSPinGeometry> pin_geometry_;

    // bbox 派生尺寸便捷接口（㉞：替代已删除的 width_/height_ 字段）
    int32_t width() const { return bbox_.width(); }
    int32_t height() const { return bbox_.height(); }
    // polygon_ 的读写口（flag 名 polygon 占用 is_/set_/reset_polygon，
    // 经 CM_PROPERTY(polygon) 暴露会与其 set_polygon 冲突，故手写异名
    // 写入口）
    const GEOPolygon& get_polygon() const { return polygon_; }
    void assign_polygon(GEOPolygon p) {
        polygon_ = std::move(p);
    }

    // 轻量标量/字符串字段经 CM_PROPERTY（含 CMSharedPtr——赋值即指针
    // 拷贝）；容器字段 pins_/obs_ 走专门接口（㉒：不提供大容器 set 拷贝）
    CM_PROPERTY(name)
    CM_PROPERTY(library_name)
    CM_PROPERTY(bbox)
    CM_PROPERTY(origin_x)
    CM_PROPERTY(origin_y)
    CM_PROPERTY(class)
    CM_PROPERTY(site)
    CM_PROPERTY(def_path)
    CM_PROPERTY(def_units_per_micron)
    CM_PROPERTY(pin_tables)
    CM_PROPERTY(pin_geometry)

    // 构建期容器接口，返回下标
    uint32_t add_pin(DSPin&& pin) {
        pins_.push_back(std::move(pin));
        return static_cast<uint32_t>(pins_.size() - 1);
    }
    uint32_t add_obs(const DSShapeRef& obs) {
        obs_.push_back(obs);
        return static_cast<uint32_t>(obs_.size() - 1);
    }

    size_t pin_count() const { return pins_.size(); }
    const DSPin& pin_at(uint32_t i) const {
        assert(i < pins_.size());
        return pins_[i];
    }
    size_t obs_count() const { return obs_.size(); }
    const DSShapeRef& obs_at(uint32_t i) const {
        assert(i < obs_.size());
        return obs_[i];
    }

    // 字段表显式排除 pin_tables_/pin_geometry_（⑱：注入后 write→load 为空）
    FLY_SERIALIZE(name_, library_name_, bbox_, origin_x_, origin_y_, class_,
                  site_, def_path_, def_units_per_micron_, flags_, polygon_,
                  pins_, obs_)
};

// —— 2.3 instance（R6：transform 业务接入；S5a COMPONENTS 产物）——

// instance（cell 在设计中的实例化放置，⑧）：引用 cell id + pos/orient
// transform + 放置状态 + 权重。transform_.offset_ = pos（cell 原始坐标
// 系 (0,0) 点的全局位置，㉜ 最终形态——「经过旋转的 origin 点」的精确
// 化，由 place_from_def 自 DEF placement 换算一次）；orient_ = 放置
// 朝向（defin 回调整型直转）。instance 全局坐标 = R(orient)·m + pos。
// R7 ㊱ name 分层存储：**DSInstance 不存 name**——实例名仅在
// DSInstanceNameHasher（DSBlockBuildData 的 per-DEF local 名空间，随
// DSBlockNames_<i> 伴生对象落盘 ㊵②），内部业务全程以 local id 为键。
// local instance id 分配语义（⑧：从 1 起、local 0 = block 自身占位）
// 在 S5a per-DEF 产物 DSBlockBuildData（ds_types.h 下方 S5a 节）。
class DSInstance {
public:
    DSInstance() = default;

    // 引用的 cell id（全局 cell 编号空间；fake cell 引用见 ⑲/⑳）
    uint32_t cell_id_ = 0;
    // 放置变换（pos + orient 二元组，R6）
    GEOTransform transform_;
    // 放置状态（DSPlacementStatus；UNPLACED 实例 D14 兜底计数、不入
    // 密度通道）
    uint8_t placement_status_ =
        static_cast<uint8_t>(DSPlacementStatus::UNPLACED);
    // OPTIONAL weight（DEF COMPONENTS + WEIGHT），缺省 0 = 未给
    double weight_ = 0.0;

    CM_PROPERTY(cell_id)
    CM_PROPERTY(transform)
    CM_PROPERTY(placement_status)
    CM_PROPERTY(weight)

    FLY_SERIALIZE(cell_id_, transform_, placement_status_, weight_)
};

// —— 2.4 S5a per-DEF 产物（⑬ 大体量独立对象，不进 DSDesign）——

// 密度采样格网（S8 完整机制前的实例面积通道最小落地）：固定尺寸格子 +
// 原点 + 行列数 + 计数矩阵（行主序，int64）。统计口径 = 图形计数（⑥，
// 2026-09-09 裁定）：与格交叠出现过的实例 footprint 即计入该格（跨多格
// 则多格各计一次；半开区间 overlaps——共享边界线不算）。逐层密度系数
// （alpha 通道）本版全 1、接口随 S8 引入（密度分类分层与合并见方案
// design-db-plan.md §4.1）；UNPLACED 实例不计（D14）。
class DSDensityGrid {
public:
    // 格网原点（DBU）
    int32_t origin_x_ = 0;
    int32_t origin_y_ = 0;
    // 格宽 / 格高（DBU，正方形网格时相等）
    int32_t bin_width_ = 0;
    int32_t bin_height_ = 0;
    // 列数 / 行数
    uint32_t cols_ = 0;
    uint32_t rows_ = 0;
    // 计数矩阵（行主序：counts_[row * cols_ + col]；实例面积通道）
    CMVector<int64_t> counts_;

    CM_PROPERTY(origin_x)
    CM_PROPERTY(origin_y)
    CM_PROPERTY(bin_width)
    CM_PROPERTY(bin_height)
    CM_PROPERTY(cols)
    CM_PROPERTY(rows)

    // 配置格网（清零计数矩阵）。bin 尺寸须为正；未配置（cols_==0）时
    // accumulate_footprint 为 no-op。
    void configure(int32_t origin_x, int32_t origin_y, int32_t bin_width,
                   int32_t bin_height, uint32_t cols, uint32_t rows);

    // 实例面积通道：footprint 与格交叠的全部格子各 +1。负方向越出格网
    // 原点的部分截断；未配置时 no-op。
    void accumulate_footprint(const GEORect& footprint);

    // 格 (col,row) 计数（越界返回 0）
    int64_t cell_count(uint32_t col, uint32_t row) const;
    // 全格计数总和
    int64_t total_count() const;

    // —— ⑥ 逐层分列通道（S5b 网内容）——
    // 密度分类分层保存：金属（wire 段矩形 / rect 图形）与通孔（via cut
    // 图形）两类，键 = layer id → 计数矩阵（行主序同 counts_，格网参数
    // 共用上方字段）。逐层密度系数 S8 引入（首版权重全 1、接口已留），
    // 权重为建库一次性配置、db freeze 后不可变——数据组织按分类分层。
    CMUnorderedMap<uint32_t, CMVector<int64_t>> metal_layer_counts_;
    CMUnorderedMap<uint32_t, CMVector<int64_t>> via_layer_counts_;

    // 网侧图形计数：shape 与格交叠的全部格子各 +1（半开区间口径与
    // accumulate_footprint 一致）；未配置格网时 no-op
    void accumulate_layer_shape(uint32_t layer_id, bool via_channel,
                                const GEORect& shape);
    // 单层通道计数总和（越界层返回 0）
    int64_t layer_total(uint32_t layer_id, bool via_channel) const;
    // 通道全层计数总和
    int64_t channel_total(bool via_channel) const;
    // 语义化别名（金属 / 通孔通道）
    int64_t metal_total() const { return channel_total(false); }
    int64_t via_total() const { return channel_total(true); }

    FLY_SERIALIZE(origin_x_, origin_y_, bin_width_, bin_height_, cols_, rows_,
                  counts_, metal_layer_counts_, via_layer_counts_)
};

// S5a 实例侧统计（StatsNode 产出；汇总任务合并输出。统计标量直接公开
// 访问，同 DSDefParseStats 风格）
class DSInstanceStats {
public:
    // 收录 leaf 实例数（不含 local 0 占位）
    uint64_t instance_count = 0;
    // UNPLACED 实例数（D14 兜底计数）
    uint64_t unplaced_count = 0;
    // 本 DEF 生成的 fake cell 数（⑲/⑳ 种类数）
    uint64_t fake_cell_count = 0;
    // per-cell 引用计数（cell id → 实例数；fake cell 用任务内分配 id）
    CMUnorderedMap<uint32_t, uint64_t> per_cell_counts_;

    FLY_SERIALIZE(instance_count, unplaced_count, fake_cell_count,
                  per_cell_counts_)
};

// per-DEF 产物（⑬：解析阶段每 DEF 单份、大体量独立对象不进 DSDesign）：
// local instance 表 + local net 实例名/网名空间 + 实例面积密度通道 +
// 统计 + fake cell 本地登记。S6 编号区间表的区间长度取本对象的实例/网
// 计数；S5b 网内容解析消费 local net id。
//
// R7 ㊱/㊵②：两套 local 名空间（instance/net）为 **hasher 底座**
// （DSInstanceNameHasher/DSNetNameHasher，双向——instance 侧原单向
// name_to_id 违规已修复）且为**运行时字段不序列化**：随 DSBlockNames
// 伴生对象独立落盘（DSBlockNames_<i>，㊵②），落盘读回后经
// set_instance_names/set_net_names 注入回填（⑰/⑱ 按需加载同构——
// 只读实例/密度数据时不拿 name）。instance/net id 均为 64 位（㊳，
// instance 可达 10⁹ 级）：local id 从 1 起、local 0 = block 自身占位
// （⑧，占位不入 hasher）。
class DSBlockBuildData {
public:
    // block 名（DEF DESIGN 语句；fake cell 命名前缀来源）
    CMString block_name_;
    // local instance 表（⑧：id 从 1 起；id 0 = block 自身占位）
    CMUnorderedMap<uint64_t, DSInstance> instances_;
    // —— local 名空间（R7 hasher 底座；运行时字段不序列化 ㊵②）——
    // instance 名 ↔ local id（双向；local id 从 1 起，local 0 不入表）。
    // CMSharedPtr 共享持有：解析期惰性创建、attach_names 与伴生对象
    // （DSBlockNames）共享同一实例——全链零数据拷贝零 move
    CMSharedPtr<DSInstanceNameHasher> instance_names_;
    // local net 名 ↔ local id（③：仅网名；local id 从 1 起，0 保留
    // 未用——与 instance 编号对称，global id = local id + 起始编号
    // （⑨ flatten 换算））
    CMSharedPtr<DSNetNameHasher> net_names_;
    // 实例面积密度通道（S8 合并消费）
    DSDensityGrid density_;
    // 统计
    DSInstanceStats stats_;
    // fake cell 本地登记（⑲/⑳）：fake 登记名 → 任务内分配 id（同 DEF
    // 内复用）；fake cell 数据随序存放于 fake_cells_（其 id 经
    // fake_name_to_id_ 反查）。fake 登记表为 block 级便捷子集索引、
    // 非完整 hasher（fake cell 属 cell 编号空间，全局查询走
    // DSDesign cell hasher）——保留原样（R7 ㊲ 注释裁定）。
    CMUnorderedMap<CMString, uint32_t> fake_name_to_id_;
    CMVector<DSCell> fake_cells_;
    // per-DEF 计数器（InstanceBuildNode / 网名扫描分配用；㊳ 64 位）
    uint64_t next_instance_id_ = 1;
    uint64_t next_net_id_ = 1;

    CM_PROPERTY(block_name)

    // 名字伴生对象注入口（㊵②：DSBlockNames_<i> 读回后共享注入——
    // CMSharedPtr 拷贝即共享计数，零数据拷贝零 move；hasher 归属与伴生
    // 对象共享、消费点仅 get_id/get_name 只读）。与 block_name_ 不符时
    // 为调用方契约错误（不做运行时校验——flow 侧按 def_paths 序对齐）。
    void set_instance_names(CMSharedPtr<DSInstanceNameHasher> h) {
        instance_names_ = std::move(h);
    }
    void set_net_names(CMSharedPtr<DSNetNameHasher> h) {
        net_names_ = std::move(h);
    }
    // 惰性创建（解析期首写前保证就位；空 = 读回未 attach 场景）
    void ensure_names() {
        if (!instance_names_) {
            instance_names_ = CMMakeShared<DSInstanceNameHasher>();
        }
        if (!net_names_) {
            net_names_ = CMMakeShared<DSNetNameHasher>();
        }
    }

    // R8d 落盘封口（裁定 55：alpha 键 lcp_name_arena 经 build_design_db
    // 传入，解析完成 → DSBlockNames_<i> 落盘前的单一封口点）：64 位组
    // 两 hasher（instance/net）id→name 侧 LCP 后缀压缩封口（后缀 arena
    // + id→rank + checkpoint=64，封口后只读）；lcp_enabled = false 恒
    // 无操作（形态一默认路径零变化）。32 位组（cell/pin/via/layer）
    // 不压缩维持形态一（十万级以内不压缩，裁定 ㊷/55）。幂等。
    void finalize_names_for_save(bool lcp_enabled) {
        if (instance_names_) {
            instance_names_->finalize_for_save(lcp_enabled);
        }
        if (net_names_) {
            net_names_->finalize_for_save(lcp_enabled);
        }
    }

    // local 0 = block 自身占位（⑧；解析开始时调用一次，幂等）：占位
    // instance 携带 block cell id（hasher 未命中传 kInvalidId，仅占号
    // 无引用语义；占位不进 instance hasher——非真实实例）
    void init_placeholder(const CMString& block_name, uint32_t block_cell_id);

    // local instance 收录（返回分配的 local id ㊳）。重名实例非法（DEF
    // 语义保证唯一）；实例名登记进 instance hasher（双向）。
    uint64_t add_instance(DSInstance&& inst, const CMString& name);
    // local net id 分配（重名保留首份，返回既有 id；skipped 计数由
    // 调用方经返回值判别）
    uint64_t register_net(const CMString& name);

    size_t instance_total() const { return instances_.size(); }
    const DSInstance* find_instance(uint64_t id) const;
    // 经 instance hasher 查名（hasher 未注入/未命中返回 nullptr；
    // ㊵② 需先注入 DSBlockNames）
    const DSInstance* find_instance_by_name(const CMString& name) const;

    // 已收录网数（hasher 未就位——读回未 attach——返回 0）
    size_t net_count() const { return net_names_ ? net_names_->size() : 0; }
    // local net id → 网名（未注入 hasher / 越界返回 nullopt；空洞返回
    // 空串——R8b arena 化后 get_name 按值，语义同 R7 空名占位可区分）
    std::optional<CMString> net_name_at(uint64_t local_id) const;

    // 字段表排除两 hasher（㊵②：随 DSBlockNames 伴生对象独立落盘）
    FLY_SERIALIZE(block_name_, instances_, density_, stats_,
                  fake_name_to_id_, fake_cells_, next_instance_id_,
                  next_net_id_)
};

// —— 2.4b S5b 网内容（③ 分批解析产物；⑨ local net id；⑩ via instance）——
// per-DEF 网内容独立对象（⑬ 大体量数据不进 DSDesign）：键 = local net id
//（与 DSBlockBuildData 的 local net namemap 对齐，⑨ 仅依赖 S5a）。

// 连接项（local 拓扑保留，S7 并查集的输入）：instance pin 引用或 block
// 级 port 引用。instance_name_ = "PIN" 表示 port 引用（defi 回调语义，
// ( PIN portName ) 语法），pin_name_ = pin/port 名。
class DSNetConnection {
public:
    CMString instance_name_;
    CMString pin_name_;

    CM_PROPERTY(instance_name)
    CM_PROPERTY(pin_name)

    // block 级 port 引用判别
    bool is_port_ref() const { return instance_name_ == "PIN"; }

    FLY_SERIALIZE(instance_name_, pin_name_)
};

// wire 段：layer + 宽度 + 路径点列。点 = 全局 DBU（int32，路径顶点按
// DEF 出现序）；宽度 = 全局 DBU（special net 显式宽度 / 普通 net 回填
// stack 层缺省宽后的最终值）。
class DSNetWire {
public:
    uint32_t layer_id_ = 0;
    int32_t width_ = 0;
    CMVector<GEOPoint> points_;

    CM_PROPERTY(layer_id)
    CM_PROPERTY(width)

    size_t point_count() const { return points_.size(); }
    const GEOPoint& point_at(uint32_t i) const {
        assert(i < points_.size());
        return points_[i];
    }

    FLY_SERIALIZE(layer_id_, width_, points_)
};

// RECT 项：layer + 矩形（net 级 RECT 语句，全局 DBU）
class DSNetRect {
public:
    uint32_t layer_id_ = 0;
    GEORect rect_;

    CM_PROPERTY(layer_id)
    CM_PROPERTY(rect)

    FLY_SERIALIZE(layer_id_, rect_)
};

// via instance（⑩：过孔的放置实例——专用 id 空间从 1 起、无 name 不入
// namemap，仅需 via cell id + 位置；连层关系经 via cell 定义获得）
class DSViaInstance {
public:
    uint32_t via_cell_id_ = 0;
    GEOPoint pos_;

    CM_PROPERTY(via_cell_id)
    CM_PROPERTY(pos)

    FLY_SERIALIZE(via_cell_id_, pos_)
};

// S5b 网侧统计（节点产出；汇总任务合并输出）
class DSNetStats {
public:
    uint64_t net_count = 0;          // 收录网数（有内容产物，NETS+SPECIALNETS）
    uint64_t connection_count = 0;   // 连接项总数
    uint64_t wire_count = 0;         // wire 段总数
    uint64_t rect_count = 0;         // rect 项总数
    uint64_t via_instance_count = 0; // via instance 总数（VIADATA 展开后）
    // 未定义 via 引用跳过数（权威表 plain + ⑫ 前缀双未命中；DSGN::0008）
    uint64_t skipped_via_count = 0;
    // 未定义层引用丢弃条目数（wire 段 / rect 项条目级丢弃；DSGN::0010）
    uint64_t skipped_layer_ref_count = 0;
    // 网名不在 S5a local namemap 的防御兜底计数
    uint64_t skipped_net_count = 0;

    FLY_SERIALIZE(net_count, connection_count, wire_count, rect_count,
                  via_instance_count, skipped_via_count,
                  skipped_layer_ref_count, skipped_net_count)
};

// per-DEF 网内容产物（③ 分批解析落批追加；⑬ 独立对象）：连接表 + 几何表
// + via instance 表 + 网侧密度逐层分列通道 + 统计。S7 并查集消费连接表、
// S8 合并消费密度通道、S9 flatten 消费几何与 via instance。
// R7 ㊳：via instance id 64 位化（⑩ 专用空间从 1 起）。
class DSNetBuildData {
public:
    // block 名（DESIGN 语句；与 DSBlockBuildData 对齐冗余）
    CMString block_name_;
    // 连接表：local net id → 连接项列表
    CMUnorderedMap<uint64_t, CMVector<DSNetConnection>> connections_;
    // 几何表：local net id → wire 段 / rect 项列表
    CMUnorderedMap<uint64_t, CMVector<DSNetWire>> wires_;
    CMUnorderedMap<uint64_t, CMVector<DSNetRect>> rects_;
    // via instance 表（⑩ local id 从 1 起）+ 网归属（net id → 该网的
    // via instance id 列表）
    CMUnorderedMap<uint64_t, DSViaInstance> via_instances_;
    CMUnorderedMap<uint64_t, CMVector<uint64_t>> net_via_ids_;
    // 网侧密度（金属/通孔逐层分列通道，⑥；格网参数与实例面积通道一致，
    // 由 DIEAREA 配置）
    DSDensityGrid density_;
    // 统计
    DSNetStats stats_;
    // via instance id 计数器（⑩ 独立空间从 1 起；㊳ 64 位）
    uint64_t next_via_instance_id_ = 1;

    CM_PROPERTY(block_name)

    // 构建期接口（责任链节点落批追加）
    void add_connection(uint64_t net_id, DSNetConnection&& conn);
    void add_wire(uint64_t net_id, DSNetWire&& wire);
    void add_rect(uint64_t net_id, DSNetRect&& rect);
    // 收录 via instance 并登记网归属，返回分配的 via instance id
    uint64_t add_via_instance(uint64_t net_id, DSViaInstance&& via);

    // 查询辅助（未命中 nullptr）
    const CMVector<DSNetConnection>* connections_of(uint64_t net_id) const;
    const CMVector<DSNetWire>* wires_of(uint64_t net_id) const;
    const CMVector<DSNetRect>* rects_of(uint64_t net_id) const;
    const CMVector<uint64_t>* via_ids_of(uint64_t net_id) const;
    const DSViaInstance* via_instance_at(uint64_t via_id) const;

    FLY_SERIALIZE(block_name_, connections_, wires_, rects_, via_instances_,
                  net_via_ids_, density_, stats_, next_via_instance_id_)
};

// —— 2.5 via cell（裁定 ⑩/⑫）——

// 独立编号空间（非 cell）；VIARULE 生成式 via 在解析期按参数展开（D13）。
// DEF 来源登记名 = "design_name::原名"（⑫）。
class DSViaCell {
public:
    // tech/cell lef 原名；DEF 来源带前缀
    CMString name_;
    uint32_t bottom_layer_id_ = 0;
    uint32_t top_layer_id_ = 0;
    // 切割层 id（⑥ 通孔密度通道的分层键；UINT32_MAX = 未判定——旧数据
    // 或异常形态兜底，消费方回退 bottom_layer_id_）
    uint32_t cut_layer_id_ = UINT32_MAX;
    // 切割层矩形集（DBU）
    CMVector<GEORect> cut_rects_;
    // 上下层包围矩形
    CMVector<GEORect> bottom_enclosure_;
    CMVector<GEORect> top_enclosure_;

    CM_PROPERTY(name)
    CM_PROPERTY(bottom_layer_id)
    CM_PROPERTY(top_layer_id)
    CM_PROPERTY(cut_layer_id)

    uint32_t add_cut_rect(const GEORect& r) {
        cut_rects_.push_back(r);
        return static_cast<uint32_t>(cut_rects_.size() - 1);
    }
    uint32_t add_bottom_enclosure(const GEORect& r) {
        bottom_enclosure_.push_back(r);
        return static_cast<uint32_t>(bottom_enclosure_.size() - 1);
    }
    uint32_t add_top_enclosure(const GEORect& r) {
        top_enclosure_.push_back(r);
        return static_cast<uint32_t>(top_enclosure_.size() - 1);
    }

    size_t cut_rect_count() const { return cut_rects_.size(); }
    const GEORect& cut_rect_at(uint32_t i) const {
        assert(i < cut_rects_.size());
        return cut_rects_[i];
    }
    size_t bottom_enclosure_count() const { return bottom_enclosure_.size(); }
    const GEORect& bottom_enclosure_at(uint32_t i) const {
        assert(i < bottom_enclosure_.size());
        return bottom_enclosure_[i];
    }
    size_t top_enclosure_count() const { return top_enclosure_.size(); }
    const GEORect& top_enclosure_at(uint32_t i) const {
        assert(i < top_enclosure_.size());
        return top_enclosure_[i];
    }

    FLY_SERIALIZE(name_, bottom_layer_id_, top_layer_id_, cut_layer_id_,
                  cut_rects_, bottom_enclosure_, top_enclosure_)
};

// —— 2.6 VIARULE 展开说明（㉚）——
// VIARULE 不单独保存（㉚：DSViaRule 结构与 collector 已删除）——tech lef
// 的 GENERATE 型规则在解析期按规则默认参数直接展开为中心对齐的模板
// DSViaCell（cut/enclosure 生成，见 ds_lef_adapter）；DEF 引用处带参数的
// 生成式 via 按 DEF 自带参数直接展开（T5 逻辑）。via cell 权威表是唯一
// via 形态存储。

// —— 2.7 独立对象：pin 表数据集 / pin 几何数据集 ——

// pin 表数据集：按全局 pin id 组织（R4，P1 裁定 pin 三字段按 pin 维度），
// 只存 lib 侧表数据；条目仅含该 pin 自己的表（不再整 cell 混装一个
// vector）。表以值语义存储（CMVector<CMLookupTable>）：序列化宏不支持
// shared_ptr 元素，且对象级共享语义已由 CMSharedPtr<DSPinTables> 保证
// （⑰ 注入零拷贝）；lib 侧表本为值存，design 侧沿用。
class DSPinTables {
public:
    // 全局 pin id → 功耗表集（rise_power/fall_power 等）
    CMUnorderedMap<uint32_t, CMVector<CMLookupTable>> internal_power_tables_;
    // 全局 pin id → 时序表集（cell_rise/cell_fall 等）
    CMUnorderedMap<uint32_t, CMVector<CMLookupTable>> timing_tables_;

    // 构建期接口（整体接管表集，避免逐表拷贝）
    void add_internal_power_tables(uint32_t pin_id,
                                   CMVector<CMLookupTable>&& tables);
    void add_timing_tables(uint32_t pin_id, CMVector<CMLookupTable>&& tables);

    // 该 pin 任一类表存在即 true
    bool pin_has_tables(uint32_t pin_id) const;
    // 未命中返回 nullptr（引用读取零拷贝）
    const CMVector<CMLookupTable>* internal_power_tables_of(
        uint32_t pin_id) const;
    const CMVector<CMLookupTable>* timing_tables_of(uint32_t pin_id) const;

    FLY_SERIALIZE(internal_power_tables_, timing_tables_)
};

// pin 几何数据集：全局 pin id → lef 逐层 pin 几何（DSShapeRef 值存；
// R4 键从 cell id 改为全局 pin id——block port 几何同样挂 port 的全局
// pin id）。cell 维度聚合视图由 DSDesign::cell_pin_geometries 便利方法
// 提供（遍历 cell 的 pin id 逐个取聚合）。
class DSPinGeometry {
public:
    CMUnorderedMap<uint32_t, CMVector<DSShapeRef>> pin_geometry_;

    void add_geometry(uint32_t pin_id, DSShapeRef&& ref);
    void add_geometries(uint32_t pin_id, CMVector<DSShapeRef>&& geos);

    bool pin_has_geometry(uint32_t pin_id) const;
    const CMVector<DSShapeRef>* geometry_of(uint32_t pin_id) const;

    FLY_SERIALIZE(pin_geometry_)
};

// —— 2.9 层级树（S6；裁定 ⑧⑨⑮）——

// 层级树节点（block instance；⑮ node 同时存 name 与 id）：parent/children
// + 三类编号区间。编号语义（⑧⑨，深度优先序连续分配）：
//   - instance 区间 [instance_start_, instance_start_ + instance_count_)：
//     count = 该 block 定义的 instance_total（含 local 0 占位槽）；映射
//     local → start + local。local 0 = block 自身占位——root 的 local 0
//     即 global id 0（start = 0）；非 root 的占位槽保留不用，自身 global
//     id 存 self_global_id_（落在父块 instance 区间内——block instance
//     本就是父块中的一个实例）。
//   - net / via instance 区间：count = 该 block 定义的净计数（local id
//     从 1 起），映射 local → start + local − 1；local 0 保留未用。via
//     计数 = S5b 产物的 per-DEF 统计（与 S5a 计数同构入参，树构建时点
//     在 S5b 之后）。
// R7 ㊳：三类区间与 self_global_id 64 位化（instance 空间可达 10⁹ 级）；
// 树节点 id（nodes_ 下标导航空间）保持 uint32。block_cell_id_ = 该
// block cell 的全局 cell id（DSNameMapperT 注入表主口键 ㊻；构建时由
// ds_build_hier_tree 查容器 cell hasher 回填，未命中 kInvalidId）。
class DSHierNode {
public:
    // 节点 id = nodes_ 下标（root = 0，⑧ global id 0 = top block instance）
    uint32_t id_ = 0;
    // 父节点 id；root 自指（0）= 无父哨兵
    uint32_t parent_id_ = 0;
    // 直系 children id 列表（DFS/实例 local id 序）
    CMVector<uint32_t> children_ids_;
    // block cell 名 + 实例化该 block 的实例名（⑮ name 与 id 双存；root
    // 实例名 = block 名自指）
    CMString block_cell_name_;
    CMString instance_name_;
    // block cell 全局 id（R7 ㊻：DSNameMapperT 注入表主口键；未命中 =
    // DSCellNameHasher::kInvalidId）
    uint32_t block_cell_id_ = DSCellNameHasher::kInvalidId;
    // 该 block instance 自身的全局 instance id（⑧ local 0 映射目标；
    // root = 0，非 root = 父块 instance_start_ + 在父块内的 local id；
    // ㊳ 64 位）
    uint64_t self_global_id_ = 0;
    // 三类编号区间（[start, start + count)；㊳ 64 位）
    uint64_t instance_start_ = 0;
    uint64_t instance_count_ = 0;
    uint64_t net_start_ = 0;
    uint64_t net_count_ = 0;
    uint64_t via_start_ = 0;
    uint64_t via_count_ = 0;

    CM_PROPERTY(id)
    CM_PROPERTY(parent_id)
    CM_PROPERTY(block_cell_name)
    CM_PROPERTY(instance_name)
    CM_PROPERTY(block_cell_id)
    CM_PROPERTY(self_global_id)
    CM_PROPERTY(instance_start)
    CM_PROPERTY(instance_count)
    CM_PROPERTY(net_start)
    CM_PROPERTY(net_count)
    CM_PROPERTY(via_start)
    CM_PROPERTY(via_count)

    const CMVector<uint32_t>& get_children_ids() const {
        return children_ids_;
    }
    // 构建期挂接（S6 DFS）
    CMVector<uint32_t>& get_ref_children_ids() { return children_ids_; }

    FLY_SERIALIZE(id_, parent_id_, children_ids_, block_cell_name_,
                  instance_name_, block_cell_id_, self_global_id_,
                  instance_start_, instance_count_, net_start_, net_count_,
                  via_start_, via_count_)
};

// 层级树（⑮ 编号区间表是树的组成部分，不独立成对象）：nodes_ 深度优先
// 前序 = 下标序（root = 0），三类区间的 start 序随下标单调不减，区间
// 反查经二分。四接口：区间反查（block_of_*）/ 范围查（*_range）/
// parent 与 children / format_tree（以 name 打印缩进层级文本）。
class DSHierTree {
public:
    static constexpr uint32_t kNoNode = UINT32_MAX;  // 查询未命中（节点 id）

    CMVector<DSHierNode> nodes_;
    // top block 名（根的 block cell 名）
    CMString design_name_;

    CM_PROPERTY(design_name)

    size_t node_count() const { return nodes_.size(); }
    // 越界为调用方契约错误（debug 断言）
    const DSHierNode& node(uint32_t id) const {
        assert(id < nodes_.size());
        return nodes_[id];
    }

    // —— 四接口 ——
    // ① 区间反查：global id（㊳ 64 位）→ 所属 block instance 节点 id
    //（未命中 kNoNode）
    uint32_t block_of_instance(uint64_t global_id) const;
    uint32_t block_of_net(uint64_t global_id) const;
    uint32_t block_of_via_instance(uint64_t global_id) const;
    // ② 范围查：返回 (start, count)；越界节点返回 (0, 0)
    std::pair<uint64_t, uint64_t> instance_range(uint32_t node_id) const;
    std::pair<uint64_t, uint64_t> net_range(uint32_t node_id) const;
    std::pair<uint64_t, uint64_t> via_range(uint32_t node_id) const;
    // ③ 父与直系 children（root 的 parent = 自身 0）
    uint32_t parent(uint32_t node_id) const {
        assert(node_id < nodes_.size());
        return nodes_[node_id].get_parent_id();
    }
    const CMVector<uint32_t>& children(uint32_t node_id) const {
        assert(node_id < nodes_.size());
        return nodes_[node_id].get_children_ids();
    }
    // ④ 以 name 打印缩进层级文本（每行含三类区间）
    CMString format_tree() const;

    // 直系 children 中按实例名查找子节点（R7：DSNameMapperT 树下降的
    // 辅助接口；未命中 kNoNode，同实例名的多个 child 不可达——实例名
    // 在 block 内唯一，此处仅首个命中防御）
    uint32_t find_child_by_instance_name(uint32_t node_id,
                                         const CMString& name) const;

    // —— global id 换算 API（⑨ local id + 起始编号，S9 flatten 输入口；
    // 越界/未用 local 返回 kNoNode；global id ㊳ 64 位）——
    uint64_t global_instance_id(uint32_t node_id, uint64_t local_id) const;
    uint64_t global_net_id(uint32_t node_id, uint64_t local_id) const;
    uint64_t global_via_instance_id(uint32_t node_id, uint64_t local_id) const;

    FLY_SERIALIZE(nodes_, design_name_)
};

// —— 2.8 DSDesign 容器（裁定 ⑬）——

// design db 顶层容器：cell id = cells_ 下标、via cell id = via_cells_
// 下标（各 id 空间独立 uint32；block cell 与 macro 同一 cell 编号空间，
// ㉙——独立 blocks_ 表与 block namemap 已删除，block 查找走 cell
// namemap + is_block_cell()）；pin id 全局平铺单调分配（D1），pin
// namemap 键 = "cell_name/pin_name"。
class DSDesign {
public:
    // —— 序列化字段 ——
    // 正常 cell（lef macro + block cell 同空间，⑯/㉙）
    CMVector<DSCell> cells_;
    // fake cell 单独字段（⑳：指向 cells_ 内 is_fake_cell() 条目的 id 集；
    // P4：flags 为权威语义，本集合为遍历加速索引）
    CMVector<uint32_t> fake_cell_ids_;
    // via cell 权威表（⑫：tech lef + cell lef + 各 DEF 全集）
    CMVector<DSViaCell> via_cells_;
    // cell id → lib cell 名（lib 关联；lib 独有 cell 不入 id 空间）
    CMUnorderedMap<uint32_t, CMString> lib_link_;

    // name ↔ id 双向 hasher（R7 ㊲/㊸：原三套散装 map+vector 收编进
    // hasher 底座，双向语义不变；pin 键 = "cell_name/pin_name"（D1 组合
    // 键）。32 位组（id 十万级以内，㊳）。
    DSCellNameHasher cell_names_;
    DSPinNameHasher pin_names_;
    DSViaCellNameHasher via_cell_names_;
    // 层级树（S6；⑮ 编号区间表为树的组成部分，随容器序列化持久化）
    DSHierTree hier_tree_;

    // —— 运行时专用字段（⑰/⑱，不序列化）——
    CMSharedPtr<DSPinTables> pin_tables_;
    CMSharedPtr<DSPinGeometry> pin_geometry_;

    // find_* 未命中的 id（R7 ㊴ 收编：值不变 = 类型最大值，与
    // DSCellNameHasher::kInvalidId 等值同口径；is_valid_id 判定见
    // DSCellNameHasher）
    static constexpr uint32_t kInvalidId = DSCellNameHasher::kInvalidId;

    // 构建期接口：追加并注册 hasher，返回 id（重名由调用方保证唯一——
    // 重复 macro 的保留首份 DSGN 语义在适配层处理；hasher emplace 重名
    // 保留首份兜底）
    uint32_t add_cell(DSCell&& cell);
    // S5a 汇总专用：按预分配 id 直接落位（fake cell id 保持任务内分配
    // 值，⑳）——稀疏 resize 占位（id = 下标语义不变，空洞为空名占位），
    // hasher 同步注册。仅汇总任务串行调用。
    void add_cell_at(uint32_t cell_id, DSCell&& cell);
    uint32_t add_via_cell(DSViaCell&& via);
    // pin hasher 注册（pin id 由调用方按 D1 平铺分配；键 =
    // "cell_name/pin_name"）
    void register_pin(const CMString& cell_name, const CMString& pin_name,
                      uint32_t pin_id);

    // 查询辅助（未命中 nullptr）
    const DSCell* find_cell(const CMString& name) const;
    const DSViaCell* find_via_cell(const CMString& name) const;

    // pin 名查询（R7 ㊱：DSPin 自身不存 name，经 pin hasher 组合键
    // "cell_name/pin_name" 反查取 pin 名段；未登记 id 返回空串）
    CMString pin_name_of(uint32_t pin_id) const;

    // S3 merge（裁定 ⑯）：与 lib 库容器按 cell name 对齐——匹配 cell 填
    // lib 字段（library_name_）+ lib_link_ 注册 + pin 集合比对（缺失
    // 名单 DSGN::0004 提醒）；lef 有 lib 无 → DSGN::0002、lib 有 lef 无
    // → DSGN::0003（名单汇总提醒，不拦截不 crash）；匹配 cell 的 lib
    // 功耗/时序表逐 pin 提取为 DSPinTables（⑰ 独立对象，R4 按全局
    // pin id 落位）挂容器专用字段。返回匹配 cell 数。
    int merge_lib(const LIBLibrary& lib);

    // cell 维度便利聚合（R4）：cell 全部 pin（按 pins_ 序）的几何拼接
    // （pin 几何容器按全局 pin id 组织，逐 pin 取出拼合；cell 无几何
    // 数据或无 pin 时返回空集）。
    CMVector<DSShapeRef> cell_pin_geometries(uint32_t cell_id) const;

    // 统一入口（⑰）：把容器专用字段按指针注入 cell 的两个不序列化字段
    // （共享非拷贝）后返回引用。越界为调用方契约错误（debug 断言）。
    DSCell& get_cell(uint32_t cell_id);
    const DSCell& get_cell(uint32_t cell_id) const;

    // 层级树（S6；⑬ 全局轻量数据收纳进容器——含编号区间表，随容器序列
    // 化持久化）。S6 在正式 DSDesign 写定前完成树构建（容器唯一写定原
    // 则，同 S5a 先例）。
    const DSHierTree& get_hier_tree() const { return hier_tree_; }
    void set_hier_tree(const DSHierTree& tree) { hier_tree_ = tree; }
    void set_hier_tree(DSHierTree&& tree) { hier_tree_ = std::move(tree); }

    CM_PROPERTY(pin_tables)
    CM_PROPERTY(pin_geometry)

    // 字段表显式排除 pin_tables_/pin_geometry_（⑱）
    FLY_SERIALIZE(cells_, fake_cell_ids_, via_cells_, lib_link_, cell_names_,
                  pin_names_, via_cell_names_, hier_tree_)
};

}  // namespace fly
