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
（返回 (stats, fake cells)——fake cell 数据经独立临时对象传 S5a 汇总，
2026-09-13 裁定产物本体不含副本）/ ds_merge_block_build（责任链
DSInstancePipeline 含 unique_ptr 不可绑定，装配在 C++ 侧内部）。
网内容解析：EXDSNetStats/EXDSNetBuildData（网内容产物只读面）+
EXDSNetConnection（连接项 id 形态只读面，flags 六位 port/driver/receiver/
power/ground/clock，hybrid = driver+receiver 同置）+ EXDSDefNetsStats +
ds_parse_def_nets（网内容链 DSNetPipeline 同上不上 Python）。
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
S9 flatten（2026-09-13 重组裁定 + 2026-09-14 拆分裁定）：
EXDSPartitionProduct（分片中间形态，merge_from + 六类成员访问——nets/
nets_pg/geometry/geometry_pg 分侧四成员）/ EXDSGeomEntry /
EXDSPartConnection（INST_CONNECTIONS 条目）/ EXDSNetConnEntry / EXDSNet /
EXDSPartitionNets（/NETS 与 /NETS_PG 共用类——单表形态，net_of 单表查；
2026-09-14 拆分裁定：原信号网/pg 网两表单对象拆为两个独立正式对象）/
EXDSPgNetSlice / EXDSPgNetSet（全局 pg 网 id 集，is_power/is_ground/is_pg
O(1) 查询口）+ ds_flatten_block（每定义一调用展开）/
ds_collect_pg_net_slice（分区 NETS_PG 对象 → pg 片段）/
ds_build_pg_net_set（全部分区片段 → 两 set 去重合并）。
S10 汇总校验：EXDSIdDomain（id 连续性观测域）/ EXDSPartitionCheckResult
（分区级校验结果，Python 面仅规模计数）/ EXDSDesignCheckReport（全局校验
报告，正式对象 "verify_report"）+ ds_verify_partition（每分区一校验）/
ds_verify_design（全局汇总校验）/ ds_verify_report_or_fatal（损坏类处置：
非空即 fatal 退出码 80——阻断损坏库冻结）。
id → partition 反向映射（2026-09-13 debug 定位裁定；2026-09-14 拆分裁
定 NET 片段源 = 两几何对象键集并集）：EXDSIdPartitionSlice
（S9 每分区合并任务的临时片段）/ EXDSIdPartitionSegment（段正式对象，
定长 pids 数组 + 空洞哨兵）/ EXDSIdPartitionIndex（段表轻对象）+
ds_collect_partition_id_slice（分区产物 → 本区片段：primary inst id 集 /
两几何对象键集并集）+ ds_merge_id_partition_slices（多片段 → (段表, 段
集)）。
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
    EXDSIdPartitionIndex,
    EXDSIdPartitionSegment,
    EXDSIdPartitionSlice,
    EXDSInstance,
    EXDSInstanceStats,
    EXDSLayer,
    EXDSLefParseStats,
    EXDSNameMapper,
    EXDSNetBuildData,
    EXDSNetConnection,
    EXDSNetStats,
    EXDSPartConnection,
    EXDSPartInstConnections,
    EXDSPartInstances,
    EXDSPartitionGeometry,
    EXDSPartitionNets,
    EXDSPartitionProduct,
    EXDSPgNetSet,
    EXDSPgNetSlice,
    EXDSNet,
    EXDSNetConnEntry,
    EXDSPin,
    EXDSPinGeometry,
    EXDSPinTables,
    EXDSSubPartition,
    EXDSStack,
    EXDSNetUnion,
    EXDSNetUnionSlice,
    EXDSPartitionCheckResult,
    EXDSViaCell,
    ds_build_hier_tree,
    ds_build_net_union,
    ds_build_pg_net_set,
    ds_collect_net_union_slice,
    ds_collect_partition_id_slice,
    ds_collect_pg_net_slice,
    ds_decide_partitions,
    ds_flatten_block,
    ds_make_name_mapper,
    ds_merge_block_build,
    ds_merge_cell_lef,
    ds_merge_def_header,
    ds_merge_global_density,
    ds_merge_id_partition_slices,
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
