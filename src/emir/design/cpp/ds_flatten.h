#pragma once

// =============================================================================
// S9 flatten 展平 + 分区保存（方案 design-db-plan.md §3.2 S9 + 2026-09-13
// 裁定补记①-⑤ 定稿）。
//
// 数据结构（分区产物四类 + 分片中间形态，裁定补记②）：
//   DSPartConnection        连接项条目（INST_CONNECTIONS / NET_CONNECTIONS
//                           共用形态）：端点实例 global id + 端点网 global
//                           id + pin/port 名（S5b 原名保留）+ port 位
//                          （("PIN", port) 引用——端点实例 = 所属块实例自
//                           身 global id，⑧ local 0 映射）。
//   DSGeomEntry             几何条目（统一形态）：layer id + 全局矩形 +
//                           via 专属字段（via cell id，kNoViaCell = 非
//                           via）+ obs 位（DEF obstruction）+ primary 位
//                           （仅 via 条目有意义——放置点归属，补记①）。
//   DSPartitionGeometry     /GEOMETRY：net global id → 条目集（net 0 =
//                           OBS 桶，与 root 块首网 global 0 条目共存、
//                           obs 位判别）+ 跨分区网集合（is_crossing，
//                           补记④：成员图形散布 > 1 分区，S10 统计口径）。
//   DSPartInstances         /INSTANCES：instance global id → DSInstance
//                           副本（全局 transform + primary 位 + 电源引脚
//                           预展开坐标 D18）。
//   DSPartInstConnections   /INST_CONNECTIONS：instance global id → 连接
//                           项列表（跟随 instance 副本）。
//   DSPartNetConnections    /NET_CONNECTIONS：net global id → 连接项列表
//                           （跟随 net 副本；非 pg 网全量补全——跨分区连
//                           接也保存、本分区自足；pg 网不补全——仅本分区
//                           instance 副本相关条目，靠 union + instance
//                           维度拼装，补记②）。
//   DSPartitionProduct      四类聚合容器 = 分片（slice）中间形态 + 合并
//                           工作形态；merge_from = 追加合并（幂等键覆盖
//                           不叠加——instance 副本同 global id 只此一份）。
//
// 展开算法（每 block 定义一调用，方案：每份 DEF 数据只读一次）：
//   ds_flatten_block  收集该定义在层级树上的全部出现位置（每位置 = 一个
//                     block instance，携自根复合变换与三类起始编号段），
//                     复合变换前序递推（同 S8 线性等价形式），逐位置展开：
//                     instance/via 归属 = 放置点（pos）判定——core_rect
//                     半开区间包含 → 该分区 primary；不在任何 core 但在
//                     extend_rect 内 → 副本 primary=false（补记①；无 core
//                     命中的防御回退 = 距最近 core 分区 primary，恰一不
//                     变式恒成立）；net 几何（wire 段矩形化 + rect + via
//                     cell 图形展开）与各分区 extend_rect 交叠即放副本
//                     （无 primary 概念、跨多区多副本，坐标分量判定——
//                     禁 width()/height()，补记④/⑦）；DEF obstruction →
//                     net 0 + obs 位（补记③）；cell 级 pin/macro OBS 几何
//                     绝不入产物（复现原则）；电源引脚预展开（D18）。
//
// 编排（ds_flow.py 两级任务）：展开任务按 block 定义切分（小 DEF 按
// alpha def_aggregate_threshold 聚合），每任务产出所涉各分区的分片；每
// 分区一合并任务 merge 全部相关分片 → 四类正式对象。net id 保持
// local + offset 形式不换算 root（S7 裁定④）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>

#include <cstdint>
#include <utility>

