"""Test: wait_workers_registered / expect_workers（唤起占位符 API）.

场景：
1. launch_workers 自动登记占位符 → wait_workers_registered() 立即（或注册后）返回 True。
2. expect_workers 手动登记一个永不启动的 worker id → 短超时下返回 False
   （config worker_register_timeout 设小值，QA 自行设置短超时的用法演示）。
"""
from _fly_log import INFO
import time

from fly import launch_workers, wait_workers_registered, expect_workers, get_config

get_config().set_int("worker_register_timeout", 2)  # QA 自设短超时（默认 0=无限）

# 1. 正常路径：spawn 2 worker，本地启动快，注册等待返回 True
launch_workers([{}, {}])
t0 = time.time()
assert wait_workers_registered() is True, "wait_workers_registered should return True"
elapsed = time.time() - t0
INFO(f"[1] wait_workers_registered returned True in {elapsed:.2f}s")

# 2. 超时路径：手动登记永不启动的 worker → config 2s 超时 → False
expect_workers([999])
t0 = time.time()
assert wait_workers_registered() is False, "phantom worker should time out (config=2s)"
elapsed = time.time() - t0
assert 1.5 <= elapsed <= 10.0, f"timeout should honor config (~2s), got {elapsed:.2f}s"
INFO(f"[2] phantom worker timed out after {elapsed:.2f}s as configured")

# 3. 显式 timeout 参数优先于 config：再次登记，用显式 1s 更快返回
expect_workers([998])
t0 = time.time()
assert wait_workers_registered(timeout=1.0) is False
elapsed = time.time() - t0
assert elapsed < 2.5, f"explicit timeout=1s should return in ~1s, got {elapsed:.2f}s"
INFO(f"[3] explicit timeout=1 honored ({elapsed:.2f}s)")

INFO("[PASS] test_wait_workers_registered")
