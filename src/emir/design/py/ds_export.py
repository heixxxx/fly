"""design 模块 export 层——_fly_emir_design.so 符号唯一导入点。

全部 C++ 绑定符号（EXDS* 数据结构 + 解析入口 + 汇总 merge）经本层导入；
design 模块内其他文件一律从本文件取符号，不直连 .so。
R5：EXDSPort/EXDSBlock/EXDSViaRule 删除（port 复用 EXDSPin、block 复用
EXDSCell、VIARULE 展开为 EXDSViaCell）。
R6：EXDSInstance（pos/orient 二元组）。
R7 name 体系收敛（㊱㊳㊴㊸㊹㊻）：EXDSPin/EXDSInstance 删 name 面（name
分层存储在 hasher）；EXDSBlockNames（㊵② 伴生对象，DSBlockNames_<i>
独立落盘）+ EXDSNameMapper（㊻ 注入式轻壳，运行时构造不落盘）+
ds_make_name_mapper 统一组装工厂。
COMPONENTS 解析：EXDSDensityGrid/EXDSInstanceStats/EXDSBlockBuildData
（per-DEF 产物只读面）+ EXDSDefComponentsStats + ds_parse_def_components
/ ds_merge_block_build（责任链 DSInstancePipeline 含 unique_ptr 不可绑
定，装配在 C++ 侧内部）。
网内容解析：EXDSNetStats/EXDSNetBuildData（网内容产物只读面）+
EXDSDefNetsStats + ds_parse_def_nets（网内容链 DSNetPipeline 同上不上
Python）。
层级树：EXDSHierNode/EXDSHierTree（层级树只读面，⑮ 四接口 + ⑨ 换算）+
ds_build_hier_tree（构建在 C++，多根/零根/环 raise D22）。
S8 分区：EXDSSubPartition（core/extend 双区域，core/extend 四元组）+
ds_merge_global_density（全局密度合并）/ ds_decide_partitions（分区决策，
通道比重散参传入）。
S7 跨块连接归并：EXDSNetUnion（find/members/class_count/dangling 只读面）
+ EXDSNetUnionSlice（per-DEF 局部收集临时产物，观测面仅规模计数）+
ds_collect_net_union_slice（每父块 DEF 一收集）/ ds_build_net_union（全局
汇总：两层化 + root 规范化 + 悬空计数）/ ds_net_union_child_indexes（编排
辅助：def 序号 → 子定义序号集）。
S10 汇总校验：EXDSIdDomain（id 连续性观测域）/ EXDSPartitionCheckResult
（分区级校验结果，Python 面仅规模计数）/ EXDSDesignCheckReport（全局校验
报告，正式对象 "verify_report"）+ ds_verify_partition（每分区一校验）/
ds_verify_design（全局汇总校验）/ ds_verify_report_or_fatal（损坏类处置：
非空即 fatal 退出码 80——阻断损坏库冻结）。
"""

from _fly_emir_design import (
    EXDSBlockBuildData,
    EXDSBlockNames,
    EXDSCell,
    EXDSDefComponentsStats,
    EXDSDefNetsStats,
    EXDSDefParseStats,
    EXDSDesign,
    EXDSDesignCheckReport,
    EXDSHierNode,
    EXDSHierTree,
    EXDSDensityGrid,
    EXDSGeomEntry,
    EXDSIdDomain,
    EXDSInstance,
    EXDSInstanceStats,
    EXDSLayer,
    EXDSLefParseStats,
    EXDSNameMapper,
    EXDSNetBuildData,
    EXDSNetStats,
    EXDSPartConnection,
    EXDSPartInstConnections,
    EXDSPartInstances,
    EXDSPartNetConnections,
    EXDSPartitionGeometry,
    EXDSPartitionProduct,
    EXDSPin,
    EXDSPinGeometry,
    EXDSPinTables,
    EXDSPowerPin,
    EXDSSubPartition,
    EXDSStack,
    EXDSNetUnion,
    EXDSNetUnionSlice,
    EXDSPartitionCheckResult,
    EXDSViaCell,
    ds_build_hier_tree,
    ds_build_net_union,
    ds_collect_net_union_slice,
    ds_decide_partitions,
    ds_flatten_block,
    ds_make_name_mapper,
    ds_merge_block_build,
    ds_merge_cell_lef,
    ds_merge_def_header,
    ds_merge_global_density,
    ds_net_union_child_indexes,
    ds_parse_cell_lef,
    ds_parse_def_components,
    ds_parse_def_header,
    ds_parse_def_nets,
    ds_parse_tech_lef,
    ds_verify_design,
    ds_verify_partition,
    ds_verify_report_or_fatal,
)
