#pragma once

// =============================================================================
// design db 的 DEF 解析适配层（S4 头部轻量扫描 + S4b via 定义解析 +
// S5a COMPONENTS 责任链 ∥ 网名扫描，实施计划 §3.2；同一遍回调读取、
// 语义上多个产出通道）。
//
// S4 产出：block cell（㉙：DSCell + block_cell 位——DESIGN 名 / 来源路径 /
// DIEAREA 双存 bbox + polygon（㉞）/ origin = −diearea 左下角（P7）/ DBU
// 换算系数 / port 集 = cell.pins_ 的 port 位 DSPin）+ port 几何独立对象
// （键 = 局部 pin 下标，汇总时按全局 pin id 重挂）。S4b 产出：VIAS 段
// via → DSViaCell，登记名带 `design_name::` 前缀（裁定 ⑫，防跨 DEF 同名
// 合并污染）。
//
// S5a 产出（ds_parse_def_components，每 DEF 一独立并行任务）：COMPONENTS 真回调
// 经责任链产出 DSBlockBuildData（per-DEF 实例表 + 密度通道 + fake cell
// 登记 + 统计）∥ NETS/SPECIALNETS 网名扫描（③ NetNameOnly）——同一遍
// DEF 读取同时收 components 与网名（SkipNetDetails/SkipSNetDetails 只跳
// 网体、保留网名回调；defrSetNetNameOnly 会连带 SkipComponents，不适用）。
//
// 大段跳过（接口探明结论，lefdef fork 已吸收的选择性解析 API）：
// defrSetSkipComponents / defrSetSkipNets / defrSetSkipSpecialNets 为
// **parser 层真跳过**（跳过对应 section 的对象构建，非仅不回调）；
// S4 头扫描对三大段零构建；S5a 不 skip COMPONENTS、NETS/SPECIALNETS 走
// detail-skip（仅网名回调）。
//
// 初始化红线（T2/T4 实施记录）：defrInitSession() +
// defrSetRegisterUnusedCallbacks()，回调注册必须在其后（经 run_defr
// 的 register_cbs）。
//
// 坐标换算：DEF 坐标（DBU_def，DEF UNITS DIST MICRONS N）→ 全局 DBU：
// v × stack.dbu_per_micron / def_units（裁定 ㉝：stack 基准恒 1000，
// 见 DSStack::kGlobalDbuPerMicron），int64 四舍五入（与 T4 一致）。
// DIEAREA 经 defiBox::getPoint() 取完整点集（T2 红线：xl/yl/xh/yh 是
// 前两点兼容赋值不可信）：>2 点 = 多边形（polygon 全点集 + is_polygon
// 置位）、2 点 = 矩形（polygon 空）；bbox 恒存（㉞ 双存）。
//
// 异常语义（dev-rules §7.2 流程错误处理范式，2026-09-13 裁定）：文件不可
// 读 → std::runtime_error（§7 第一类）；DEF 语法/格式错误（defrRead 非 0）
// → fatal message DSGN::0016（design db 数据不完整无意义，范式 (a) 结束
// 整个 run）。层引用未定义不再 raise——经 ds_resolve_layer_id 发
// DSGN::0010 提醒后条目级丢弃（rect 丢弃该矩形、via 整条放弃、wire 段/
// rect 项丢弃）并计入 skipped_layer_ref_count。重名 port / 重名 via /
// 重名网保留首份并计数（DSGN::0005/0006 的数据源，T6 接线）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>

