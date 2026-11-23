"""waveform 模块对外函数层——供其他模块读取 waveform db 的汇总、分区
枚举与双容器对象（plan §3.2 读库 API 面；建库入口见
wf_db.build_waveform_db）。

不含解析/换算（那是 waveform db 的内部流程，C++ 执行函数仅经 wf_export
供 wf_flow/测试使用，不对外暴露）。

依赖声明规范（DEVELOPMENT_GUIDELINES Section 17）：本模块全部 API 内部
read_object，一律 @wait_obj 包装声明自身数据依赖（inputs 用 lambda 声明
防依赖漂移）。design db 的依赖经数据库链 find_db(role="design") 解析
（dev-rules §3 间接前置经数据库链）；调用规范同 design/timing 先例：
  - 本地直调（脚本/交互）：数据未就绪时 API 自动轮询等待（wait_obj 语
    义；确认无法产出时 RuntimeError 兜底）；
  - task 内调用：上层 as_task 的 inputs 必须以 `api.deps(db, ...) + [自
    身其他依赖]` 传播数据依赖，函数体用 `run_direct(api, db, ...)` 直跑。
"""

from fly import wait_obj

from .wf_db import WaveformDb


@wait_obj(inputs=lambda db: [db.get_full_name(WaveformDb.SUMMARY_OBJ)])
def load_waveform_summary(db):
    """读取汇总（EXWFSummary：全局 DURATION〔率/概率归一化唯一基准〕+
    x/z 口径与 settings 默认值元数据（消费侧展开依据）+ 覆盖率/弃收/
    兜底计数 + 波形数/多文件覆盖与时间轴观测 + 逐来源文件统计）。

    调用规范见模块 docstring：task 内调用须
    `load_waveform_summary.deps(db)` 传播依赖 +
    `run_direct(load_waveform_summary, db)` 直跑。
    """
    return db.read_object(WaveformDb.SUMMARY_OBJ)


def _design_deps(db):
    """design db 的依赖锚名列表（数据库链解析；无 design db 前驱返回
    空列表——依赖声明退化为 waveform 自身锚，读时显式报错）。"""
    design_db = db.find_db(role="design")
    if design_db is None:
        return []
    from emir.design import DesignDb
    return [design_db.get_full_name(DesignDb.DESIGN_OBJ),
            design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ)]


@wait_obj(inputs=lambda db: (
    [db.get_full_name(WaveformDb.SUMMARY_OBJ)]
    + _design_deps(db)))
def iter_waveform_partition(db):
    """枚举全部分区 (xp, yp) 网格坐标（行主序产出序；DESIGN_OBJ 锚——
    读 design db 分区表，waveform 分区对象与其同坐标系。GLOBAL_DENSITY
    锚 = S8 分区表填充证明，同 design 侧 iter 口径）。空分区亦产出空对
    象（双容器照常产出）——下游逐分区并行消费的枚举口。

    调用规范见模块 docstring：task 内调用须
    `iter_waveform_partition.deps(db)` 传播依赖 +
    `run_direct(iter_waveform_partition, db)` 直跑。
    """
    design_db = db.find_db(role="design")
    if design_db is None:
        raise RuntimeError(
            "waveform db: design db predecessor not found in db chain")
    from emir.design import DesignDb
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    return [(design.partition_at(i).xp, design.partition_at(i).yp)
            for i in range(design.partition_count)]


@wait_obj(inputs=lambda db, xp, yp: (
    [db.get_full_name(WaveformDb.activity_obj_name(xp, yp))]))
def load_partition_activity(db, xp: int, yp: int):
    """读取一个分区的统计容器（EXWFPartitionActivity：本区逐实例逐引脚
    {波形 id 引用 + 四态时长 + rise/fall 计数}——power db 主输入；哨兵
    waveform_id = 无波形）。

    调用规范见模块 docstring：task 内调用须
    `load_partition_activity.deps(db, xp, yp)` 传播依赖 +
    `run_direct(load_partition_activity, db, xp, yp)` 直跑。
    """
    return db.read_object(WaveformDb.activity_obj_name(xp, yp))


@wait_obj(inputs=lambda db, xp, yp: (
    [db.get_full_name(WaveformDb.partition_obj_name(xp, yp))]))
def load_partition_waveform(db, xp: int, yp: int):
    """读取一个分区的波形容器（EXWFPartitionWaveform：分区局部波形 id
    → 事件序列；演进期动态电流合成输入）。统计消费者无需加载——双对象
    独立加载（裁定：双容器）；settings-only 模式恒空容器。

    调用规范见模块 docstring：task 内调用须
    `load_partition_waveform.deps(db, xp, yp)` 传播依赖 +
    `run_direct(load_partition_waveform, db, xp, yp)` 直跑。
    """
    return db.read_object(WaveformDb.partition_obj_name(xp, yp))
