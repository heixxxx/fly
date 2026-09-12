"""build_lib_db flow 的 MapReduce 四阶段装配。

多文件分布式解析 + LIBLibrary 整合（docs/emir-data-flow.md §4 裁定 14）：
每 .lib 文件一个分区（独立解析任务，天然分布式点），全量合并产出单一
LIBLibrary 容器写入 lib db；freeze 依赖 LIBLibrary 写完。流程入口
build_lib_db 在 lib_db.py（UserDoc + Schema 校验器 + @register_flow）。

流程错误处理（2026-09-13 裁定范式，dev-rules §7.2）：单文件语法错误由
processor 兜底（空产物 + 失败清单），任务不 FAILED；finalizer 汇总透出
——部分失败 LIBR::0003（ERROR）+ 成功部分照常产出；全部失败 LIBR::0004
fatal（码 80 退出 + master 联动，及早止损）。
"""

from fly import as_task
from fly.mapreduce import MapReduceJob

from .lib_db import LibDb
from .lib_utils import lib_parse_one


# ── 内部 task：freeze（依赖 LIBLibrary 写完，由 master 调度）──────────

@as_task(inputs=lambda db, dep_keys: list(dep_keys))
def _freeze_db_task(db, dep_keys):
    """冻结 db。inputs 依赖 LIBLibrary 对象写完后才被 master 调度。"""
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


def run_lib_flow(db, lib_paths):
    """MapReduce 分布式解析 + 整合 + freeze 提交（对象驱动异步推进）。"""
    mr = MapReduceJob(db, output_name=LibDb.LIBRARY_OBJ)
    mr.set_partitioner(lambda paths: list(paths))   # 每文件一分区
    mr.set_processor(lib_parse_one)
    mr.set_merger(_lib_merge_two, merge_type="full")
    mr.set_finalizer(_make_finalize(len(lib_paths)))
    mr.run(input_data=list(lib_paths))

    # freeze task（依赖 LIBLibrary 写完）
    _freeze_db_task(db, [mr.get_output_name()])
