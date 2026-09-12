#pragma once

// =============================================================================
// design db 的 LEF 解析适配层（S1 tech lef / S2 cell lef，实施计划 §3.1）。
//
// 职责：把 lefrReader 回调（lefdef 库，上游 API）转换为 DS* 领域对象。
// 两个入口对应 S1/S2 两阶段，一套适配逻辑两个模式：
//   - S1 ds_parse_tech_lef：层堆叠（Stack 独立类型，裁定 ⑭）+ tech 级
//     via（权威表首批，裁定 ⑪）+ VIARULE GENERATE 按规则默认参数直接
//     展开为模板 DSViaCell（㉚：DSViaRule 已删除）。DBU 基准：裁定 ㉝
//     全局恒 1000 DBU/µm——UNITS 回调不注册，DATABASE MICRONS 声明值
//     不写 stack、不参与任何换算，LEF 几何（µm 浮点）恒乘
//     DSStack::kGlobalDbuPerMicron。
//   - S2 ds_parse_cell_lef：macro/pin/OBS → 临时 DSDesign（局部 id，
//     裁定 ①——全局 id 由 T6 汇总统一重排）+ pin 几何独立对象
//     （⑰：几何不进简化 pin；R4 起按局部平铺 pin id 落位）+ 文件级 via
//     集。stack 为只读层表（层名→id 解析）；cell lef 的 UNITS 声明不做
//     一致性校验（lef 间 DBU 不一致不再 raise，D15 该子项撤销）。
//
// 初始化序列（T2 实施记录红线）：lefrInitSession() +
// lefrSetRegisterUnusedCallbacks()，缺一会在解析中段段错误（实测）。
//
// 异常语义（dev-rules §7.2 流程错误处理范式，2026-09-13 裁定）：文件不可
// 读 → std::runtime_error（§7 第一类）。语法/格式错误（lefrRead 非 0）按
// 入口二元处置：tech lef → fatal message DSGN::0017（层表来源损坏，范式
// (a)）；cell lef → 兜底空产物 + stats.parse_failed_count=1（范式 (b)，
// DSGN::0014/0015 由 flow 汇总层发，cell 缺失由 fake cell 承接）。
// 层引用未定义不再 raise——经 ds_resolve_layer_id 发 DSGN::0010 提醒后
// 条目级丢弃（via 整条放弃、rect 逐条丢弃）并计入
// stats.skipped_layer_ref_count。implant 等非布线/切割层跳过并计数
// （兜底模式「跳过并计数」），重名 macro/via 保留首份并计数（「抛弃并
// 提醒」，提醒消息 DSGN 注册后于 T6 接线）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>

namespace fly {

// LEF 解析统计（T6 汇总与 DSGN 消息的数据源）
struct DSLefParseStats {
    int layer_count = 0;            // 收录层（routing/cut；tech 模式）
    int skipped_layer_count = 0;    // 跳过层（implant/masterslice/overlap 等）
    int macro_count = 0;            // 收录 macro（重名保留首份后；cell 模式）
    int skipped_macro_count = 0;    // 重名跳过 macro（保留首份）
    int pin_count = 0;              // pin 总数（cell 模式）
    int via_count = 0;              // via 收录数
    int via_conflict_count = 0;     // via 重名冲突（保留首份抛弃后续）
    int viarule_count = 0;          // VIARULE 展开为模板 via cell 数（㉚）
    int skipped_layer_ref_count = 0;  // 未定义层引用丢弃条目数（via 整条/
                                      // pin/OBS rect 逐条；DSGN::0010）
    int skipped_geometry_count = 0; // 跳过的非矩形几何项（polygon/path 等，
                                    // 发现遗漏可扩展处理）
    int parse_failed_count = 0;     // 语法/格式错误兜底标记（0/1）：lefrRead
                                    // 非 0 时清空产物后置 1（流程错误处理
                                    // 范式 2026-09-13——cell lef 单文件失败
                                    // 不 raise，任务级兜底；tech lef fatal
                                    // 不经此标记）
};

// S1：tech lef → DSStack（层堆叠；DBU 基准恒 1000 见 ㉝，UNITS 声明不
// 参与）+ tech 级 via 集（含 VIARULE GENERATE 展开的模板 DSViaCell，㉚）。
// 语法错误 = fatal（DSGN::0017，层表来源损坏无法兜底——dev-rules §7.2
// 2026-09-13 裁定）；文件不可读仍 raise（§7 第一类）。
DSLefParseStats ds_parse_tech_lef(const CMString& path, DSStack& stack,
                                  CMVector<DSViaCell>& tech_vias);

// S2：cell lef → 临时 DSDesign（局部 id = cells_ 下标，含简化 pin 与
// obs 几何；局部 pin namemap 同步注册，R4）+ pin 几何独立对象（键 =
// 局部平铺 pin id）+ 文件级 via 集。stack 为只读层表（层名→id 解析）。
// 语法错误 → 兜底：清空产物 + stats.parse_failed_count=1（DSGN::0014 由
// flow 汇总层发），不 raise（范式 2026-09-13；cell 缺失由 fake cell 承接）。
DSLefParseStats ds_parse_cell_lef(const CMString& path, const DSStack& stack,
                                  DSDesign& design_out,
                                  DSPinGeometry& pin_geometry_out,
                                  CMVector<DSViaCell>& vias);

}  // namespace fly
