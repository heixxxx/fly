"""流式写在异步写之后（同 db）条目寻址回归——storage_test 全文件 corruption 案发根修.

根因（2026-09-04 根修，database.cpp open_write_stream/open_temp_write_stream）：
  begin_incremental 若在任务线程现场执行，先前入队的异步写单元（commit_write）
  尚未落盘，incremental_offset_ 快照踩在陈旧 current_file_size_ 上；finish 单元
  再按 WBQ FIFO 在那些单元之后执行——条目变成 (0, 全文件尺寸)，读侧取错区间，
  trailer 恰在末尾元数据合法 → unpickle 误报 [FATAL-DATA-CORRUPTION]。
  根修：BEGIN 改为与块/finish 同 FIFO 的同步 WBQ 单元（promise 等待）。

本测试以 50 次迭代确定性放大该交错（修复前 1-2 次内即现）。
"""
import os
import shutil
import sys
import tempfile

_bazel_bin = os.path.join(os.path.dirname(__file__), '..', '..', 'bazel-bin', 'src')
for _subpath in ['storage/export', 'core/export', 'log/export', 'agent/export',
                 'network/export', 'task/export', 'message/export']:
    _full = os.path.join(_bazel_bin, _subpath)
    if os.path.exists(_full):
        sys.path.insert(0, _full)
_fly_src = os.path.join(os.path.dirname(__file__), '..', '..', 'src')
if os.path.exists(_fly_src):
    sys.path.insert(0, _fly_src)

import fly.runtime as _rt
_rt._mode = "worker"

from _fly_storage import (ex_stg_get_data_service, ex_stg_get_storage_manager,
                          EXStgIndexEntry)
from fly import open_db

ITERATIONS = 50


class PythonTaskData:
    def __init__(self, value=0, name="", tags=None):
        self.value = value
        self.name = name
        self.tags = tags or []


def _drain():
    ex_stg_get_data_service().drain_write_back()


def test_stream_write_after_async_write_same_db():
    for i in range(ITERATIONS):
        d = tempfile.mkdtemp(prefix='fly_test_stream_after_async_')
        try:
            db = open_db(d)
            # typed 异步写（commit_write 入 WBQ 后立即返回）+ 紧邻流式写 open——
            # 根修前 begin 快照落在异步单元落盘前的文件尺寸上。
            entry = EXStgIndexEntry("cpp/data", "", i, 0, False, 0)
            db.write_object("cpp/data", entry)
            py_obj = PythonTaskData(i, "py_data")
            db.write_object("py/data", py_obj)
            _drain()
            assert db.read_object("cpp/data") is not None, f"iter {i}: cpp read"
            py_result = db.read_object("py/data")
            assert py_result.name == "py_data" and py_result.value == i, \
                f"iter {i}: py mismatch {py_result}"
            db = None
        finally:
            import gc
            gc.collect()
            ex_stg_get_storage_manager().reset()
            ex_stg_get_data_service().reset_state()
            shutil.rmtree(d, ignore_errors=True)