namespace fly {

// DEF 头扫描统计（S4/S4b；T6 汇总与 DSGN 消息的数据源）
struct DSDefParseStats {
    int block_count = 0;         // 产出 block cell（每 DEF 至多 1，DESIGN 语句）
    int port_count = 0;          // 收录 port（port 位 DSPin）
    int skipped_port_count = 0;  // 重名 port 保留首份（DSGN::0006 场景）
    int via_count = 0;           // 预定义 via 收录数
    int viarule_via_count = 0;   // 生成式 via（VIARULE 语句）展开收录数
    int via_conflict_count = 0;  // 前缀化登记名冲突（保留首份，DSGN::0005）
    int die_area_count = 0;      // DIEAREA 触发次数
    int skipped_layer_ref_count = 0;  // 未定义层引用丢弃条目数（rect/via；
                                      // DSGN::0010）
    int obstruction_count = 0;        // 收录 BLOCKAGE 矩形数（S9 前置收录，
                                      // D17 修订：入分区 geometry net 0）
    int skipped_polygon_obstruction_count = 0;  // 多边形 BLOCKAGE 未收录数
                                                //（首版仅矩形几何）
};

// S4+S4b：DEF 头部一遍读取 → block cell + port pin 名序列 + port 几何 +
// via 定义集（前缀化登记名）+ obstruction 集（BLOCKAGES 段，2026-09-13
// D17 修订收录：block 局部坐标换全局 DBU 基准，flow 侧转入
// DSBlockBuildData.obstructions_ 供 S9 入分区 geometry net 0 + OBS 位；
// 未定义层引用条目级丢弃 + 计数同族兜底）。生成式 via（VIARULE 语句）
// 按 DEF 自带参数展开（D13）：CUTSIZE 为 cut（中心对齐）、LAYER 三层序、
// ENCLOSURE 为 cut 外扩量——lef 侧展开（㉚，同参数语义）产物不参与本入
// 口，DEF 生成式语法自带全部展开参数。R7 ㊱：DSPin 不存 name——
// port_names_out 与 block_cells_out[0].pins_ 下标对齐（每 DEF 至多 1 个
// block cell），由汇总 ds_merge_def_header 传进全局 pin hasher。
void ds_parse_def_header(const CMString& path, const DSStack& stack,
                         CMVector<DSCell>& block_cells_out,
                         CMVector<CMString>& port_names_out,
                         DSPinGeometry& port_geoms_out,
                         CMVector<DSViaCell>& def_vias_out,
                         CMVector<DSShapeRef>& obstructions_out,
                         DSDefParseStats& stats);

// S5a 统计（网名扫描侧；实例/密度/统计在 DSBlockBuildData 内）
struct DSDefComponentsStats {
    int component_count = 0;    // COMPONENTS 回调条数（含 UNPLACED）
    int net_count = 0;          // 收录网名数（NETS + SPECIALNETS，去重后）
    int skipped_net_count = 0;  // 重名网保留首份（同 DEF 跨段重名兜底）
};

// S5a：DEF 一遍读取 → COMPONENTS 责任链（四节点，内部装配
// ds_make_components_pipeline）产出 per-DEF 产物 + 网名扫描填 local net
// namemap。
// density_bin_dbu = 密度采样格边长（全局 DBU，正方形格子；flow 侧由
// alpha 配置换算）——DIEAREA 换算后配置格网（原点 = diearea 左下角，
// 行列数向上取整覆盖 diearea）；DIEAREA 缺失（非法 DEF）时格网不配置，
// 密度通道空转。block_data 需为空产物（占位由本入口经 DESIGN 回调
// init_placeholder 建立）。
void ds_parse_def_components(const CMString& path, const DSStack& stack,
                             const DSDesign& design,
                             DSBlockBuildData& block_data,
                             DSDefComponentsStats& stats,
                             int32_t density_bin_dbu);

// S5b 统计（批处理侧；实例/网名在 S5a 侧。几何/via instance 计数镜像
// 产物内 DSNetStats，另加批次观测）
struct DSDefNetsStats {
    int net_count = 0;           // 收录网数（NETS + SPECIALNETS 有内容者）
    int connection_count = 0;    // 连接项总数
    int wire_count = 0;          // wire 段总数
    int rect_count = 0;          // rect 项总数
    int via_instance_count = 0;  // via instance 总数（VIADATA 展开后）
    int skipped_via_count = 0;   // 未定义 via 引用跳过（DSGN::0008）
    int skipped_layer_ref_count = 0;  // 未定义层引用丢弃条目数（wire 段/
                                      // rect 项；DSGN::0010）
    int skipped_net_count = 0;   // 网名不在 S5a namemap 的防御兜底计数
    int batch_count = 0;         // 分批批次数（③ 分批落批可观测）
};

// S5b：DEF 一遍读取 → 网内容责任链（三节点，内部装配
// ds_make_nets_pipeline）+ 分批多阶段（裁定 ③）：Si2 流式单遍回调中按
// 网收集 DSNetContext，达到 net_batch_size 批界即走链落批（追加进
// net_data 后释放批缓冲），单流语法约束不变。Skip 开关口径：不跳
// NETS/SPECIALNETS 细节（defiNet 全量内容回调），COMPONENTS 保持 parser
// 层真跳过（实例已由 S5a 处理）。产物键 = local net id（⑨ block_data
// 的 namemap 对齐，缺失网名防御计数跳过）；via 引用按 ⑪ 权威表解析
//（plain → ⑫ design:: 前缀回退，未定义跳过 + DSGN::0008）。
// density_bin_dbu 同 S5a（DIEAREA 配置网侧密度格网）。net_data 需为空
// 产物。
void ds_parse_def_nets(const CMString& path, const DSStack& stack,
                       const DSDesign& design,
                       const DSBlockBuildData& block_data,
                       DSNetBuildData& net_data, DSDefNetsStats& stats,
                       int32_t density_bin_dbu, int net_batch_size);

}  // namespace fly
