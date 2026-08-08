"""测试 3：master 打印配额 = worker 发送配额（统一 limit）。

配额语义：set_message_global_limit(N) 同时控制 worker 发送配额 + master 打印配额（同一 N）。
覆盖：
  - worker 发送配额：worker 推 N 条后超限丢弃（worker 本地 emit 计数）
  - master 打印配额：master 汇聚打印总量受 N 限制
  - master 自身 message 同样受配额（走 master 进程 Registry）
  - 配额不影响 trigger 计数（summary 反映真实触发次数）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_message_global_limit,
)
from _msgtest import wait_for, get_message_log_content

DB_PATH = os.path.join(get_work_directory(), "msg_mquota_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("MQUOTA::0001", "INFO")   # worker 用
register_message_id("MLOCAL::0001", "INFO")   # master 自身用

from fly.runtime import get_agent
master = get_agent()

# 统一配额 = 3：同时控 worker 发送（每 worker 每 id 3 条）+ master 打印（总量 3 条）。
set_message_global_limit(3)

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

# master 自身在 worker task 前发 2 条（master 自身 Registry emit 配额=3，2 < 3 通过）。
message("MLOCAL::0001", 1, "master local 1")
message("MLOCAL::0001", 2, "master local 2")

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("MQUOTA::0001", "INFO")
    # worker 同步到配额=3，发 5 条 → 前 3 条 emit（推送），后 2 条丢弃（trigger 仍计）。
    for i in range(5):
        message("MQUOTA::0001", 1, f"worker iter {i}")
    return "ok"


worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()

# 验证 1：worker 发送配额=3 → MQUOTA::0001 只 3 条进 message.log。
worker_count = content.count("[MQUOTA::0001]")
assert worker_count == 3, f"worker 推送应有 3 条进 message.log，实际 {worker_count}"

# 验证 2：master 自身 MLOCAL::0001 发 2 条（< 3）全进 message.log。
local_count = content.count("[MLOCAL::0001]")
assert local_count == 2, f"master 自身 2 条应全进 message.log，实际 {local_count}"

# 验证 3：summary 反映 trigger 计数（MQUOTA::0001 worker 触发 5 次，配额不影响计数）。
assert "MQUOTA::0001 : 5" in content, f"summary 应显示 worker 触发 5 次:\n{content}"

INFO("[PASS] test_message_master_quota")
