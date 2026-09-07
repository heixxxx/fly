"""build_lib_db flow 的 MapReduce 四阶段装配。

多文件分布式解析 + LIBLibrary 整合（docs/emir-data-flow.md §4 裁定 14）：
每 .lib 文件一个分区（独立解析任务，天然分布式点），全量合并产出单一
LIBLibrary 容器写入 lib db；freeze 依赖 LIBLibrary 写完。流程入口
build_lib_db 在 lib_db.py（UserDoc + Schema 校验器 + @register_flow）。
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

    cell 冲突语义下沉 C++（EXLIBLibrary.merge → LIBLibrary::merge_from）：
    保留当前（首次出现）、抛弃后续重复，LIBR::0001 逐次提醒，不抛异常。
    """
    a.merge(b)
    return a


def run_lib_flow(db, lib_paths):
    """MapReduce 分布式解析 + 整合 + freeze 提交（对象驱动异步推进）。"""
    mr = MapReduceJob(db, output_name=LibDb.LIBRARY_OBJ)
    mr.set_partitioner(lambda paths: list(paths))   # 每文件一分区
    mr.set_processor(lib_parse_one)
    mr.set_merger(_lib_merge_two, merge_type="full")
    mr.run(input_data=list(lib_paths))

    # freeze task（依赖 LIBLibrary 写完）
    _freeze_db_task(db, [mr.get_output_name()])
