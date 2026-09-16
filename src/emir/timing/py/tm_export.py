"""timing 模块 export 层——_fly_emir_timing.so 符号唯一导入点。

全部 C++ 绑定符号（EX TM* 数据结构 + T1/T2/T3/T4 执行函数）经本层导入；
timing 模块内其他文件一律从本文件取符号，不直连 .so（同 design 的
ds_export.py 先例）。

数据结构面：
  EXTMChunkPlan / EXTMFileChunkPlan      T1 切块计划（temp 对象：文件 →
                                         字节区间块表 + §2 绑定描述）
  EXTMEntrySlice / EXTMStatsDelta /      T2 逐块分区分片（temp：分区 →
  EXTMFileStats                          实例时序片段 + 统计片段 + 块时钟
                                         表）
  EXTMPartitionTiming /                  落库正式对象（PART_{xp}_{yp}.TIMING
  EXTMInstanceTiming / EXTMPinTiming     / "clocks" / "summary"）
  EXTMClockTable / EXTMClockEntry
  EXTMSummary
  EXTMDesignContext                      design db 快照共享注入上下文
                                         （worker 端组装，EXTMClock 也在此层）
  EXTMFileBinding                        绑定描述（§2 三形态 + strip_prefix）
执行函数面（plan §6 任务链的 C++ 底座）：
  tm_plan_file_chunks                    T1 切块扫描
  tm_convert_chunk                       T2 逐块解析 + 名字换算 + 分区路由
  tm_merge_partition                     T3 每分区合并
  tm_merge_summary / tm_merge_clocks     T4 汇总
"""

from _fly_emir_timing import (
    EXTMChunkPlan,
    EXTMClockEntry,
    EXTMClockTable,
    EXTMDesignContext,
    EXTMEntrySlice,
    EXTMFileBinding,
    EXTMFileChunkPlan,
    EXTMFileStats,
    EXTMInstanceTiming,
    EXTMPartitionTiming,
    EXTMPinTiming,
    EXTMSummary,
    EXTMStatsDelta,
    tm_convert_chunk,
    tm_merge_clocks,
    tm_merge_partition,
    tm_merge_summary,
    tm_plan_file_chunks,
)

# TMClock（解析边界时钟定义）也经本层导出（T4 时钟表合并的块级来源）
from _fly_emir_timing import EXTMClock
