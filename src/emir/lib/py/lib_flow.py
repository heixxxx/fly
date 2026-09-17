"""build_lib_db flow 任务链（MapReduce 机制保留——用户裁定先例）。

三段式（dev-rules §3「建库 API 流程标准」）：入口 lib_db.build_lib_db
①轻量参数预处理（schema 校验 + 文件存在性 + 建库 + alpha_settings 写入
——不读文件内容）②提交唯一根任务 ③顶层提交 freeze。master 提交 O(1)
个任务，与输入规模无关（§20 编排判据）。

根任务 _lib_flow_task 体内目录（MapReduce 四阶段装配收编进任务体）：
  提交 MapReduceJob：每文件一分区（map 解析任务并行，liberty 形态嗅探
  在 processor 首个读取点——文件形态错任务失败透出，库不冻结）
  → full 单级全量合并（失败清单随合并汇聚）
  → finalizer 汇总透出 + LIBLibrary 写定
  → 框架中间键清理（__mr__{job_id}__* 键集运行时才知——job_id 运行时
     生成；清理责任单点 = MapReduce 框架 _mr_cleanup_task，随 job 提交、
     依赖 library 写定后调度，不经本 flow）

freeze _freeze_lib_task（build_lib_db 第③段顶层提交）：依赖固定标记
LIBLibrary（权威终态锚——finalizer 唯一写定者，freeze 不可能早于正式
产物）。lib flow 无自有临时键（全部中间键为框架键，清理责任见上）——
freeze 清理清单为空。

流程错误处理（2026-09-13 裁定范式，dev-rules §7.2）：单文件语法错误由
processor 兜底（空产物 + 失败清单），任务不 FAILED；finalizer 汇总透出
——部分失败 LIBR::0003（ERROR）+ 成功部分照常产出；全部失败 LIBR::0004
fatal（码 80 退出 + master 联动，及早止损）。文件形态错（非 liberty
误传）不在兜底范围——嗅探在 map 任务 raise，库不冻结（三段式裁定：
入口只查存在性，形态错由文件首个解析任务失败透出）。
"""

from fly import as_task
from fly.mapreduce import MapReduceJob

from .lib_db import LibDb
from .lib_utils import lib_parse_one


# ── flow 根任务（体内目录见模块 docstring）──────────────────────────

@as_task(inputs=lambda db, lib_paths: [])
def _lib_flow_task(db, lib_paths):
    """体内提交 MapReduceJob：map 解析任务族（每文件一任务，嗅探在
    processor 首个读取点）→ 全量合并 → finalizer（LIBLibrary 唯一写定
    者）。文件路径集随本任务参数传递，任务图形状由 job 运行时自定。"""
    mr = MapReduceJob(db, output_name=LibDb.LIBRARY_OBJ)
    mr.set_partitioner(lambda paths: list(paths))   # 每文件一分区
    mr.set_processor(lib_parse_one)
    mr.set_merger(_lib_merge_two, merge_type="full")
    mr.set_finalizer(_make_finalize(len(lib_paths)))
    mr.run(input_data=list(lib_paths))


# ── freeze（build_lib_db 第③段顶层提交）────────────────────────────

@as_task(inputs=lambda db: [db.get_full_name(LibDb.LIBRARY_OBJ)])
def _freeze_lib_task(db):
    """冻结 db。依赖 LIBLibrary 写定（finalizer 唯一写定者——freeze 不
    可能早于正式产物）。无清理清单：全部中间键为 MapReduce 框架键，由
    框架 cleanup 任务单点清理（见模块 docstring）。"""
    db.freeze()


def _lib_merge_two(a, b):
    """merger（二元）：把 b 并入 a，返回 a。

    产物形态 = (EXLIBLibrary, 失败清单)：解析失败的文件不产生库数据，但
    失败清单随合并树汇聚到最终产物（流程错误处理范式 2026-09-13——任务
    不 FAILED、依赖链保持满足，汇总阶段统一透出）。

    cell 冲突语义下沉 C++（EXLIBLibrary.merge → LIBLibrary::merge_from）：
    保留当前（首次出现）、抛弃后续重复，LIBR::0001 逐次提醒，不抛异常。
    """
    lib_a, failures_a = a
    lib_b, failures_b = b
    lib_a.merge(lib_b)
    return lib_a, failures_a + failures_b


def _make_finalize(total):
    """生成 finalizer（闭包捕获本 flow 文件总数，cloudpickle 随函数序列化）。

    汇总失败清单并透出，返回纯 LIBLibrary（正式对象形态不变）。
    二元处置（2026-09-13 裁定范式）：
    - 部分文件失败：LIBR::0003（ERROR）列出失败文件与原因，成功部分照常
      产出 LIBLibrary（依赖链满足，flow 完成）；
    - 全部文件失败：下游无法产出正确数据 → LIBR::0004 fatal（进程码 80
      退出 + master 联动结束整个 run，及早止损）。
    """
    def finalize(merged):
        lib, failures = merged
        if failures:
            detail = "; ".join(failures)
            if len(failures) >= total:
                from fly import fatal_message
                fatal_message("LIBR::0004", 0,
                              f"all {total} lib file(s) failed to parse, "
                              f"downstream data cannot be produced: {detail}")
                # fatal_message 不返回（_exit(80)）——下方 LIBR::0003 仅
                # 部分失败路径可达（review 2026-09-13：显式化防误读）
            from fly import message
            message("LIBR::0003", 0,
                    f"{len(failures)}/{total} lib file(s) failed to parse "
                    f"(skipped): {detail}")
        return lib
    return finalize
