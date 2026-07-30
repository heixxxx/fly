"""测试 10：per-id 配额（三层链式优先级 per-id > domain > global）。

配额语义：set_message_id_limit(id, N) 同时控制 worker 发送 + master 打印（同一 N）。
覆盖：
  - worker per-id 配额生效：发 > N 条只前 N 条进 message.log，summary trigger 计总数
  - per-id 屏蔽 domain：同时设宽松 domain 配额，仍按 per-id 生效（domain 不参与检查）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_message_id_limit, set_message_domain_limit,
)
from _msgtest import wait_for, get_message_log_content

DB_PATH = os.path.join(get_work_directory(), "msg_perid_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("PERID::0001", "INFO")   # worker per-id 主测
register_message_id("PERID2::0001", "INFO")  # per-id 屏蔽 domain 测

from fly.runtime import get_agent
master = get_agent()

# per-id 设为 3，同时设宽松 domain(100) 验证 per-id 优先（domain 不参与检查）。
# 注意：配额在 master 进程设置，通过 MSG_LIMIT_SYNC 自动同步给 worker。
set_message_id_limit("PERID::0001", 3)
set_message_domain_limit("PERID", 100)  # 宽松，不应生效（per-id 优先）

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("PERID::0001", "INFO")
    # per-id=3（已从 master 同步），发 5 条：前 3 条 emit，后 2 条丢弃（trigger 仍计）。
    # domain(100) 宽松但不生效（per-id 优先于 domain）。
    for i in range(5):
        message("PERID::0001", 1, f"pid iter {i}")
    return "ok"


worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()

# 验证 1：per-id=3 → PERID::0001 只 3 条进 message.log。
perid1_count = content.count("[PERID::0001]")
assert perid1_count == 3, f"per-id=3 应有 3 条进 message.log，实际 {perid1_count}"

# 验证 2：per-id 屏蔽 domain —— 若 domain(100) 生效则会有 5 条，实际按 per-id=3。
assert perid1_count == 3, "per-id 应屏蔽宽松的 domain 配额"

# 验证 3：summary 反映 trigger 计数（per-id 配额不影响计数）。
assert "PERID::0001 : 5" in content, f"summary 应显示 worker 触发 5 次:\n{content}"

INFO(f"[PASS] test_message_per_id_quota")
