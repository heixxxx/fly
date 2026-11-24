"""spef 模块 export 层——_fly_emir_spef.so 符号唯一导入点。

全部 C++ 绑定符号（EXSP* 数据结构 + 执行函数）经本层导入；spef 模块内
其他文件一律从本文件取符号，不直连 .so（同 waveform 的 wf_export.py
先例）。

数据结构面：
  EXSPFileHeader / EXSPNet /             解析边界（temp：文件头〔单位换
  EXSPChunkResult                        算系数 + 分隔符 + 名字映射表，逐
                                         块任务共享〕+ 单网段聚合量 + 块
                                         解析统计计数）
  EXSPFileChunk / EXSPFileChunkPlan      T1 切块计划（temp：头段终点 +
                                         块表——一次扫描随计划产出文件头）
  EXSPEntrySlice / EXSPStatsDelta /      T2 逐块分区分片（temp：分区 →
  EXSPFileStats / EXSPNetEntry /         网负载条目）+ 统计载体（字段可
  EXSPPartitionSlice                     写——流程组装面与测试构造）
  EXSPNetLoad / EXSPPartitionNetLoad     落库正式对象（PART_{xp}_{yp}.
                                         NET_LOAD）
  EXSPMergeStats / EXSPSummary           T3 合并统计 + T4 汇总正式对象
                                         （"summary"）
  EXSPDesignContext                      design db 快照共享注入上下文
                                         （worker 端组装；含分区实例连
                                         接对象注入口——覆盖率材料）
  EXSPFileBinding                        绑定描述（三形态 + strip_prefix）
执行函数面（plan §6 任务链的 C++ 底座）：
  sp_plan_file_chunks                    T1 切块扫描（返回 (计划, 文件头)
                                         二元组；文件级形态错在此抛错透
                                         出）
  sp_read_byte_range                     字节区间读取（块文本供给）
  sp_parse_header_text / sp_parse_header_file    头段解析
  sp_parse_chunk_text                    逐块解析（段级破损跳过 + 计数）
  sp_parse_spef_text / sp_parse_spef_file        整文件解析（二元组）
  sp_convert_chunk                       T2 逐块解析 + 名字换算 + 分区路由
  sp_merge_partition                     T3 每分区合并（返回 (对象, 统计)
                                         二元组）
  sp_merge_summary                       T4 汇总
"""

from _fly_emir_spef import (
    EXSPChunkResult,
    EXSPDesignContext,
    EXSPEntrySlice,
    EXSPFileBinding,
    EXSPFileChunk,
    EXSPFileChunkPlan,
    EXSPFileHeader,
    EXSPFileStats,
    EXSPMergeStats,
    EXSPNet,
    EXSPNetEntry,
    EXSPNetLoad,
    EXSPPartitionNetLoad,
    EXSPPartitionSlice,
    EXSPStatsDelta,
    EXSPSummary,
    sp_convert_chunk,
    sp_merge_partition,
    sp_merge_summary,
    sp_parse_chunk_text,
    sp_parse_header_file,
    sp_parse_header_text,
    sp_parse_spef_file,
    sp_parse_spef_text,
    sp_plan_file_chunks,
    sp_read_byte_range,
)
