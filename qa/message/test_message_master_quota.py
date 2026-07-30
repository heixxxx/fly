"""测试 3：master 打印配额（id 配额）。

覆盖（review 问题 1 修复验证）：
  - master 打印 id 配额控制 worker 推送来的 message 打印上限
  - master 打印 id 配额【同样控制 master 自身的 message】（review §2 原盲区）
  - 配额不影响 worker 触发计数（summary 仍反映 worker 实际触发次数）
  - master 自身 message 与 worker 推送共用同一 id 配额（同 id 合并计数）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_master_print_id_limit,
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

# master 打印 id 配额设为 3。
set_master_print_id_limit(3)

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

# master 自身在 worker task 前发 2 条（受 master 打印配额控制，2 < 3 通过）。
message("MLOCAL::0001", 1, "master local 1")
message("MLOCAL::0001", 2, "master local 2")

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("MQUOTA::0001", "INFO")
    # worker 推 5 条（worker 本地配额默认 20，全部推送）。
    for i in range(5):
        message("MQUOTA::0001", 1, f"worker iter {i}")
    return "ok"


worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()

# 验证 1：master 打印 id 配额 3 → worker 推 5 条只有 3 条进 message.log。
worker_count = content.count("[MQUOTA::0001]")
assert worker_count == 3, f"worker 推送应有 3 条进 message.log，实际 {worker_count}"

# 验证 2：master 自身 MLOCAL::0001 发 2 条（< 配额 3）全进 message.log。
local_count = content.count("[MLOCAL::0001]")
assert local_count == 2, f"master 自身 2 条应全进 message.log，实际 {local_count}"

# 验证 3：summary 反映 worker 实际触发次数（master 配额不影响触发计数）。
assert "MQUOTA::0001 : 5" in content, f"summary 应显示 worker 触发 5 次:\n{content}"

INFO(f"[PASS] test_message_master_quota")
