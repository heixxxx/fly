"""流式写在异步写之后（同 db）条目寻址回归——storage_test 全文件 corruption 案发根修.

根因（2026-09-04 根修，database.cpp open_write_stream/open_temp_write_stream）：
  begin_incremental 若在任务线程现场执行，先前入队的异步写单元（commit_write）
  尚未落盘，incremental_offset_ 快照踩在陈旧 current_file_size_ 上；finish 单元
  再按 WBQ FIFO 在那些单元之后执行——条目变成 (0, 全文件尺寸)，读侧取错区间，
  trailer 恰在末尾元数据合法 → unpickle 误报 [FATAL-DATA-CORRUPTION]。
  根修：BEGIN 改为与块/finish 同 FIFO 的同步 WBQ 单元（promise 等待）。

本测试以 50 次迭代确定性放大该交错（修复前 1-2 次内即现）。

运行方式：QA 子阶段脚本（qa/unit/test_stream_after_async_write.pyt 调度，fly
进程内执行）。原为裸 pytest 进程手工运行的函数体（2026-09-04 入库时从未被
任何自动化流程真正执行——pytest 风格只定义不调用，fly 按扁平脚本 exec 后
直接以 0 退出）；2026-11-25 扁平化真正执行。裸进程专用的 sys.path 注入与
runtime._mode = "worker" 均已去除：fly 启动时自动配好模块路径；master 模式
与裸进程 worker 模式的写路径同为 open_write_stream → finish_and_commit 唯一
路径（database.py write_object），交错场景不变。
"""
import gc
import shutil

from _fly_log import INFO

from _fly_storage import (ex_stg_get_data_service, ex_stg_get_storage_manager,
                          EXStgIndexEntry)
from fly import open_db
from stream_after_async_data import PythonTaskData
from test import qa_tmp, record

ITERATIONS = 50


def _drain():
    ex_stg_get_data_service().drain_write_back()


passed_iters = 0
failed_iters = []

for i in range(ITERATIONS):
    # 数据目录与 case 日志同目录共生共死（qa_tmp：runqa 下落 FLY_CASE_LOG_DIR，
    # 手动直跑落 .work/tmp——禁止系统 /tmp）；开头清残留保证迭代幂等。
    d = qa_tmp(f"fly_test_stream_after_async_{i}")
    shutil.rmtree(d, ignore_errors=True)
    try:
        db = open_db(d)
        # typed 异步写（commit_write 入 WBQ 后立即返回）+ 紧邻流式写 open——
        # 根修前 begin 快照落在异步单元落盘前的文件尺寸上。
        entry = EXStgIndexEntry("cpp/data", "", i, 0, False, 0)
        db.write_object("cpp/data", entry)
        py_obj = PythonTaskData(i, "py_data")
        db.write_object("py/data", py_obj)
        _drain()
        cpp_val = db.read_object("cpp/data")
        iter_cpp_ok = cpp_val is not None
        # 原断言：assert db.read_object("cpp/data") is not None, f"iter {i}: cpp read"
        py_result = db.read_object("py/data")
        iter_py_ok = (py_result is not None
                      and py_result.name == "py_data" and py_result.value == i)
        # 原断言：assert py_result.name == "py_data" and py_result.value == i, \
        if iter_cpp_ok and iter_py_ok:
            passed_iters += 1
        else:
            failed_iters.append(i)
        db = None
    finally:
        # 每迭代单例复位（同 storage_test fixture 三步收尾）：① gc 收网析构
        # 测试局部 Database；② StorageManager 容器清空（master 路径创建的 db
        # 登记在 MasterAgent 权威 map，此处置为空操作）；③ DataService 键控
        # 状态全清，WBQ 停止后由下次写入惰性重启。
        gc.collect()
        ex_stg_get_storage_manager().reset()
        ex_stg_get_data_service().reset_state()

# 原断言：assert db.read_object("cpp/data") is not None, f"iter {i}: cpp read"
# 原断言：assert py_result.name == "py_data" and py_result.value == i, \
record("iterations/all_passed", passed_iters == ITERATIONS, detail=failed_iters)
record("iterations/completed", passed_iters, detail=failed_iters)
INFO(f"[DONE] test_stream_after_async_write: {passed_iters}/{ITERATIONS} iterations passed")