namespace fly {

// 分区连接项（INST_CONNECTIONS / NET_CONNECTIONS 共用条目形态；S5b 名字
// 形态的 global 化——instance 名经伴生 hasher → local id → +inst_start；
// ("PIN", port) 引用 → 端点实例 = 所属块实例自身 global id + port 位）。
class DSPartConnection {
public:
    // 端点实例 global id（⑧ local 0 映射目标；port 位条目 = 块实例自身）
    uint64_t instance_global_id_ = 0;
    // 端点网 global id（local + offset，不换算 root）
    uint64_t net_global_id_ = 0;
    // pin / port 名（S5b 连接表原名保留；下游经 cell 数据解 pin id）
    CMString pin_name_;
    // port 位 = ("PIN", port) 块级引用（instance_name == "PIN" 语义移植）
    CM_FLAGS(uint8_t, port)

    FLY_SERIALIZE(instance_global_id_, net_global_id_, pin_name_, flags_)
};

// 分区几何条目（net wire/rect 图形 + via instance 展开图形 + DEF
// obstruction 统一形态；「geometry 以 net 组织」总形态的最小单元）。
class DSGeomEntry {
public:
    // 非 via 条目的 via_cell_id_ 哨兵（与 DSViaCell::cut_layer_id_ 未判
    // 定哨兵同值口径）
    static constexpr uint32_t kNoViaCell = UINT32_MAX;

    // 层 id（via 条目：cut → cut_layer_id_、enclosure → bottom/top 层）
    uint32_t layer_id_ = 0;
    // 全局坐标矩形（DBU；wire 段矩形化 = 相邻点对 + 宽度半开区间外扩，
    // 同 S5b 密度节点口径；不做分区裁剪）
    GEORect rect_;
    // via 专属：via cell 权威表 id（kNoViaCell = 非 via 条目；下游连层
    // /电阻分流判定用）
    uint32_t via_cell_id_ = kNoViaCell;
    // obs 位 = DEF obstruction（net 0 桶）；primary 位 = via 放置点归属
    //（仅 via 条目置位——core 内放置点的分区条目为真，补记①）
    CM_FLAGS(uint8_t, obs, primary)

    bool is_via() const { return via_cell_id_ != kNoViaCell; }

    FLY_SERIALIZE(layer_id_, rect_, via_cell_id_, flags_)
};

// /GEOMETRY：net global id → 几何条目集 + 跨分区网集合。
class DSPartitionGeometry {
public:
    // net 0 = OBS 桶（与 root 块首网 global id 0 的条目共存，obs 位判别
    // ——root 块 net_start_ = 0、local 1 → global 0，为合法网 id）
    CMUnorderedMap<uint64_t, CMVector<DSGeomEntry>> nets_;
    // 跨分区网（is_crossing，补记④）：成员图形散布多于一个分区的
    // global net id 集（本分区有副本的网才登记；S10 统计口径）。键 = net
    // global id、值恒 1（序列化框架无 set 容器支持，map 充当 set）
    CMUnorderedMap<uint64_t, uint8_t> crossing_nets_;

    // 构建期接口（条目追加；跨分区判定由展开任务完成）
    void add_entry(uint64_t net_global_id, DSGeomEntry&& entry) {
        nets_[net_global_id].push_back(std::move(entry));
    }
    void mark_crossing(uint64_t net_global_id) {
        crossing_nets_.emplace(net_global_id, 1);
    }
    bool is_crossing(uint64_t net_global_id) const {
        return crossing_nets_.contains(net_global_id);
    }
    // 未命中 nullptr（引用读取零拷贝）
    const CMVector<DSGeomEntry>* entries_of(uint64_t net_global_id) const {
        auto it = nets_.find(net_global_id);
        return it == nets_.end() ? nullptr : &it->second;
    }

