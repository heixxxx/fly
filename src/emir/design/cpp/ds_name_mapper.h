#pragma once

// =============================================================================
// DSNameMapperT — design db 的全局 name 组装层（R7，裁定 ㊵①㊹㊻；方案
// design-db-phase2-plan.md §0 ㊹/㊻ 行）。
//
// 三层职责的第二层（㊹ 架构终局）：**真正的 name mapper**——内部按需
// 持有各 block 的 hasher（注入表）+ 层级树引用：
//   get_global_id(full_hier_name) = 拆路径（'/' 分隔）→ 自根按实例名
//     逐层定位 DSHierNode → 叶层 hasher get_id(leaf) → local id +
//     该节点区间 start（⑨ 换算：instance = start + local、local 0 →
//     self_global_id ⑧；net = start + local、local 从 1 起）；
//   get_full_name(global_id) = 区间反查节点 → 叶层 hasher get_name →
//     递归向上拼 block instance 名 prefix（'/' 连接）。
//
// 注入式轻壳（㊻）：**不序列化、不落盘**（无 FLY_SERIALIZE——自身状态
// 极轻、运行时构造）；set_block_hasher 按需注入部分或全部（block 标识
// = cell id 主口 / cell name 便利口经树解析），hasher 级共享注入
//（CMSharedPtr<const DSNameHasherT<IdT>>，业务层零裸指针零拷贝零
// move——生命周期由计数自动管理）；查询时叶层未注入 → get_global_id
// 返回 kInvalidId、get_full_name 返回空名（局部注入 = 局部可查）。
// hasher 随 DSBlockNames_<i> 伴生对象独立落盘（㊵②）、read_object 读回
// 后经共享指针注入——「用户需要什么专门加载什么」的按需体系完成形态
//（⑰/⑱ 同构）。统一组装经 ds_make_instance_name_mapper /
// ds_make_net_name_mapper 双口工厂。
//
// 分派索引（R8c，裁定 53：方案 B——全局 block 路径前缀索引 + 最长前缀
// 下降）：get_global_id 的树逐层下降原为 find_child_by_instance_name
// 线性扫 children（O(扇出×段长)/层，顶层扇出数百~数千时反超叶层查询），
// 收编为内建**分派索引** = `DSHasherBackendHatrie<uint32_t>` 直接实例
//（键 = block 层次全路径（'/' 连接实例名，含 root 实例名段）、值 = 树
// 节点 id；复用 R8b backend 封装、不用整只 hasher——id→name 侧由
// DSHierNode 的 name 与 id 双存承担，分派索引是纯 name→id 视图）。
// 索引只依赖树、不依赖 hasher 注入（set_block_hasher 不触发重建），
// 构造与 set_tree 时自动从 DSHierTree 遍历重建（对「显式 build」与
// 「首次查询惰性构建」二选一中取最简形态：无需 mutable、无首次查询
// 并发构建窗口、无树更换后忘重建的静默错；树在 mapper 生命周期内被
// 修改的罕见场景由显式 rebuild_dispatch_index() 兜底）；运行时构件
// **不序列化**（mapper 本无 FLY_SERIALIZE，㊻ 语义不变）。
//
// 边界（㊺）：仅服务 instance/net 两维度（唯它们有 local id 与层级组装
// 需求）；cell/pin/layer/via cell 的 id 天然全局、其 hasher 即完整查询
// 结构，与 DSNameMapper 无任何关联。
// 两维度 = 两个强类型实例化（DSInstanceNameMapper = <CMInstanceId>、
// DSNetNameMapper = <CMNetId>，审计 B-4/B-5c：hasher 底座强类型化后
// 两维度 hasher 不再同型，维度即类型——原 DSNameMapperKind 运行时
// 分派废除，区间换算差异由 if constexpr 编译期编码，跨维度误用编译期
// 报错）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <emir/common/cpp/emir_ids.h>
#include <emir/design/cpp/ds_name_hasher.h>
#include <emir/design/cpp/ds_types.h>

#include <cstdint>
#include <type_traits>

namespace fly {

template <typename IdT>
class DSNameMapperT {
public:
    // 哨兵口径收编（㊴）：与 hasher 底座同值（id 类型最大值）
    static constexpr IdT kInvalidId = DSNameHasherT<IdT>::kInvalidId;

    static constexpr bool is_valid_id(IdT id) {
        return DSNameHasherT<IdT>::is_valid_id(id);
    }

    DSNameMapperT() = default;
    // tree 为共享观察（§16 业务层零裸指针——评审 B-5a：aliasing
    // shared_ptr 持宿主 DSDesign 计数、指向其内联层级树成员，mapper 生
    // 命期自保证树存活）。构造即建分派索引（R8c 裁定 53，见文件头——
    // 索引只依赖树，与 hasher 注入无关）。维度 = IdT 类型本身
    //（CMInstanceId/CMNetId——审计 B-5c 双实例化）
    explicit DSNameMapperT(CMSharedPtr<const DSHierTree> tree)
        : tree_(std::move(tree)) {
        rebuild_dispatch_index();
    }

