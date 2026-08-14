"""测试 11：master → worker 配额运行时同步。

核心验证：worker task 内【不自己设配额】，master 设的 limit 通过 MSG_LIMIT_SYNC 同步到 worker，
使 worker log 打印条数真正受控（当前实现修复前做不到——worker 拿默认值 20）。

覆盖：
  - worker 不设配额，master 设 set_message_global_limit(3) → worker log 只 3 条（同步生效）
  - master message.log 收 3 条（master 打印配额同步）
  - summary trigger 计数 = worker 实际触发次数（配额不影响计数）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id, set_message_global_limit,
)
from _msgtest import wait_for, get_message_log_content, get_worker_debug_log

DB_PATH = os.path.join(get_work_directory(), "msg_sync_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("SYNC::0001", "INFO")

from fly.runtime import get_agent
master = get_agent()

# master 设配额 = 3，会通过 MSG_LIMIT_SYNC 广播给所有 worker（含后续上线的）。
set_message_global_limit(3)

master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    # 关键：worker task 内【不设配额】，验证 master 设的 limit 同步到了 worker。
    # 若同步失效，worker 拿默认 20，发 5 条会全推；同步成功则只推 3 条。
    register_message_id("SYNC::0001", "INFO")
    for i in range(5):
        message("SYNC::0001", 1, f"sync iter {i}")
    return "ok"


worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()
worker1_log = get_worker_debug_log(1)

# 验证 1（关键）：worker log 只 3 条 —— 证明配额同步到了 worker。
worker_count = worker1_log.count("[SYNC::0001]")
assert worker_count == 3, f"worker log 应只有 3 条（配额同步生效），实际 {worker_count}"

# 验证 2：master message.log 收 3 条（worker 只推了 3 条）。
master_count = content.count("[SYNC::0001]")
assert master_count == 3, f"master message.log 应有 3 条，实际 {master_count}"

# 验证 3：summary trigger 计数 = 5（配额不影响计数）。
assert "SYNC::0001 : 5" in content, f"summary 应显示触发 5 次:\n{content}"

INFO("[PASS] test_message_limit_sync")
