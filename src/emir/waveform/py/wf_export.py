"""waveform 模块 export 层——_fly_emir_waveform.so 符号唯一导入点。

全部 C++ 绑定符号（EXWF* 数据结构 + 执行函数）经本层导入；waveform 模
块内其他文件一律从本文件取符号，不直连 .so（同 design 的 ds_export.py
先例）。

数据结构面：
  EXWFFileChunkPlan / EXWFFileChunk /    T1 切块计划（temp：档位〔头部
  EXWFVarTable                           $timescale 解析产物〕+ 头段区
                                         间 + 块表 + 单文件 var 表——
                                         逐块任务共享）
  EXWFEntrySlice / EXWFStatsDelta /      T2 逐块分区分片（temp：分区 →
  EXWFFileStats                          波形/统计片段）+ 统计片段
  EXWFWaveformKey                        波形身份键（§7 两形态观察面）
  EXWFPartitionWaveform / EXWFWaveform   落库正式对象（PART_{xp}_{yp}.
  EXWFPartitionActivity /                WAVEFORM / PART_{xp}_{yp}.
  EXWFInstanceActivity / EXWFPinActivity ACTIVITY / "summary"）
  EXWFSummary / EXWFFileStats
  EXWFSettingsMeta / EXWFActivityOverride  settings 元数据 + 逐实体覆盖
                                           （§4；settings-only 物化入参）
  EXWFDesignContext                      design db 快照共享注入上下文
                                         （worker 端组装）
  EXWFFileBinding                        绑定描述（§2 三形态 +
                                         strip_prefix）
执行函数面（plan §6 任务链的 C++ 底座）：
  wf_plan_file_chunks                    T1 切块扫描（产 var 表 + 档位；
                                         缺失 $timescale 形态错在此抛
                                         错透出）
  wf_convert_chunk                       T2 逐块解析 + 名字换算 + 分区路由
  wf_merge_partition                     T3 每分区合并（波形去重 + 统计
                                         + 四级回落）
  wf_apply_overrides                     混合模式覆盖（裁定 IV-2：VCD +
                                         settings.activity.overrides 并
                                         存——合并产物统计容器上替换命
                                         中条目统计值，波形引用保留）
  wf_merge_summary                       T4 汇总（含 settings 元数据 + 覆
                                         盖条目计数）
  wf_materialize_activity                settings-only 统计容器物化
  wf_empty_partition_waveform            settings-only 空波形容器构造口
"""

from _fly_emir_waveform import (
    EXWFActivityOverride,
    EXWFDesignContext,
    EXWFEntrySlice,
    EXWFFileBinding,
    EXWFFileChunk,
    EXWFFileChunkPlan,
    EXWFFileStats,
    EXWFInstanceActivity,
    EXWFPartitionActivity,
    EXWFPartitionWaveform,
    EXWFPinActivity,
    EXWFSettingsMeta,
    EXWFSummary,
    EXWFStatsDelta,
    EXWFVarTable,
    EXWFWaveform,
    EXWFWaveformKey,
    wf_apply_overrides,
    wf_convert_chunk,
    wf_empty_partition_waveform,
    wf_materialize_activity,
    wf_merge_partition,
    wf_merge_summary,
    wf_plan_file_chunks,
)
