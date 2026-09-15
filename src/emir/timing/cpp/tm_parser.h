#pragma once

// TWF（Timing Window File，时序窗口文件）解析器：楷登 Innovus
// write_timing_windows 产物（含 -power_compatible 风味）。
//
// 开源现状（2026-09-15 全站检索结论）：无任何语言的完整开源解析器
// （CircuitNet read_twf 仅取 WAVEFORM 周期与两组到达窗口，作交叉参照），
// 按 dev-rules §4 自研 C++ 解析器。字节级语法权威源 = 命令手册 25.10
// 版：S 表达式，顶层 (TIMING_WINDOWS (HEADER ...) (WAVEFORM ...)
// (CAUSED_BY "clk" RISE|FALL|NULL (NET|PIN "name" 八对字段 C|D) ...) ...)
// —— 八对字段 = RTW/Rt/RDr/RSlk + FTW/Ft/FDr/FSlk。
//
// 语义约定（偏差已在 docs/emir/timing-db 立项方案裁定）：
//   - 单位：全部时间值 × TIME_SCALE 换算 ns 存储；
//   - 「a:b」对与单标量 a（min = max）均接受；「*」= 无数据；
//   - RDr/FDr（源电阻）与 RSlk/FSlk（富余量）弃收 + 计数（无消费者）；
//   - C/D 结尾标记计数不入库（语义无公开说明）；
//   - 同名条目跨 CAUSED_BY 分组合并：窗口/翻转逐字段取并集、首现时钟
//     为主时钟、时钟源差异置 multi_source；
//   - 引号内名字去除反斜杠转义（Innovus 名字转义清理，与 CircuitNet
//     read_twf 行为一致）；
//   - 条目级破损（字段数不符/值非法/名字形态错误）跳过 + 计数（括号
//     配对恢复记录边界）；流级破损（顶层结构破坏/未闭合引号/结尾杂散
//     内容）抛 std::runtime_error。

#include <emir/timing/cpp/tm_types.h>

namespace fly {

// 解析 TWF 文件（整读）。文件不可读或流级语法错误抛 std::runtime_error。
TMTimingFile tm_parse_twf_file(const CMString& path);

// 解析内存文本（单测 / 后续单文件分布式切块复用）。source_name 仅作
// source_file_ 可追溯标注。
TMTimingFile tm_parse_twf_text(const CMString& text,
                               const CMString& source_name);

}  // namespace fly
