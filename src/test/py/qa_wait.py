"""QA 等待 helper：异步结果的轮询等待（零容忍裸 sleep;assert 的替代）。

用法（case 脚本内）：
    from test import wait_until
    assert wait_until(lambda: master.failed_tasks, timeout=30), \
        "group death must surface within 30s"
"""
import time


def wait_until(cond, timeout: float = 30.0, interval: float = 0.05) -> bool:
    """轮询等待 cond() 为真。deadline 内为真返回 True，超时返回 False。

    cond 无参可调用；interval 为采样间距。替代「裸 sleep 后断言」——
    断言的事件到达时间不确定，固定睡眠在慢机/高负载下必然 flaky。
    """
    deadline = time.monotonic() + timeout
    while True:
        if cond():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)
