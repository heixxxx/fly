"""PeerRpc 流式通道压力/稳定性 case（流插件化 2026-08-31 第 3c 步）。

场景：master + 2 workers。
  member（worker1）：PeerRpc listen + 常驻 echo 循环——请求流式到达后
    经 stream respond writer 原样回传（覆盖响应流式）。
  check（worker2）：混合 payload × 轮次——
    · 空流 / 1B / 64KB / 1MB / 4MB-1 / 4MB / 4MB+1 / 16MB（跨帧边界）
    · × 6 轮，echo 校验（zlib.crc32 + 长度）
    · 并发收集圈：两连接同时各发一个流式请求再统一等待（RAS 动态链模式）
    · 错误路径：__fail__ 请求 → respond_failure 状态传播到 call_wait
断言：全部轮完成、echo 逐字节一致；失败即 case 失败（零容忍）。
"""
import os
import sys
import threading
import time
import zlib

from fly import get_config, open_db, as_task, wait_tasks
from fly.runtime import get_agent

PAYLOADS = [
    b"",                                  # 空流
    b"x",                                 # 1B
    os.urandom(64 * 1024),                # 64KB
    os.urandom(1024 * 1024),              # 1MB
    os.urandom(4 * 1024 * 1024 - 1),      # 4MB-1（帧边界内）
    os.urandom(4 * 1024 * 1024),          # 4MB（恰一帧）
    os.urandom(4 * 1024 * 1024 + 1),      # 4MB+1（跨帧）
    os.urandom(16 * 1024 * 1024),         # 16MB
]
ROUNDS = 6


@as_task(requires=["member"])
def member_serve(db, gen):
    agent = get_agent()
    agent.stop_peer_rpc()
    port = agent.start_peer_rpc_listen("127.0.0.1", 0)
    assert port > 0, "listen failed"
    db.write_object(f"{gen}_addr", {"host": "127.0.0.1", "port": port},
                    save_to_db=False)
    from _fly_log import INFO
    INFO(f"[STREAM-STRESS] member listening port={port}")
    # 常驻 echo 循环（模块级 _serve_loop；task 函数体内不得引用 nanobind
    # 对象，否则 cloudpickle 序列化失败）。
    threading.Thread(target=_serve_loop, daemon=True).start()


def _serve_loop():
    agent = get_agent()
    while True:
        try:
            conn_id, rpc_id, src, payload = agent.peer_rpc_recv_request(0)
        except Exception:
            return  # server 关闭（case 收尾）
        if payload == b"__fail__":
            agent.peer_rpc_respond_failure(conn_id, rpc_id, b"no ctx")
            continue
        # 响应流式 echo：payload → 压缩管线 → DATA/END。
        w = agent.peer_stream_respond_writer(conn_id, rpc_id, "lz4", -1)
        w.write(payload)
        from _fly_log import ERR
        if not w.finish():
            ERR(f"[STREAM-STRESS] respond finish failed rpc={rpc_id}")
            return


@as_task(inputs=lambda db, gen: [db.get_full_name(f"{gen}_addr")],
         requires=["check"])
def check_run(db, gen):
    agent = get_agent()
    addr = db.read_object(f"{gen}_addr")
    conn = agent.peer_rpc_connect(addr["host"], addr["port"])
    assert conn, "connect failed"

    failures = []

    def run_one(payload, tag):
        w = agent.peer_stream_writer(conn, "lz4", -1)
        rpc_id = w.rpc_id()
        w.write(payload)
        if not w.finish():
            failures.append(f"{tag}: writer finish failed")
            return
        status, resp = agent.peer_stream_call_wait(rpc_id, 0)
        if status != 1:
            failures.append(f"{tag}: status={status}")
            return
        if zlib.crc32(resp) != zlib.crc32(payload) or len(resp) != len(payload):
            failures.append(f"{tag}: echo mismatch len={len(resp)}/{len(payload)}")

    # 逐档 × 轮次：echo 校验。
    for r in range(ROUNDS):
        for i, p in enumerate(PAYLOADS):
            run_one(p, f"round{r}-p{i}")
        from _fly_log import INFO
        INFO(f"[STREAM-STRESS] round {r} done")

    # 并发收集圈：两连接各一个流式请求同时发（RAS 动态链模式）。
    conn2 = agent.peer_rpc_connect(addr["host"], addr["port"])
    assert conn2
    results = {}

    def concurrent_round(conn_tag):
        p = os.urandom(1024 * 1024)
        w = agent.peer_stream_writer(conn_tag, "lz4", -1)
        rid = w.rpc_id()
        w.write(p)
        w.finish()
        status, resp = agent.peer_stream_call_wait(rid, 0)
        results[conn_tag] = (status, resp == p)

    import concurrent.futures
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:
        f1 = ex.submit(concurrent_round, conn)
        f2 = ex.submit(concurrent_round, conn2)
        for f in (f1, f2):
            f.result()
    for tag, (status, ok) in results.items():
        if status != 0 or not ok:
            failures.append(f"concurrent conn={tag} status={status} ok={ok}")

    # 错误路径：respond_failure 状态传播。
    w = agent.peer_stream_writer(conn, "lz4", -1)
    rid = w.rpc_id()
    w.write(b"__fail__")
    w.finish()
    status, resp = agent.peer_stream_call_wait(rid, 0)
    if status == 0:
        failures.append("fail path: expected non-zero status")

    agent.peer_rpc_close(conn)
    agent.peer_rpc_close(conn2)
    if failures:
        raise RuntimeError(f"[STREAM-STRESS] {len(failures)} failures: " +
                           "; ".join(failures[:5]))
    INFO("[STREAM-STRESS] all rounds passed")


def main():
    gen = os.urandom(4).hex()
    master = get_agent()
    master.launch_local_workers([
        {"attributes": ["member"]},
        {"attributes": ["check"]},
    ])
    assert master.wait_workers_registered(timeout=60), "workers 未注册"
    db = open_db(get_config().get_str("log_dir") + "/db")
    member_serve(db, gen)
    check_run(db, gen)
    wait_tasks(600)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
