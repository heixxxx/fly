// baseline_parse_test 的两个解析 driver 的公共接口声明。
// LEF 与 DEF 分属独立翻译单元（各自 include 上游头，隔离 lex 层实体），
// 测试主文件只经本头消费计数结果。
#pragma once

namespace lef_baseline {

// LEF 解析计数（complete.5.8.lef 基线，回调只做计数）
struct LefCounts {
    int macros = 0;   // MACRO 语句（lefrSetMacroCbk）
    int layers = 0;   // 顶层 LAYER 语句（lefrSetLayerCbk）
    int vias = 0;     // 顶层 VIA 定义（lefrSetViaCbk）
    int pins = 0;     // MACRO 内 PIN 语句（lefrSetPinCbk）
    int status = -1;  // lefrRead 返回值（0 = 解析成功）
};

// 解析 path 指向的 LEF 文件，计数写入 counts。
// 返回 0 = 文件可读并完成解析流程；-1 = 文件不可读。
int parse_lef(const char* path, LefCounts* counts);

}  // namespace lef_baseline

namespace def_baseline {

// DEF 解析计数（complete.5.8.def 基线）。*_start 系列 = 段头声明计数
// （解析器读 "SECTION N ;" 行后触发 start 回调，附带声明值 N）；对象
// 计数 = 逐条解析触发的对象回调次数。两条独立解析路径互相印证。
struct DefCounts {
    int via_start = 0;      int via_start_declared = 0;   int vias = 0;
    int comp_start = 0;     int comp_start_declared = 0;  int comps = 0;
    int pins_start = 0;     int pins_start_declared = 0;  int pins = 0;
    int net_start = 0;      int net_start_declared = 0;   int nets = 0;
    int snet_start = 0;     int snet_start_declared = 0;  int snets = 0;
    int region_start = 0;   int region_start_declared = 0; int regions = 0;
    int groups_start = 0;   int groups_start_declared = 0; int groups = 0;
    int group_names = 0;
    int slot_start = 0;     int slot_start_declared = 0;  int slots = 0;
    int fill_start = 0;     int fill_start_declared = 0;  int fills = 0;
    int blockage_start = 0; int blockage_start_declared = 0; int blockages = 0;
    int ndr_start = 0;      int ndr_start_declared = 0;   int ndrs = 0;
    int styles_start = 0;   int styles_start_declared = 0; int styles = 0;
    int die_area = 0;                                    int die_area_points = 0;
    int die_xl = 0, die_yl = 0, die_xh = 0, die_yh = 0;
    double units = 0.0;
    int status = -1;  // defrRead 返回值（0 = 解析成功）
};

// 解析 path 指向的 DEF 文件，计数写入 counts。
// 返回 0 = 文件可读并完成解析流程；-1 = 文件不可读。
int parse_def(const char* path, DefCounts* counts);

}  // namespace def_baseline
