"""design 模块内部工具——worker 侧解析任务。

供 ds_flow 的阶段链 task 引用；每 lef/def 文件一个独立任务（天然分布
式点）。不对外导出。
"""

import os

from .ds_export import (
    ds_parse_cell_lef,
    ds_parse_def_components,
    ds_parse_def_header,
    ds_parse_def_nets,
    ds_parse_tech_lef,
)


def _check_readable(path: str) -> None:
    """文件存在性守卫：缺失抛 FileNotFoundError（可 raise 场景，dev-rules
    §7 第一类；校验前置已拦截，此处为 worker 侧二次防御）。"""
    if not os.path.isfile(path):
        raise FileNotFoundError(f"design parse: input file missing: {path}")


def ds_parse_tech_one(path: str, stack):
    """S1：解析 tech lef → 填充 stack，返回 via 集合（含 VIARULE 展开的
    模板 via cell，㉚；stats 留在导出面，不进 task 数据通道）。"""
    _check_readable(path)
    return ds_parse_tech_lef(path, stack)[0]


def ds_parse_cell_one(path: str, stack):
    """S2：解析 cell lef → (临时 design, pin 几何, via 集合)。"""
    _check_readable(path)
    return ds_parse_cell_lef(path, stack)[:3]


def ds_parse_def_one(path: str, stack):
    """S4+S4b：DEF 头扫描 → (block cell 集合, port pin 名序列, port 几何,
    via 集合)。port 名与 block cell 的 pins_ 下标对齐（R7 ㊱ DSPin 不存
    name——经汇总传进全局 pin hasher）。"""
    _check_readable(path)
    return ds_parse_def_header(path, stack)[:4]


def ds_parse_def_components_one(path: str, stack, design, block_data,
                                bin_dbu: int):
    """COMPONENTS 解析（实例侧）：责任链 ∥ 网名扫描（同一遍 DEF 读取）
    ——就地填充 block_data（实例表/密度通道/统计），返回 stats。bin_dbu
    = 密度采样格边长（全局 DBU；alpha 配置换算，见 ds_flow）。网名空间
    在返回的 stats 侧（skipped 计数）；实例名经 DSInstanceNameHasher 双向
    登记（R7）。"""
    _check_readable(path)
    return ds_parse_def_components(path, stack, design, block_data, bin_dbu)


def ds_parse_def_nets_one(path: str, stack, design, block_data, net_data,
                          bin_dbu: int, net_batch_size: int):
    """网内容解析：责任链 ∥ 分批多阶段（裁定 ③/⑨）——就地填充 net_data
    （连接表/几何表/via instance 表/网侧密度通道/统计），返回 stats。
    local net id 与 block_data 的网名空间对齐；net_batch_size = 批界网数
   （alpha 键 net_batch_size）；bin_dbu = 密度采样格边长（全局 DBU）。"""
    _check_readable(path)
    return ds_parse_def_nets(path, stack, design, block_data, net_data,
                             bin_dbu, net_batch_size)
