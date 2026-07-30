"""测试 5：summary 边界 —— 无任何 message 触发。

覆盖：
  - 单进程 master（不启动 worker）时，无任何 message 触发，summary 仍正常打印
    （no message triggered 分支），屏障不崩溃。

注意：若启动 worker，worker 注册会产生 AGENT::0001（流程 message），summary 不再为空。
因此本测试不启动 worker，纯粹验证 master 单进程的空 summary 分支。
"""
import os
from _fly_log import INFO
from fly import get_config, get_work_directory
from _msgtest import get_message_log_content, count_summary_block

get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
# 不 launch_workers：单进程 master，无 worker 注册 → 无 AGENT::0001，无任何 message。
master.start()

# 不发任何 message，直接 stop（触发 summary 屏障 + 打印）。
master.stop()

content = get_message_log_content()
summary = count_summary_block(content)

# 验证 1：summary 正常打印（不崩溃）。
assert "Message Trigger Summary" in summary, f"summary 未打印:\n{summary}"
assert "By message id" in summary
assert "By domain" in summary

# 验证 2：无 message 时的空提示分支。
assert "no message triggered" in summary, f"应有 no message triggered 提示:\n{summary}"

INFO(f"[PASS] test_message_empty_summary")
