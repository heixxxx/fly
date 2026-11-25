"""run2：load_project(resume=True) 断点重跑——已完成对象直接 ready（不重算），
bin 里的未完成 task 重投执行，最终全部对象就绪且值正确。"""
import os

from _fly_log import INFO
from fly import open_project, wait_tasks
from test import record
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

# 断点现场铁证：bin 落归属 db 目录（Task db 归属规则，非 {log_dir} 也非 project 根）。
# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前（原脚本在 launch worker 之后做的
# 就是这同一句检查，现挪到最前）。
bin_path = os.path.join(PROJ_PATH, "workdb", "failed_tasks.bin")
bin_exists = os.path.isfile(bin_path)
# 原断言：assert os.path.isfile(bin_path), \
record("resume_run2/bin_before_resume", bin_exists, detail=bin_path)
# 非守卫：bin_path 之后只被 wait_until 的 isfile 检查消费，缺失时返回 False，不解引用

master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2)

proj = open_project(PROJ_PATH)
db = proj.get_db("workdb")

# 已完成对象（run1 中 COMPLETED）应立即可读（load_project 恢复索引）。
# 未完成对象由 resume 重投的 task 补齐。
proj.resume()

ok = wait_tasks(timeout=120)
assert ok, "all resumed tasks should complete"

for i in range(6):
    v = db.read_object(f"obj_{i}")
    # 原断言：assert v == i * 10, f"obj_{i} expected {i * 10}, got {v}"
    record(f"resume_run2/obj_{i}", v)

# resume 消费后 bin 应被清（全部 task 成功 → remove_persisted_task 清空删文件）。
from test import wait_until
assert wait_until(lambda: not os.path.isfile(bin_path), timeout=30), \
    "bin should be emptied after successful resume"

INFO("[DONE] project_resume_run2: task-level resume from project-local failed_tasks.bin")
master.stop()
