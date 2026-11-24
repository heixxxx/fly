"""spef 模块对外函数层——供其他模块读取 spef db 的汇总、分区枚举与分
区网负载对象（plan §3.2 读库 API 面；建库入口见 sp_db.build_spef_db）。

不含解析/换算（那是 spef db 的内部流程，C++ 执行函数仅经 sp_export
供 sp_flow/测试使用，不对外暴露）。首期消费者 = power db 的 net load。

依赖声明规范（DEVELOPMENT_GUIDELINES Section 17）：本模块全部 API 内部
read_object，一律 @wait_obj 包装声明自身数据依赖（inputs 用 lambda 声明
防依赖漂移）。design db 的依赖经数据库链 find_db(role="design") 解析
（dev-rules §3 间接前置经数据库链）；调用规范同 design/timing/waveform
先例：
  - 本地直调（脚本/交互）：数据未就绪时 API 自动轮询等待（wait_obj 语
    义；确认无法产出时 RuntimeError 兜底）；
  - task 内调用：上层 as_task 的 inputs 必须以 `api.deps(db, ...) + [自
    身其他依赖]` 传播数据依赖，函数体用 `run_direct(api, db, ...)` 直跑。
"""

from fly import wait_obj

from .sp_db import SpefDb


@wait_obj(inputs=lambda db: [db.get_full_name(SpefDb.SUMMARY_OBJ)])
def load_spef_summary(db):
    """读取汇总（EXSPSummary：逐来源文件统计〔解析网数/命中/各类跳过
    与破损计数/切块数〕+ 覆盖率统计 + 缩减网/行首值不一致/同作用域重
    合观测 + 内部单位基准声明）。

    调用规范见模块 docstring：task 内调用须
    `load_spef_summary.deps(db)` 传播依赖 +
    `run_direct(load_spef_summary, db)` 直跑。
    """
    return db.read_object(SpefDb.SUMMARY_OBJ)


def _design_deps(db):
    """design db 的依赖锚名列表（数据库链解析；无 design db 前驱返回
    空列表——依赖声明退化为 spef 自身锚，读时显式报错）。"""
    design_db = db.find_db(role="design")
    if design_db is None:
        return []
    from emir.design import DesignDb
    return [design_db.get_full_name(DesignDb.DESIGN_OBJ),
            design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ)]


@wait_obj(inputs=lambda db: (
    [db.get_full_name(SpefDb.SUMMARY_OBJ)]
    + _design_deps(db)))
def iter_spef_partition(db):
    """枚举全部分区 (xp, yp) 网格坐标（行主序产出序；design db 分区表
    ——spef 分区对象与其同坐标系。空分区亦产出空对象——下游逐分区并
    行消费的枚举口）。

    调用规范见模块 docstring：task 内调用须
    `iter_spef_partition.deps(db)` 传播依赖 +
    `run_direct(iter_spef_partition, db)` 直跑。
    """
    design_db = db.find_db(role="design")
    if design_db is None:
        raise RuntimeError(
            "spef db: design db predecessor not found in db chain")
    from emir.design import DesignDb
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    return [(design.partition_at(i).xp, design.partition_at(i).yp)
            for i in range(design.partition_count)]


@wait_obj(inputs=lambda db, xp, yp: (
    [db.get_full_name(SpefDb.partition_obj_name(xp, yp))]))
def load_partition_net_load(db, xp: int, yp: int):
    """读取一个分区的网负载容器（EXSPPartitionNetLoad：本区逐网 {行首
    自报总电容、对地合计、耦合合计、贡献来源位图、缩减形态标记}——
    power db 的输入；条目按 net id 升序）。

    调用规范见模块 docstring：task 内调用须
    `load_partition_net_load.deps(db, xp, yp)` 传播依赖 +
    `run_direct(load_partition_net_load, db, xp, yp)` 直跑。
    """
    return db.read_object(SpefDb.partition_obj_name(xp, yp))
