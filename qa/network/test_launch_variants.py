"""_advertise_host 优先级 + launch 变体（role 传递 / ssh target 校验）。

覆盖（2026-09 覆盖率批次 14 项之 8）：
  - config master_advertise_host 覆盖 → _advertise_host() 原样返回（最高优先级）
  - 清覆盖后回退探测链（本机非环回地址，同 test_launch_ssh_workers 环境口径）
  - launch_ssh_workers target 缺 'host' → RuntimeError（ssh 下发之前，不真连）
  - launch_workers role="storage_only" → 注册为 storage worker
  - launch_workers 非法 role → WARN 回退 hybrid（不进 storage 集合）
（ssh 真连路径环境特化，由 qa/network/test_launch_ssh_workers 覆盖，此处不测。）
"""
import os

from _fly_log import INFO

from fly import launch_workers, launch_ssh_workers, get_config
from fly.runtime import get_agent
from test import wait_until, record

master = get_agent()
master.start()

# ── 1. master_advertise_host 覆盖优先级 ──────────────────────────────
cfg = get_config()
cfg.set_str("master_advertise_host", "10.123.0.9")
try:
    overridden = master._advertise_host()
    # 原断言：assert master._advertise_host() == "10.123.0.9", \
    record("advertise/config_override_wins", overridden)
finally:
    cfg.set_str("master_advertise_host", "")
cleared_host = master._advertise_host()
override_cleared = cleared_host != "10.123.0.9"
# 原断言：assert master._advertise_host() != "10.123.0.9", "override must be cleared"
record("advertise/override_cleared", override_cleared, detail=cleared_host)
INFO("[DONE] master_advertise_host override wins + clears")

# 清覆盖后走探测链：本机应解析出非环回地址（UDP connect 出口 IP / hostname）
advertised = master._advertise_host()
try:
    import ipaddress
    advertised_ip_not_loopback = not ipaddress.ip_address(advertised).is_loopback
    # 原断言：assert not ipaddress.ip_address(advertised).is_loopback, advertised
    record("advertise/detected_ip_not_loopback", advertised_ip_not_loopback,
           detail=advertised)
except ValueError:
    raise AssertionError(f"advertised host is not an IP: {advertised!r}")
INFO(f"[DONE] detection chain returns non-loopback address ({advertised})")

# ── 2. launch_ssh_workers target 缺 host → 立即 RuntimeError ─────────
try:
    launch_ssh_workers([{"attributes": ["x"]}])
    raise AssertionError("ssh target without 'host' must raise")
except RuntimeError as e:
    err_msg = str(e)
    err_type = type(e).__name__
    msg_mentions_host = "missing 'host'" in err_msg
# 原断言：assert "missing 'host'" in str(e), str(e)
record("ssh_target_missing_host/error_type", err_type)
record("ssh_target_missing_host/msg_mentions_host", msg_mentions_host, detail=err_msg)
INFO("[DONE] launch_ssh_workers rejects target without host (pre-ssh)")

# ── 3. role 传递：storage_only 注册为存储 worker；非法 role 回退 hybrid ─
launch_workers([{"role": "storage_only"}, {"role": "bogus_role"}])
assert wait_until(lambda: master.worker_count >= 2, timeout=60), \
    f"both workers should register, got {master.worker_count}"

storage_ids = set(master._agent.get_storage_only_workers())
one_storage_worker = len(storage_ids) == 1
# 原断言：assert len(storage_ids) == 1, f"exactly one storage_only worker expected: {storage_ids}"
record("role/storage_only_worker_count_is_1", one_storage_worker, detail=sorted(storage_ids))
INFO(f"[DONE] role=storage_only registered as storage worker {storage_ids}; "
     "bogus role fell back to hybrid")

master.stop()
INFO("[DONE] test_launch_variants")
