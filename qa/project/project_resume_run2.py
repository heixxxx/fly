"""run2：load_project(resume=True) 断点重跑——已完成对象直接 ready（不重算），
bin 里的未完成 task 重投执行，最终全部对象就绪且值正确。"""
import os
import time

from _fly_log import INFO
from fly import open_project, wait_tasks
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2)

# 断点现场铁证：bin 落归属 db 目录（Task db 归属规则，非 {log_dir} 也非 project 根）。
bin_path = os.path.join(PROJ_PATH, "workdb", "failed_tasks.bin")
assert os.path.isfile(bin_path), f"failed_tasks.bin should live in the owner db dir: {bin_path}"

proj = open_project(PROJ_PATH)
db = proj.get_db("workdb")

# 已完成对象（run1 中 COMPLETED）应立即可读（load_project 恢复索引）。
# 未完成对象由 resume 重投的 task 补齐。
proj.resume()

ok = wait_tasks(timeout=120)
assert ok, "all resumed tasks should complete"

for i in range(6):
    v = db.read_object(f"obj_{i}")
    assert v == i * 10, f"obj_{i} expected {i * 10}, got {v}"

# resume 消费后 bin 应被清（全部 task 成功 → remove_persisted_task 清空删文件）。
t0 = time.time()
while os.path.isfile(bin_path) and time.time() - t0 < 30:
    time.sleep(0.5)
assert not os.path.isfile(bin_path), "bin should be emptied after successful resume"

INFO("[PASS] project_resume_run2: task-level resume from project-local failed_tasks.bin")
master.stop()
