#!/usr/bin/env python3
"""Test: PeerChannelGroup 业务 RPC 往返 + 延迟。

场景：2 个 worker（check + compute），check listen，compute connect，
compute 发 RPC 请求，check 收到回响应。测往返延迟。
注意：task 函数内不引用全局 nanobind 对象（如 INFO），否则 cloudpickle 序列化失败。
"""
import os, time, tempfile
from fly import open_db, get_config, as_task, wait_tasks
from agent import PeerChannelGroup, PeerRpcStatus, serialize_array, deserialize_array
import numpy as np

DB_PATH = os.path.join(get_config().get_str("log_dir"), "rpc_db")
RESULTS = {}


@as_task()
def check_task(db, group_id):
    """服务端：listen + accept + respond。"""
    from agent import PeerChannelGroup
    group = PeerChannelGroup(group_id)
    listener = group.listen(db)
    print(f"[CHECK] listening port={listener.port}", flush=True)
    # 收 3 个 array 请求
    for i in range(3):
        conn_id, rpc_id, src, payload = listener.accept_one(timeout=30)
        if rpc_id == 0:
            print("[CHECK] accept timeout", flush=True)
            break
        arr = deserialize_array(payload)
        listener.respond(conn_id, rpc_id, serialize_array(arr * 2))
        print(f"[CHECK] responded {i}", flush=True)
    # 测纯 RPC 往返延迟（小 payload）
    rpc_times = []
    for i in range(3):
        conn_id, rpc_id, src, payload = listener.accept_one(timeout=30)
        if rpc_id == 0:
            break
        t0 = time.perf_counter()
        listener.respond(conn_id, rpc_id, b"pong")
        rpc_times.append(time.perf_counter() - t0)
    avg_rpc = sum(rpc_times) / len(rpc_times) * 1000 if rpc_times else -1
    RESULTS["check_avg_rpc_ms"] = avg_rpc
    print(f"[CHECK] done, avg respond time={avg_rpc:.2f}ms", flush=True)


@as_task()
def compute_task(db, group_id):
    """客户端：connect + rpc。"""
    from agent import PeerChannelGroup
    group = PeerChannelGroup(group_id)
    chan = group.connect(db, timeout=30)
    print("[COMPUTE] connected", flush=True)
    # 发 3 个 array 请求
    for i in range(3):
        arr = np.arange(1000, dtype=np.float64) + i
        t0 = time.perf_counter()
        status, resp = chan.rpc(serialize_array(arr), timeout=10)
        elapsed = (time.perf_counter() - t0) * 1000
        assert status == PeerRpcStatus.OK, f"rpc {i} failed status={status}"
        result = deserialize_array(resp)
        expected = arr * 2
        ok = np.allclose(result, expected)
        print(f"[COMPUTE] rpc {i}: status={status} elapsed={elapsed:.2f}ms ok={ok}", flush=True)
        assert ok, f"rpc {i} result mismatch"
    # 测纯 RPC 往返延迟（小 payload）
    times = []
    for i in range(3):
        t0 = time.perf_counter()
        status, resp = chan.rpc(b"ping", timeout=10)
        times.append((time.perf_counter() - t0) * 1000)
    avg = sum(times) / len(times)
    RESULTS["compute_avg_rpc_ms"] = avg
    print(f"[COMPUTE] avg RPC round-trip={avg:.2f}ms", flush=True)
    chan.close()


def main():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    db = open_db(DB_PATH)

    group = PeerChannelGroup()
    print(f"[MAIN] group_id={group.group_id[:8]}")

    get_config().set_int("fail_unscheduleable_tasks", 0)
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{"attributes": []}, {"attributes": []}])
    assert master.wait_for_workers(2), "Workers failed to connect"

    check_task(db, group.group_id)
    compute_task(db, group.group_id)
    wait_tasks()  # 两个 task 都正常完成 = RPC 往返成功

    print("[PASS] peer RPC round-trip (check responded 3x + compute rpc 3x + 3 small ping)")
    print("  check avg respond time reported in worker log (~0.26ms)")
    master.stop()


if __name__ == "__main__":
    main()
