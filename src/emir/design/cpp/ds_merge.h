#pragma once

// =============================================================================
// design db 的全局汇总与 merge 函数（S2 汇总 / S3 lib merge / S4 汇总，
// 实施计划 §4；裁定 ①：并行任务各自产出、全局汇总统一编号）。
//
//   - ds_merge_cell_lef：S2 汇总——单 cell lef 任务的中间产物（局部 id
//     的 DSDesign + pin 几何）并入全局容器：统一 cell id 分配（重名
//     macro 保留首份 + DSGN::0001）、pin namemap 重挂（局部 pin id →
//     全局平铺新 id + cell.pins_ 的 pin_id_ 回填）、via cell 合入（重名
//     保留首份 + DSGN::0005）、pin 几何按新全局 pin id 重挂（被抛弃
//     cell 的 pin 几何随之丢弃）。
//   - ds_merge_def_header：S4/S4b 汇总——block cell（㉙：DSCell +
//     block_cell 位）进 cell namemap（与 macro 同空间，重名保留首份 +
//     DSGN::0001）+ port pin id 平铺分配（pin_id_ 回填 + port 几何按
//     全局 pin id 重挂）+ via cell 权威表合入（⑫ 前缀名天然隔离跨 DEF
//     重名；同前缀重名保留首份 + DSGN::0005）。
//   - DSDesign::merge_lib：S3（裁定 ⑯，声明在 ds_types.h，实现于本 cpp）。
//
// 冲突一律保留首份 + MSG 提醒（兜底模式「抛弃并提醒」，不 raise）；
// 函数返回冲突计数（供调用方日志/统计）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>

namespace fly {

// S2 汇总：返回抛弃的重名 macro 数
int ds_merge_cell_lef(DSDesign& dst, const DSDesign& src_part,
                      DSPinGeometry& dst_geom,
                      const DSPinGeometry& src_geom);

// S4/S4b 汇总（单 DEF 产物一调用）：block_cells 为该 DEF 的 block cell
// （至多 1 个）、port_geoms 为其 port 几何（键 = 局部 pin 下标）、
// port_names 为该 block cell 的 port pin 名序列（按下标与 pins_ 对齐
// ——R7 ㊱：DSPin 不存 name，pin 名经解析边界传递进全局 pin hasher）。
// 返回抛弃的重名（block cell + via cell）数
int ds_merge_def_header(DSDesign& dst, const CMVector<DSCell>& block_cells,
                        const CMVector<CMString>& port_names,
                        DSPinGeometry& dst_geom,
                        const DSPinGeometry& port_geoms,
                        const CMVector<DSViaCell>& def_vias);

// S5a 汇总（单 per-DEF 产物一调用）：fake cell 并入全局 cell 表（id 保持
// 任务内分配值 ⑳；目标位已被占用时顺延到下一空位——⑳ 冲突率不严格，
// 正确性不依赖无冲突）+ instance 引用与统计键同步重映射 + namemap 注册
// + fake_cell_ids_ 索引。同名 fake 重复并入（同 block DEF 重复提交）
// 保留首份跳过。返回并入的 fake cell 数。
int ds_merge_block_build(DSDesign& dst, DSBlockBuildData& block_data);

// S6：层级树构建 + 起始编号分配（裁定 ⑧⑨⑮；消费 S5a 的实例/网计数与
// 引用关系 + S5b 的 via instance 统计计数——via 区间为四接口之区间反查
// 与 S9 global via id 换算的输入，故树构建时点在 S5b 之后）。blocks 与
// nets 按 def_paths 序一一对齐（指针借用、不拷贝大体量产物；数量不一
// 致 = 调用方契约错误，fatal 退出）；主 DEF 判定 = 唯一「无父者」（其
// block cell 不被任何其他 DEF 的实例引用；多根/零根 → 不可恢复结构错误
// fatal，D22/DSGN::0011——两表皆空时返回空树，兼容无 DEF 建库）。自根
// DFS 展开：block 定义的每次引用 = 一个 block instance 节点（定义 DAG
// 共享、实例层面为树），深度优先序连续分配 instance/net/via 三类区间
// （instance 长度 = instance_total 含 local 0 占位槽；net/via 长度 =
// 定义净计数，local id 从 1 起）。环检测：DFS 入环 → fatal（DSGN::0011）。
// fatal = MSG_FATAL_EXIT：进程以码 80 退出 + master 联动（2026-09-12
// 裁定，原 raise 改 fatal message；见 docs/message-system.md fatal 章节）。
// block 定义的 local 0（自身占位）→ 该 block instance 的 global id
//（⑧，存节点 self_global_id_）。
DSHierTree ds_build_hier_tree(const CMVector<const DSBlockBuildData*>& blocks,
                              const CMVector<const DSNetBuildData*>& nets,
                              const DSDesign& design);

}  // namespace fly
