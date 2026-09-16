#pragma once

// =============================================================================
// S9 flatten 展平 + 分区保存（方案 design-db-plan.md §3.2 S9 + 2026-09-13
// 裁定补记①-⑤ 定稿 + 同日 partition 网数据结构重组终态 + 2026-09-14
// 分区产物 pg/信号拆分裁定 + 同日 net id 0 专属 OBS 裁定）。
//
// 数据结构（分区产物六类 + 分片中间形态）：
//   DSNetConnEntry          NETS 侧连接条目 = (端点实例 global id, 端点
//                           pin 全局平铺 id, flags 六位)——沿用原
//                           DSPartConnection 的 NET 维度语义（网 id 由所
//                           在 DSNet 的键承载，条目不再冗余），2026-09-13
//                           重组裁定更名。
//   DSPartConnection        INST_CONNECTIONS 条目（instance 维度）：与
//                           DSNetConnEntry 同款字段（inst_id/pin_id/
//                           flags）+ 端点网 global id（instance 维度组
//                           织必需——条目散挂各实例副本，网 id 无法由键
//                           承载）。port 位（块级 port 引用——端点实例 =
//                           所属块实例自身 global id，⑧ local 0 映射）。
//   DSGeomEntry             几何条目（统一形态）：layer id + 全局矩形 +
//                           via 专属字段（via cell id，kNoViaCell = 非
//                           via）+ obs 位（DEF obstruction）+ primary 位
//                           （仅 via 条目有意义——放置点归属，补记①）。
//   DSPartitionGeometry     /GEOMETRY 与 /GEOMETRY_PG（2026-09-14 拆分
//                           裁定：信号网几何与 pg 网几何分两个对象保存，
//                           类定义同一、两侧各一实例）：net global id →
//                           条目集 + 跨分区网集合（is_crossing，补记④：
//                           成员图形散布 > 1 分区，S10 统计口径——两侧
//                           各带自己网的 crossing 集，合计口径不变）。
//                           信号侧 GEOMETRY 键 0 = OBS 桶专属（2026-09-14
//                           裁定：OBS 是设计级阻挡非网数据，归信号侧——
//                           pg 侧保持纯净只含 pg 网条目；net 区间空洞位
//                           形态下键 0 不再与 root 首网混叠，obs 位判别
//                           保留为防御校验）。
//   DSPartInstances         /INSTANCES：instance global id → DSInstance
//                           副本（全局 transform + primary 位）。
//   DSPartInstConnections   /INST_CONNECTIONS：instance global id → 连接
//                           项列表（跟随 instance 副本）。
//   DSNet                   单网聚合 = net id + use（DSNetUse，缺省
//                           SIGNAL）+ 连接条目集（几何不进本结构——仍在
//                           分区 GEOMETRY/GEOMETRY_PG 对象按 net 组织）。
//   DSPartitionNets         /NETS 与 /NETS_PG（2026-09-14 拆分裁定：原
//                           一个对象内信号网/pg 网两表拆为两个独立对象，
//                           类单表化——各持 part_id_ + 本侧表一份）。
//                           信号网（use 非 POWER/GROUND）= 全量补全连接
//                           副本（跨分区连接也保存、本分区自足）；pg 网
//                           = 不补全（仅本分区 instance 副本相关条目，
//                           靠 union + instance 维度拼装，补记②）。use
//                           随 DSNet 写入（自 S5b net_uses_，USE 全量
//                           补收裁定），get_net 类 debug 消费经 is_pg
//                           路由到对应侧对象单表查。
//   DSPgNetSlice            pg 网全局集分区片段（临时对象）：本区 NETS_PG
//                           对象表键按 use 分流的两 id 列表。
//   DSPgNetSet              全局 pg 网 id 集（"pg_nets" 正式对象）：
//                           power/ground 两 unordered_set（O(1) 判定，
//                           2026-09-13 用户定稿结构；运行时结构与序列化
//                           格式解耦已消解——序列化宏已接 set 全族直存）。
//   DSPartitionProduct      六类聚合容器 = 分片（slice）中间形态 + 合并
//                           工作形态（instances/inst_connections + nets/
//                           nets_pg + geometry/geometry_pg 四对成员）；
//                           merge_from = 追加合并（幂等键覆盖不叠加——
//                           instance 副本同 global id 只此一份）。
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
//                     net 0 + obs 位（补记③；恒入信号侧 GEOMETRY）；
//                     cell 级 pin/macro OBS 几何绝不入产物（复现原则）。
//                     （2026-09-13 裁定：电源引脚预展开（D18）删除——电源
//                     引脚坐标归 ④ 提取按复现原则自 instance + transform
//                     + cell pin 几何自取。）
//
// 编排（ds_flow.py 两级任务）：展开任务按 block 定义切分（小 DEF 按
// alpha def_aggregate_threshold 聚合），每任务产出所涉各分区的分片；每
// 分区一合并任务 merge 全部相关分片 → 六类正式对象（连接与几何写入按
// S5b is_pg_net 分流：pg → NETS_PG / GEOMETRY_PG、信号 → NETS /
// GEOMETRY）。net id 保持 local + offset 形式不换算 root（S7 裁定④；
// global = net_start + local，区间含空洞位——2026-09-14 裁定）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>

