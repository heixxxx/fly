"""emir design db 的 Python 包（七文件规范，docs/emir/dev-rules.md）。

加载顺序（有依赖）：export 层最先（.so 符号就位；依赖 _fly_emir_lib，
由 emir/__init__ 保证 lib 包先加载）→ 消息注册（副作用）→ db（容器 +
flow 入口）→ functions（对外读取函数）。utils/flow 为内部装配，不经包
根导出。
"""

# .so 符号唯一导入点（design 模块内其他文件一律从这里取符号）
from .ds_export import (  # noqa: F401
    EXDSBlockBuildData,
    EXDSBlockNames,
    EXDSCell,
    EXDSDefComponentsStats,
    EXDSDefNetsStats,
    EXDSDefParseStats,
    EXDSDesign,
    EXDSHierNode,
    EXDSHierTree,
    EXDSDensityGrid,
    EXDSInstance,
    EXDSInstanceStats,
    EXDSLayer,
    EXDSLefParseStats,
    EXDSNameMapper,
    EXDSNetBuildData,
    EXDSNetStats,
    EXDSPin,
    EXDSPinGeometry,
    EXDSPinTables,
    EXDSSubPartition,
    EXDSStack,
    EXDSViaCell,
    ds_build_hier_tree,
    ds_decide_partitions,
    ds_make_name_mapper,
    ds_merge_block_build,
    ds_merge_cell_lef,
    ds_merge_def_header,
    ds_merge_global_density,
    ds_parse_cell_lef,
    ds_parse_def_components,
    ds_parse_def_header,
    ds_parse_def_nets,
    ds_parse_tech_lef,
)

# 消息 id 注册（全局区直书，导入即生效）
from . import ds_register_msg  # noqa: F401

# 容器 + flow 入口 + 对外函数
from .ds_db import DesignDb, build_design_db
from .ds_functions import (
    load_block_names,
    load_design,
    load_name_mapper,
    load_design_stack,
    load_design_with,
    load_global_density,
)
