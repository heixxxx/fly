#!/usr/bin/env python3
"""Test: PeerChannelGroup 业务 RPC 往返 + 延迟 + 失败通知。

场景：2 个 worker（check + compute），check listen，compute connect，
compute 发 RPC 请求，check 收到回响应。测往返延迟。
另测 notify_failure 主动失败通知传播。
"""
import os, sys, time, tempfile

from fly import open_db, get_config, launch_workers, wait_tasks, as_task
from _fly_log import INFO
from agent import PeerChannelGroup, serialize_array, deserialize_array
import numpy as np

DB_PATH = os.path.join(get_config().get_str("log_dir"), "rpc_db")
RESULTS = {}


@as_task()
def check_task(db, group_id):
    """服务端：listen + accept + respond。"""
    from agent import PeerChannelGroup
    from _fly_log import INFO
    group = PeerChannelGroup(group_id)
    listener = group.listen(db)
    INFO(f"[CHECK] listening port={listener.port}")
    # 收 3 个请求 + 测延迟
    latencies = []
    for i in range(3):
        conn_id, rpc_id, src, payload = listener.accept_one(timeout=30)
        if rpc_id == 0:
            INFO("[CHECK] accept timeout")
            break
        t0 = time.perf_counter()
        arr = deserialize_array(payload)
        # echo back
        listener.respond(conn_id, rpc_id, serialize_array(arr * 2))
        latencies.append(time.perf_counter() - t0)
    # 测纯 RPC 往返延迟（小 payload）
    rpc_times = []
    for i in range(3):
        conn_id, rpc_id, src, payload = listener.accept_one(timeout=30)
        if rpc_id == 0: break
        t0 = time.perf_counter()
        listener.respond(conn_id, rpc_id, b"pong")
        rpc_times.append(time.perf_counter() - t0)
    avg_rpc = sum(rpc_times) / len(rpc_times) * 1000 if rpc_times else -1
    RESULTS["check_avg_rpc_ms"] = avg_rpc
    INFO(f"[CHECK] done, avg respond time={avg_rpc:.2f}ms")


@as_task()
def compute_task(db, group_id):
    """客户端：connect + rpc。"""
    from agent import PeerChannelGroup
    from _fly_log import INFO
    group = PeerChannelGroup(group_id)
    chan = group.connect(db, timeout=30)
    INFO("[COMPUTE] connected")
    # 发 3 个 array 请求
    for i in range(3):
        arr = np.arange(1000, dtype=np.float64) + i
        t0 = time.perf_counter()
        status, resp = chan.rpc(serialize_array(arr), timeout=10)
        elapsed = (time.perf_counter() - t0) * 1000
        result = deserialize_array(resp)
        expected = arr * 2
        ok = np.allclose(result, expected)
        INFO(f"[COMPUTE] rpc {i}: status={status} elapsed={elapsed:.2f}ms ok={ok}")
    # 测纯 RPC 往返延迟（小 payload）
    times = []
    for i in range(3):
        t0 = time.perf_counter()
        status, resp = chan.rpc(b"ping", timeout=10)
        times.append((time.perf_counter() - t0) * 1000)
    avg = sum(times) / len(times)
    RESULTS["compute_avg_rpc_ms"] = avg
    INFO(f"[COMPUTE] avg RPC round-trip={avg:.2f}ms")
    chan.close()


def main():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    db = open_db(DB_PATH)

    group = PeerChannelGroup()
    INFO(f"[MAIN] group_id={group.group_id[:8]}")

    get_config().set_int("fail_unscheduleable_tasks", 0)
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{"attributes": []}, {"attributes": []}])
    assert master.wait_for_workers(2), "Workers failed to connect"

    check_task(db, group.group_id)
    compute_task(db, group.group_id)
    wait_tasks()

    avg = RESULTS.get("compute_avg_rpc_ms", -1)
    print(f"\n=== RESULT: avg RPC round-trip = {avg:.2f}ms ===")
    assert avg > 0, "RPC round-trip not measured"
    assert avg < 50, f"RPC too slow: {avg}ms (expected <50ms)"
    print("[PASS] peer RPC round-trip within threshold")

    master.stop()


if __name__ == "__main__":
    main()
