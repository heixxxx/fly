"""timing 模块对外函数层——供其他模块读取 timing db 的时钟表、汇总与
分区时序对象（plan §3.2 读库 API 面；建库入口见 tm_db.build_timing_db）。

不含解析/换算（那是 timing db 的内部流程，C++ 执行函数仅经 tm_export
供 tm_flow/测试使用，不对外暴露）。

依赖声明规范（DEVELOPMENT_GUIDELINES Section 17）：本模块全部 API 内部
read_object，一律 @wait_obj 包装声明自身数据依赖（inputs 用 lambda 声明
防依赖漂移）。design db 的依赖经数据库链 find_db(role="design") 解析
（dev-rules §3 间接前置经数据库链）；调用规范同 design 先例：
  - 本地直调（脚本/交互）：数据未就绪时 API 自动轮询等待（wait_obj 语
    义；确认无法产出时 RuntimeError 兜底）；
  - task 内调用：上层 as_task 的 inputs 必须以 `api.deps(db) + [自身其他
    依赖]` 传播数据依赖，函数体用 `run_direct(api, db)` 剥离本地等待直跑。
"""

from fly import wait_obj

from .tm_db import TimingDb


@wait_obj(inputs=lambda db: [db.get_full_name(TimingDb.CLOCKS_OBJ)])
def load_timing_clocks(db):
    """读取时钟表（EXTMClockTable：时钟 id → {名, 周期, 上升/下降沿时
    刻}；周期 → 频率 = 1/period 由消费侧推导——Innovus 格式无实例级频率
    字段，裁定 3）。

    调用规范见模块 docstring：task 内调用须
    `load_timing_clocks.deps(db)` 传播依赖 + `run_direct(load_timing_clocks,
    db)` 直跑。
    """
    return db.read_object(TimingDb.CLOCKS_OBJ)


@wait_obj(inputs=lambda db: [db.get_full_name(TimingDb.SUMMARY_OBJ)])
def load_timing_summary(db):
    """读取汇总（EXTMSummary：覆盖率（逐文件条目数/命中数）/弃收计数
    （源电阻/富余量）/兜底计数（未匹配实例/引脚/网、悬空网、未放置实例、
    strip_prefix 未命中）/跨文件冲突数/C-D 观测/单位与来源文件清单）。

    调用规范见模块 docstring：task 内调用须
    `load_timing_summary.deps(db)` 传播依赖 +
    `run_direct(load_timing_summary, db)` 直跑。
    """
    return db.read_object(TimingDb.SUMMARY_OBJ)


def _design_deps(db):
    """design db 的依赖锚名列表（数据库链解析；无 design db 前驱返回
    空列表——依赖声明退化为 timing 自身锚，读时显式报错）。"""
    design_db = db.find_db(role="design")
    if design_db is None:
        return []
    from emir.design import DesignDb
    return [design_db.get_full_name(DesignDb.DESIGN_OBJ),
            design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ)]


@wait_obj(inputs=lambda db: (
    [db.get_full_name(TimingDb.CLOCKS_OBJ)]
    + _design_deps(db)))
def iter_timing_partition(db):
    """枚举有分区归属的 (xp, yp) 网格坐标（行主序产出序；DESIGN_OBJ 锚
    ——读 design db 分区表，timing 分区对象与其同坐标系）。GLOBAL_DENSITY
    锚 = S8 分区表填充证明（同 design 侧 iter 口径）。

    调用规范见模块 docstring：task 内调用须
    `iter_timing_partition.deps(db)` 传播依赖 +
    `run_direct(iter_timing_partition, db)` 直跑。
    """
    design_db = db.find_db(role="design")
    if design_db is None:
        raise RuntimeError(
            "timing db: design db predecessor not found in db chain")
    from emir.design import DesignDb
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    return [(design.partition_at(i).xp, design.partition_at(i).yp)
            for i in range(design.partition_count)]


@wait_obj(inputs=lambda db, xp, yp: (
    [db.get_full_name(TimingDb.partition_obj_name(xp, yp))]))
def load_partition_timing(db, xp: int, yp: int):
    """读取一个分区的时序正式对象（EXTMPartitionTiming：本区逐实例时序
    ——实例 global id → 时钟归属 + 引脚稀疏表；primary 恰一）。

    调用规范见模块 docstring：task 内调用须
    `load_partition_timing.deps(db, xp, yp)` 传播依赖 +
    `run_direct(load_partition_timing, db, xp, yp)` 直跑。
    """
    return db.read_object(TimingDb.partition_obj_name(xp, yp))
