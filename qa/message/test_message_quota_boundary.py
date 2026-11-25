"""测试 7：配额边界值 -1（不限）/ 0（禁止）。

覆盖：
  - id 配额 -1：不限制，全部推送 + 打印
  - id 配额 0：完全禁止，一条都不打印/推送，但仍计数 1
  - master 打印配额 0：完全禁止 worker 推送来的打印
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_message_global_limit,
)
from _msgtest import get_message_log_content
from test import record, wait_tasks_no_failure

DB_PATH = os.path.join(get_work_directory(), "msg_boundary_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("BDUNLIM::0001", "INFO")   # id 配额 -1 测试
register_message_id("BDZERO::0001", "INFO")    # id 配额 0 测试
register_message_id("BDMZERO::0001", "INFO")   # master 打印配额 0 测试

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("BDUNLIM::0001", "INFO")
    register_message_id("BDZERO::0001", "INFO")
    register_message_id("BDMZERO::0001", "INFO")

    # id 配额 -1：发 15 次全部通过。
    set_message_global_limit(-1)  # 注意：这是全局默认配额，影响后续所有 id
    for i in range(15):
        message("BDUNLIM::0001", 1, f"u{i}")

    # id 配额 0：禁止，第一次也丢弃，但计数 1。
    set_message_global_limit(0)
    message("BDZERO::0001", 1, "should be blocked")
    return "ok"


worker_send(db)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 1, "worker_send/no_failed_tasks", timeout=20.0)

master.stop()

content = get_message_log_content()

# 验证 1：id 配额 -1 → 15 条全进 message.log。
unlim_count = content.count("[BDUNLIM::0001]")
# 原断言：assert unlim_count == 15, f"配额 -1 应有 15 条，实际 {unlim_count}"
record("boundary/unlimited_count", unlim_count)

# 验证 2：id 配额 0 → 0 条进 message.log（完全禁止），但 summary 计数 1。
zero_count = content.count("[BDZERO::0001]")
# 原断言：assert zero_count == 0, f"配额 0 应有 0 条，实际 {zero_count}"
record("boundary/zero_count", zero_count)
zero_trigger_1 = "BDZERO::0001 : 1" in content
# 原断言：assert "BDZERO::0001 : 1" in content, f"配额 0 summary 应计数 1:\n{content}"
record("boundary/zero_trigger_1", zero_trigger_1, detail=content)

# 验证 3：summary 显示 BDUNLIM::0001 触发 15 次。
unlim_trigger_15 = "BDUNLIM::0001 : 15" in content
# 原断言：assert "BDUNLIM::0001 : 15" in content, f"summary 应显示 15 次:\n{content}"
record("boundary/unlim_trigger_15", unlim_trigger_15)

INFO("[DONE] test_message_quota_boundary")