#include <cstdint>
#include <utility>

namespace fly {

// NETS 侧连接条目（2026-09-13 重组裁定：原 DSPartConnection 的 NET 维度
// 语义更名——端点网 global id 由所在 DSNet 的键承载，条目三字段）。S5b
// 连接表 id 形态的 global 化——instance local id → +inst_start（⑧ local
// 0 = 块实例自身）；pin 全局平铺 id 直存（2026-09-13 裁定（D1 全局平铺
// pin id）：分区副本 pin 不留名——pin id 全局唯一，名字反查经容器 pin
// hasher）。flags 六位（port/driver/receiver/power/ground/clock）自 S5b
// 条目直存，hybrid = driver+receiver 同置（第三分类命名，统计口径互斥
// 单列——见 DSNetConnection 注释）。
class DSNetConnEntry {
public:
    // 端点实例 global id（⑧ local 0 映射目标；port 位条目 = 块实例自身）
    CMInstanceId inst_id_ = CMInstanceId{0};
    // 端点 pin 全局平铺 id（S5b 解析边界换算完成，直存）
    CMPinId pin_id_;
    // port / driver / receiver / power / ground / clock 位（S5b 直存；
    // hybrid = driver+receiver 同置）
    CM_FLAGS(uint8_t, port, driver, receiver, power, ground, clock)

    FLY_SERIALIZE(inst_id_, pin_id_, flags_)
};

// INST_CONNECTIONS 侧连接条目（instance 维度）：与 DSNetConnEntry 同款
// 字段（inst_id/pin_id/flags 六位，2026-09-13 重组裁定对齐更名）+ 端点
// 网 global id——instance 维度组织必需（条目散挂各实例副本，网 id 无法
// 由键承载；NET 维度条目 DSNetConnEntry 则由所在 DSNet 键承载）。
class DSPartConnection {
public:
    // 端点实例 global id（⑧ local 0 映射目标；port 位条目 = 块实例自身）
    CMInstanceId inst_id_ = CMInstanceId{0};
    // 端点网 global id（local + offset，不换算 root）
    CMNetId net_global_id_;
    // 端点 pin 全局平铺 id（S5b 解析边界换算完成，直存）
    CMPinId pin_id_;
    // port / driver / receiver / power / ground / clock 位（S5b 直存；
    // hybrid = driver+receiver 同置）
    CM_FLAGS(uint8_t, port, driver, receiver, power, ground, clock)

    FLY_SERIALIZE(inst_id_, net_global_id_, pin_id_, flags_)
};

// 分区几何条目（net wire/rect 图形 + via instance 展开图形 + DEF
// obstruction 统一形态；「geometry 以 net 组织」总形态的最小单元）。
class DSGeomEntry {
public:
    // 非 via 条目的 via_cell_id_ 哨兵 = 强类型默认（整型最大值，与
    // DSViaCell::cut_layer_id_ 未判定哨兵同值口径）
    static constexpr CMViaCellId kNoViaCell{};

    // 层 id（via 条目：cut → cut_layer_id_、enclosure → bottom/top 层）
    CMLayerId layer_id_;
    // 全局坐标矩形（DBU；wire 段矩形化 = 相邻点对 + 宽度半开区间外扩，
    // 同 S5b 密度节点口径；不做分区裁剪）
    GEORect rect_;
    // via 专属：via cell 权威表 id（哨兵 = 非 via 条目；下游连层
    // /电阻分流判定用）
    CMViaCellId via_cell_id_;
    // obs 位 = DEF obstruction（net 0 桶）；primary 位 = via 放置点归属
    //（仅 via 条目置位——core 内放置点的分区条目为真，补记①）
    CM_FLAGS(uint8_t, obs, primary)

    bool is_via() const { return via_cell_id_.is_valid(); }