    // 换树即重建分派索引（R8c 裁定 53：索引与树一一对应，换树后旧键
    // 全部失效——自动重建避免「忘重建查错树」的静默错误）
    void set_tree(CMSharedPtr<const DSHierTree> tree) {
        tree_ = std::move(tree);
        rebuild_dispatch_index();
    }

    // 分派索引显式重建（R8c 裁定 53）：树在 mapper 生命周期内被原地
    // 修改（children 增删 / 实例名改写——罕见，构建完成后的树对 mapper
    // 只读）后的兜底入口；构造 / set_tree 已自动调用
    void rebuild_dispatch_index();

    // —— 注入接口（㊻；hasher 级共享注入，业务层零裸指针零拷贝零
    // move）——hasher 在伴生对象（DSBlockNames，㊵②）与 DSBlockBuildData
    // 运行时字段均为 CMSharedPtr 持有：注入 = shared_ptr 拷贝（const 化
    // CMSharedPtr<const T>，计数管理生命周期），查询全程只读（get_id/
    // get_name 均 const；emplace/assign 仅建库期使用、不经 mapper）。
    // 主口：block 标识 = cell id（block 与 DSCell 同构，㉙；树节点经
    // block_cell_id_ 关联；CMCellId 强类型——评审 B-5b）。重复注入同键
    // 覆盖指向；空指针撤销注入。注入 hasher 类型与 mapper 维度强绑定
    //（instance mapper 只收 instance hasher——类型即维度，审计 B-5c）
    void set_block_hasher(CMCellId cell_id,
                          CMSharedPtr<const DSNameHasherT<IdT>> hasher) {
        if (hasher == nullptr) {
            injected_.erase(cell_id);
            return;
        }
        injected_[cell_id] = std::move(hasher);
    }
    // 便利口：block cell 名经树解析（遍历节点取首个同名 block_cell_id_；
    // 未知名不注册不生效）
    void set_block_hasher(const CMString& cell_name,
                          CMSharedPtr<const DSNameHasherT<IdT>> hasher);
    // 已注入的 block 定义数
    size_t injected_count() const { return injected_.size(); }

    // 正向组装：完整层级实例名路径（'/' 分隔，如 "top/i2/i1/n0"）→
    // global id；路径无法定位 / 叶层未注入 / 叶名未命中 → kInvalidId。
    // R8c 起分派走内建索引（最后一段固定为叶名，其前全部段拼接的层次
    // 路径对分派索引一次 find 得叶层 block 节点）
    IdT get_global_id(const CMString& full_hier_name) const;

    // 反向组装：global id → 完整层级路径（local 0 / block instance 自身
    // → 该 block instance 的实例名路径 ⑧）；未命中 / 叶层未注入 → 空名
    CMString get_full_name(IdT global_id) const;

private:
    // 层级树共享观察（aliasing shared_ptr 持宿主 design 计数——§16 业
    // 务层零裸指针，评审 B-5a）
    CMSharedPtr<const DSHierTree> tree_;
    // 注入表：block cell id → hasher 只读视图（CMSharedPtr 共享计数，㊻
    // 不序列化不落盘；hasher 类型与 mapper 维度同 IdT 强绑定）
    CMUnorderedMap<CMCellId, CMSharedPtr<const DSNameHasherT<IdT>>> injected_;
    // 分派索引（R8c 裁定 53 方案 B）：block 层次全路径 → 树节点 id 的
    // 纯 name→id 视图（DSHasherBackendHatrie<uint32_t> 直接实例，复用
    // R8b backend 封装；万级条目，运行时从 DSHierTree 遍历重建、不
    // 序列化——㊻ 注入式轻壳语义不变）
    DSHasherBackendHatrie<uint32_t> dispatch_;
};

// 实体别名（㊹ 两维度 = 强类型双实例化，审计 B-5c；别名不带位宽标识）
using DSInstanceNameMapper = DSNameMapperT<CMInstanceId>;
using DSNetNameMapper = DSNameMapperT<CMNetId>;

// 显式实例化（双维度唯一实例化组；定义在 ds_name_mapper.cpp）
extern template class DSNameMapperT<CMInstanceId>;
extern template class DSNameMapperT<CMNetId>;

// 统一组装工厂（㊵②+㊻ 的「读 DSBlockNames + 构造 mapper + 注入」封装
// 的 C++ 侧；Python 统一加载 API 同构）：遍历 per-DEF 伴生对象集，按
// block 名（= block cell 名）经容器 cell hasher 解析 cell id 注入。
// 同名 block 保留首份（与 ds_build_hier_tree 的 def_by_cell_name 一致）。
// 返回的 mapper 经 aliasing shared_ptr 持 design 内树（评审 B-5a：生命
// 周期自持——调用侧不必再保证 design 存活期覆盖 mapper）。names 为共
// 享集（§16 业务层零裸指针——评审 B-12）。双口 = 两维度各一（维度即
// 类型，审计 B-5c——原 kind 入参运行时分派废除）。
DSInstanceNameMapper ds_make_instance_name_mapper(
    CMSharedPtr<const DSDesign> design,
    const CMVector<CMSharedPtr<const DSBlockNames>>& names);
DSNetNameMapper ds_make_net_name_mapper(
    CMSharedPtr<const DSDesign> design,
    const CMVector<CMSharedPtr<const DSBlockNames>>& names);

}  // namespace fly
