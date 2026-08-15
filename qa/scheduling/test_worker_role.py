"""E2E test: worker role（hybrid / storage_only，静态身份，调度不感知）。

验证（用户确认语义）：
  1. launch config 的 role key 真实生效（此前被静默忽略——F3 遗留）；
  2. storage_only worker 在线（连接/数据面成员）但不出现在调度候选
    （idle 列表），计算 task 全部由 hybrid 执行；
  3. storage_only worker 上没有任何计算 task 执行痕迹（本地日志）。
"""
from _fly_log import INFO
import os
import time

from test import write_data
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "role_db")
from fly.runtime import get_agent


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def cleanup():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([
    {},                       # 默认 hybrid
    {"role": "storage_only"},  # 存储 worker
])
assert master.wait_workers_registered(timeout=60), "both workers should register"

# 两者都在线（连接表 = 数据面成员）。
wait_for(lambda: master.worker_count >= 2, timeout=30)
INFO("[ROLE] both workers online")

# storage_only（worker_id=2）不在调度候选。
wait_for(lambda: master._agent.get_idle_workers() == [1], timeout=30)
idle = master._agent.get_idle_workers()
assert idle == [1], f"idle candidates must be hybrid-only, got {idle}"
INFO("[ROLE] idle candidates exclude storage_only")

# 提交计算 task：全部应由 hybrid（worker 1）执行完成。
db = open_db(DB_PATH)
for i in range(5):
    write_data(db, f"role/obj{i}", i)
assert wait_for(lambda: len(master.completed_tasks) >= 5), "5 compute tasks should complete"
INFO("[ROLE] all compute tasks completed on hybrid worker")

# 先停止（worker 的 INFO 日志带缓冲，进程退出才 flush——运行中读文件会
# 漏最新行；P3-19 同机制），再读 worker2 本地日志做最终断言。
master.stop()

worker2_log = os.path.join(get_config().get_str("log_dir"), "worker2.log")
assert os.path.exists(worker2_log), "worker2 log should exist"
with open(worker2_log) as f:
    content = f.read()
# 注册行确认 role 上报。
assert "role=storage_only" in content, "worker2 should register as storage_only"
# storage_only worker 无任何计算 task 执行痕迹。
assert "Executing task" not in content, \
    "storage_only worker must never execute compute tasks"
INFO("[ROLE] storage_only worker registered with role, no compute-task trace")

INFO("[PASS] test_worker_role")