    FLY_SERIALIZE(layer_id_, rect_, via_cell_id_, flags_)
};

// /GEOMETRY 与 /GEOMETRY_PG（2026-09-14 拆分裁定：同一类两侧各一实例
// ——信号侧 GEOMETRY / pg 侧 GEOMETRY_PG；④ 提取首期专注电源网络时只
// 加载 GEOMETRY_PG + NETS_PG 两个小对象）：net global id → 几何条目集 +
// 跨分区网集合。信号侧键 0 = OBS 桶专属（2026-09-14 裁定：net 区间空洞
// 位形态下 root 首网 global 1 起，键 0 不再与真网混叠；OBS 是设计级阻挡
// 非网数据、归信号侧——pg 侧保持纯净只含 pg 网条目）；obs 位判别保留为
// 防御校验（net_entries(0) 恒空、obs_entries() = 键 0 全桶）。
class DSPartitionGeometry {
public:
    // 信号侧键 0 = OBS 桶（obs 位判别——2026-09-14 起恒纯 OBS，与真网
    // 无同键共存；pg 侧无此键）
    CMUnorderedMap<CMNetId, CMVector<DSGeomEntry>> nets_;
    // 跨分区网（is_crossing，补记④）：成员图形散布多于一个分区的
    // global net id 集（本分区有副本的网才登记；S10 统计口径）。
    // （2026-09-13 修正：原「CMUnorderedMap<uint64_t, uint8_t> 值恒 1 充
    // 当 set」系序列化宏无 set 支持时期的妥协——宏已接 set 全族，回归
    // CMUnorderedSet 直存）
    CMUnorderedSet<CMNetId> crossing_nets_;

    // 构建期接口（条目追加；跨分区判定由展开任务完成）
    void add_entry(CMNetId net_global_id, DSGeomEntry&& entry) {
        nets_[net_global_id].push_back(std::move(entry));
    }
    void mark_crossing(CMNetId net_global_id) {
        crossing_nets_.insert(net_global_id);
    }
    bool is_crossing(CMNetId net_global_id) const {
        return crossing_nets_.contains(net_global_id);
    }
    // 未命中 nullptr（引用读取零拷贝）
    const CMVector<DSGeomEntry>* entries_of(CMNetId net_global_id) const {
        auto it = nets_.find(net_global_id);
        return it == nets_.end() ? nullptr : &it->second;
    }

    // —— OBS 桶过滤便捷接口（键 0 专属 OBS 后混叠风险已消，保留为防御
    //    校验与 OBS 单独消费口——勿以 net_entries(0) 取「网几何」）——
    // 某网的真实几何条目（过滤 OBS；键 0 恒空——0 为 OBS 专属位）
    CMVector<DSGeomEntry> net_entries(CMNetId net_global_id) const {
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
        const auto* all = entries_of(CMNetId{0});
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
    CMUnorderedMap<CMInstanceId, DSInstance> items_;

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
    CMUnorderedMap<CMInstanceId, CMVector<DSPartConnection>> items_;

    size_t size() const { return items_.size(); }

    FLY_SERIALIZE(items_)
};

// 单网聚合（2026-09-13 重组裁定）：id + use + 连接条目集。几何不进本
// 结构——仍在分区 GEOMETRY 对象按 net 组织。use = DSNetUse 枚举定型存储
// （2026-09-16 裁定：领域枚举禁止裸整型；S5b net_uses_ 全量补收裁定随
// 网写入；缺省 SIGNAL——S5b 只记非 SIGNAL 条目，缺省读取口径一致）。
class DSNet {
public:
    CMNetId net_id_;
    DSNetUse use_ = DSNetUse::SIGNAL;
    CMVector<DSNetConnEntry> connections_;

    // use 读取
    DSNetUse use() const { return use_; }

    FLY_SERIALIZE(net_id_, use_, connections_)
};

// /NETS 与 /NETS_PG（2026-09-14 拆分裁定：原 DSPartitionNets 信号网/pg 网
// 两表单对象拆为两个独立正式对象，类单表化——删 pg_nets_ 成员，两侧复用
// 同一类、各持 part_id_ + 本侧表；④ 提取首期与 GEOMETRY_PG 配对只加载
// pg 侧两个小对象）。信号网对象 /NETS（use 非 POWER/GROUND）= 全量补全
// 连接副本（跨分区连接也保存、本分区自足）；pg 网对象 /NETS_PG = 不补全
// （仅本分区 instance 副本相关条目，靠 union + instance 维度拼装，补记
// ②）。pg 判定源 = S5b is_pg_net（special ∨ use ∈ {POWER, GROUND}）。
// debug 消费 get_net 经全局 pg 网 id 集 is_pg 路由到对应侧对象单表查。
class DSPartitionNets {
public:
    // 本分区 id（ds_flatten_block 分片产出时回填；merge 幂等——同分区
    // 分片同 id。NETS / NETS_PG 两侧同值）
    CMPartitionId part_id_;
    // 本侧网表（键 = net global id；信号侧全量补全 / pg 侧不补全）
    CMUnorderedMap<CMNetId, DSNet> nets_;