    // —— 同键共存防误用便捷接口（review 2026-09-13 建议：net 0 桶混装
    //    root 首网几何与 OBS 条目，按 net id 直取全桶会把 OBS 误当网几何
    //    ——下游消费一律用以下两个过滤视图，勿裸用 entries_of）——
    // 某网的真实几何条目（过滤 OBS；含 net 0 的 root 首网条目）
    CMVector<DSGeomEntry> net_entries(uint64_t net_global_id) const {
        CMVector<DSGeomEntry> out;
        const auto* all = entries_of(net_global_id);
        if (all != nullptr) {
            for (const DSGeomEntry& e : *all) {
                if (!e.is_obs()) out.push_back(e);
            }
        }
        return out;
    }
    // 全部 OBS 条目（DEF obstruction，设计级无所属网）
    CMVector<DSGeomEntry> obs_entries() const {
        CMVector<DSGeomEntry> out;
        const auto* all = entries_of(0);
        if (all != nullptr) {
            for (const DSGeomEntry& e : *all) {
                if (e.is_obs()) out.push_back(e);
            }
        }
        return out;
    }

    FLY_SERIALIZE(nets_, crossing_nets_)
};

// /INSTANCES：instance global id → DSInstance 副本。
class DSPartInstances {
public:
    // global id（local + inst_start，local 0 = 块实例自身——由父块展开
    // 产出）→ 副本（全局 transform + primary 位 + 电源引脚预展开坐标）
    CMUnorderedMap<uint64_t, DSInstance> items_;

    size_t size() const { return items_.size(); }

    FLY_SERIALIZE(items_)
};

// /INST_CONNECTIONS：instance global id → 连接项列表（跟随 instance 副本
// ——每个持有该实例副本的分区一份；条目携带端点网 global id）。
// **边界语义（review 2026-09-13 标注）**：键 0 = root 块自身的设计级
// port 连接（root self_global_id_ = 0，⑧ local 0 映射）——INSTANCES
// 永无 global id 0 条目（root 无实体副本），下游按键取实例前须排除 0
// 或用带默认值的查询，勿以 items_.at(0) 取实例。
class DSPartInstConnections {
public:
    CMUnorderedMap<uint64_t, CMVector<DSPartConnection>> items_;

    size_t size() const { return items_.size(); }

    FLY_SERIALIZE(items_)
};

// /NET_CONNECTIONS：net global id → 连接项列表（跟随 net 副本——仅几何
// 副本所在分区；非 pg 全量补全 / pg 仅本区 instance 副本相关条目）。
class DSPartNetConnections {
public:
    CMUnorderedMap<uint64_t, CMVector<DSPartConnection>> items_;

    size_t size() const { return items_.size(); }

    FLY_SERIALIZE(items_)
};

// 四类聚合容器：展开任务产出的分片（slice）中间形态 + 分区合并任务的
// 工作形态。merge_from = 追加合并（同 global id 的 instance 副本键覆盖
// ——global id 全局唯一，同键仅同源重放；连接项列表拼接；几何条目拼接；
// crossing 集并）。
class DSPartitionProduct {
public:
    DSPartInstances instances_;
    DSPartInstConnections inst_connections_;
    DSPartNetConnections net_connections_;
    DSPartitionGeometry geometry_;

    void merge_from(const DSPartitionProduct& src);

    FLY_SERIALIZE(instances_, inst_connections_, net_connections_, geometry_)
};

// S9 展开算法（每 block 定义一调用；方案「每份 DEF 数据只读一次」）：
// 该 def 的实例/网产物 + 伴生名（连接项 instance 名 → local id）+ 容器
//（cell 电源 pin 判定 / via cell 权威表几何）+ pin 几何对象（电源 pin
// 局部几何，可为 nullptr——无几何时不做电源引脚预展开）+ 分区表 → 所涉
// 各分区的分片列表（仅产出非空分区）。tree 与产物无对齐校验（树构建期
// 已 fatal 对齐错误）；def 未被实例化（树上无位置）→ 空结果放行
//（dev-rules §7）。
CMVector<std::pair<uint32_t, DSPartitionProduct>> ds_flatten_block(
    const DSHierTree& tree, const DSBlockBuildData& block,
    const DSNetBuildData& nets, const DSBlockNames& names,
    const DSDesign& design, const DSPinGeometry* pin_geoms,
    const CMVector<DSSubPartition>& partitions);

}  // namespace fly
