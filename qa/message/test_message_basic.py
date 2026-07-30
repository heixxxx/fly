"""测试 1：message 基础功能。

覆盖：
  - master 自身 message → message.log + 本地 debug log
  - worker task 内 message → 推送 master → message.log
  - 打印格式 [DOMAIN::NNNN] <source> msg
  - 级别绑定（注册时指定 INFO/WARN/ERROR，发送时查表）
  - 未注册 id 被丢弃（不进 message.log，不计次数）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
)
from _msgtest import wait_for, get_message_log_content, get_master_debug_log

DB_PATH = os.path.join(get_work_directory(), "msg_basic_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

# master 进程注册（级别绑定）。
register_message_id("BASICMS::0001", "INFO")    # master 用，INFO
register_message_id("BASICWK::0001", "WARN")    # worker 用，WARN
register_message_id("BASICWK::0002", "ERROR")   # worker 用，ERROR

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1), "worker 未连上"

# master 自身 message，source=10。
message("BASICMS::0001", 10, "master hello")

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    # worker 进程注册（独立进程，白名单+级别绑定）。
    register_message_id("BASICWK::0001", "WARN")
    register_message_id("BASICWK::0002", "ERROR")
    message("BASICWK::0001", 20, "worker warn msg")    # WARN 级别
    message("BASICWK::0002", 21, "worker error msg")   # ERROR 级别
    message("UNREG::9999", 30, "should be dropped")     # 未注册 → 丢弃
    return "ok"


worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1), "task 未完成"
assert len(master.failed_tasks) == 0, f"task 失败: {master.failed_tasks}"

master.stop()

content = get_message_log_content()
master_log = get_master_debug_log()

# 验证 1：master 自身 message 在 message.log，格式含 source。
assert "[master] [BASICMS::0001] <10> master hello" in content, \
    f"master message 格式错误:\n{content}"

# 验证 2：worker 推送的 message 在 message.log，带 worker 标注 + source。
assert "[worker1] [BASICWK::0001] <20> worker warn msg" in content, \
    f"worker WARN message 格式错误:\n{content}"
assert "[worker1] [BASICWK::0002] <21> worker error msg" in content, \
    f"worker ERROR message 格式错误:\n{content}"

# 验证 3：级别绑定正确 —— WARN/ERROR 级别字段正确落盘。
assert "[WARN] [worker1] [BASICWK::0001]" in content, "WARN 级别未正确绑定"
assert "[ERROR] [worker1] [BASICWK::0002]" in content, "ERROR 级别未正确绑定"

# 验证 4：未注册的 id 不进 message.log。
assert "UNREG::9999" not in content, "未注册 id 不应进 message.log"

# 验证 5：message 也写 master 本地 debug log（带 [DOMAIN::NNNN] <source> 前缀）。
assert "[BASICMS::0001] <10> master hello" in master_log, \
    "master 自身 message 未写本地 debug log"

INFO(f"[PASS] test_message_basic")