    size_t size() const { return nets_.size(); }
    // 单表查（未命中 nullptr；引用读取零拷贝——加载侧按 is_pg 路由到
    // 对应侧对象，本类不再承担两表分派）
    const DSNet* net_of(CMNetId net_id) const;
    DSNet* net_of(CMNetId net_id);

    FLY_SERIALIZE(part_id_, nets_)
};

// pg 网全局集分区片段（S9 每分区合并任务写本区片段的临时对象；汇总合
// 并后清理）：本区 NETS_PG 对象表键按 DSNet.use_ 分流的两 id 列表（use
// 非 POWER/GROUND 的 special 网不入 pg 全局集——is_pg 判定口径 = use 枚
// 举）。
class DSPgNetSlice {
public:
    CMVector<CMNetId> power_ids_;
    CMVector<CMNetId> ground_ids_;

    FLY_SERIALIZE(power_ids_, ground_ids_)
};

// 全局 pg 网 id 集（2026-09-13 用户定稿结构；"pg_nets" 正式对象）：
// power/ground 分开两 unordered_set（O(1) 判定——消费 = debug API get_net
// 的 is_pg 快速路径 + Python is_power/is_ground/is_pg 查询口）。产出 =
// flatten 后汇总任务读全部分区片段 finalize（同 pg 网跨分区副本 set 天
// 然去重）。
class DSPgNetSet {
public:
    // 运行时判定结构（O(1)）
    CMUnorderedSet<CMNetId> power_;
    CMUnorderedSet<CMNetId> ground_;

    // 全局汇总（独立轻任务）：全部分区片段键集按 use 分流合并（set 去
    // 重——同 pg 网跨分区副本只此一条）
    void finalize_from_flatten(const CMVector<const DSPgNetSlice*>& slices);

    bool is_power(CMNetId net_id) const { return power_.contains(net_id); }
    bool is_ground(CMNetId net_id) const { return ground_.contains(net_id); }
    bool is_pg(CMNetId net_id) const {
        return power_.contains(net_id) || ground_.contains(net_id);
    }
    // 规模观测（S10 统计/日志）
    size_t power_count() const { return power_.size(); }
    size_t ground_count() const { return ground_.size(); }

    FLY_SERIALIZE(power_, ground_)
};

// 六类聚合容器：展开任务产出的分片（slice）中间形态 + 分区合并任务的
// 工作形态（2026-09-14 拆分裁定：nets_ / nets_pg_ / geometry_ /
// geometry_pg_ 四成员分侧承载）。merge_from = 追加合并（同 global id 的
// instance 副本键覆盖——global id 全局唯一，同键仅同源重放；连接项列表
// 拼接；两侧 NETS 表同键 DSNet 连接条目拼接；两侧几何条目拼接；两侧
// crossing 集并）。
class DSPartitionProduct {
public:
    DSPartInstances instances_;
    DSPartInstConnections inst_connections_;
    // 信号网连接 → 正式对象 /NETS；pg 网连接 → /NETS_PG
    DSPartitionNets nets_;
    DSPartitionNets nets_pg_;
    // 信号网几何 + OBS 桶（键 0）→ /GEOMETRY；pg 网几何 → /GEOMETRY_PG
    DSPartitionGeometry geometry_;
    DSPartitionGeometry geometry_pg_;

    void merge_from(const DSPartitionProduct& src);

    FLY_SERIALIZE(instances_, inst_connections_, nets_, nets_pg_, geometry_,
                  geometry_pg_)
};

// S9 展开算法（每 block 定义一调用；方案「每份 DEF 数据只读一次」）：
// 该 def 的实例/网产物 + 容器（via cell 权威表几何）+ 分区表 → 所涉各
// 分区的分片列表（仅产出非空分区）。连接项 id 形态直换（instance local
// id → +inst_start、pin id 直存——名字换算已在 S5b 解析边界完成，本层
// 零字符串匹配）；tree 与产物无对齐校验（树构建期已 fatal 对齐错误）；
// def 未被实例化（树上无位置）→ 空结果放行（dev-rules §7）。
CMVector<std::pair<CMPartitionId, DSPartitionProduct>> ds_flatten_block(
    const DSHierTree& tree, const DSBlockBuildData& block,
    const DSNetBuildData& nets, const DSDesign& design,
    const CMVector<DSSubPartition>& partitions);

// pg 网全局集分区片段提取（S9 每分区合并任务调用）：本区 NETS_PG 对象
// 表键按 DSNet.use_ 分流（POWER → power_ids_ / GROUND → ground_ids_；
// use 非 POWER/GROUND 的 special 网不入 pg 全局集）。
DSPgNetSlice ds_collect_pg_net_slice(const DSPartitionNets& nets_pg);

}  // namespace fly
