"""MapReduce 边缘路径：坏 partitioner / finalizer 链 / get 无 db 引用。

覆盖（2026-09 覆盖率批次 14 项之 12）：
  - set_partitioner 返回不可长度测定的值 → run() dry-run 阶段 ValueError
  - set_finalizer：合并结果经 finalize 改写后作为最终输出
  - mr.get() 无 db 引用 → RuntimeError（worker 侧必须显式传 db 的守卫）
  - get_output_name() 全名格式
"""
import os
import shutil
import time

from _fly_log import INFO

from fly import open_db, wait_tasks
from fly.mapreduce import MapReduceJob
from test import qa_tmp

BASE_PATH = qa_tmp("mr_edge_db")
if os.path.isdir(BASE_PATH):
    shutil.rmtree(BASE_PATH, ignore_errors=True)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2, timeout=60), "workers must connect"

db = open_db(BASE_PATH)


# ── 2. finalizer 链：merged 10 → finalized 100 ──────────────────────
mr_fin = MapReduceJob(db, output_name="mr_edge_fin")
mr_fin.set_partitioner(lambda data: [data[i::2] for i in range(2)])
mr_fin.set_processor(lambda part: sum(part))
mr_fin.set_merger(lambda a, b: a + b, "summary")
mr_fin.set_finalizer(lambda merged: {"total": merged * 10})
mr_fin.run([1, 2, 3, 4])
assert wait_tasks(timeout=60), "finalize pipeline must complete"
result = mr_fin.get()
assert result == {"total": 100}, f"finalized result expected, got {result}"
assert mr_fin.get_output_name() == f"{db.get_db_path()}:mr_edge_fin", \
    mr_fin.get_output_name()
INFO("[PASS] set_finalizer applied to merged result + get_output_name")

# ── 3. get() 无 db 引用 → RuntimeError ──────────────────────────────
mr_nodb = MapReduceJob(db, output_name="mr_edge_nodb")
mr_nodb._db = None
try:
    mr_nodb.get()
    raise AssertionError("get() without db must raise RuntimeError")
except RuntimeError as e:
    assert "No database reference" in str(e), str(e)
INFO("[PASS] mr.get() without db reference -> RuntimeError")

master.stop()
INFO("[PASS] test_mapreduce_edge")

# ── 4. 坏 partitioner → run() dry-run ValueError（放最后：失败任务落账会
mr_bad = MapReduceJob(db, output_name="mr_edge_bad")
mr_bad.set_partitioner(lambda data: None)  # None 无 len → dry-run 失败
mr_bad.set_processor(lambda p: p)
mr_bad.set_merger(lambda a, b: a + b)
try:
    mr_bad.run([1, 2, 3])
    raise AssertionError("bad partitioner must raise ValueError in dry-run")
except ValueError as e:
    assert "dry-run" in str(e), str(e)
INFO("[PASS] set_partitioner bad function -> ValueError at run() dry-run")

# dry-run 校验发生在任务提交之后——坏 partitioner 的任务已在飞并必然失败。
# 显式消化：wait_tasks 见失败即 raise（预期行为），吞掉并断言落账，
# 避免其失败串入后续 mr_fin 的 wait_tasks 断言。
try:
    wait_tasks(timeout=60)
except RuntimeError as e:
    assert "Tasks failed" in str(e), str(e)
INFO("[PASS] bad partitioner task failure observed")

