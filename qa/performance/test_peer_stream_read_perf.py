"""PeerRpc 真流式读端性能基准：业务拉动解压（pickle.load(reader)）全链。

真实使用形态对齐 solver 动态链（ras_graph_dynamic）：
  - 请求方向 check: peer_stream_writer + pickle.dump(req) 直入压缩/发送管线
  - 请求方向 member: peer_rpc_recv_request_stream + pickle.load(reader) 拉动
  - 响应方向 member: peer_stream_respond_writer + pickle.dump(resp)
  - 响应方向 check:  peer_stream_response_reader + pickle.load(reader) 拉动
载荷 = f64 近随机数组（serialize_array 包装，近随机 ⇒ 显式 none 对照 lz4）。
断言仅数据一致性；性能数字打印供对比（不设紧阈值防 flaky）。
64MB/512MB 大档内存 footprint 大，仅 PEER_RPC_PERF_FULL=1 时跑。
"""
import os
import pickle
import sys
import time
import zlib

import numpy as np

from agent import serialize_array, deserialize_array
from fly import get_config, open_db, as_task, wait_tasks
from fly.runtime import get_agent

SIZES = [4 * 1024 * 1024, 16 * 1024 * 1024, 64 * 1024 * 1024]
BIG = 512 * 1024 * 1024   # 主档（自环内存安全上限，同 test_peer_rpc_perf 口径）
WARMUP = 1
ROUNDS = 3


@as_task(requires=["member"])
def member_serve(db, gen):
    agent = get_agent()
    agent.stop_peer_rpc()
    port = agent.start_peer_rpc_listen("127.0.0.1", 0)
    assert port > 0
    db.write_object(f"{gen}_addr", {"host": "127.0.0.1", "port": port},
                    save_to_db=False)

    def loop():
        ag = get_agent()
        while True:
            try:
                conn_id, rpc_id, src, reader = ag.peer_rpc_recv_request_stream(0)
            except Exception:
                return
            try:
                # 真流式请求读端：pickle.load 拉动边收边反序列化
                #（结构对齐 solver _serve_loop：load → 处理 → dump 响应）。
                data = pickle.load(reader)
            except Exception:
                return
            w = ag.peer_stream_respond_writer(conn_id, rpc_id, data["comp"], -1)
            pickle.dump(data, w)   # echo：响应即请求原文（恒流式）
            w.finish()

    import threading
    threading.Thread(target=loop, daemon=True).start()

    pass  # 单 worker 自环：echo 循环由 daemon 线程承担，task 立即返回


@as_task(requires=["check"])
def check_run(db, gen):
    agent = get_agent()
    addr = db.read_object(f"{gen}_addr")
    conn = agent.peer_rpc_connect(addr["host"], addr["port"])
    assert conn

    results = {}

    def stream_roundtrip(req_arr, comp):
        """真流式往返：请求/响应双向 pickle 流（solver _stream_call 同构）。"""
        req = {"action": "iterate", "ghosts": serialize_array(req_arr),
               "comp": comp}
        w = agent.peer_stream_writer(conn, comp, -1)
        rid = w.rpc_id()
        t0 = time.perf_counter()
        pickle.dump(req, w)
        w.finish()
        t_write = time.perf_counter() - t0
        status, reader = agent.peer_stream_response_reader(rid, 0)
        if status != 1:
            print(f"[PERF-DBG] resp status={status}", flush=True)
            return None
        resp = pickle.load(reader)   # 拉动解压：消费与网络接收重叠
        t_total = time.perf_counter() - t0
        if not np.array_equal(deserialize_array(resp["ghosts"]), req_arr):
            print(f"[PERF-DBG] data mismatch ({len(req_arr)}B)", flush=True)
            return None
        return t_write, t_total

    def stream_roundtrip_from_bytes(req_bytes, crc_expect, comp):
        """512MB 档内存精简往返：调用方已序列化并丢原数组，CRC 校验代替
        全量反序列化对照——echo 形态下 check 侧同时驻留 4 份 512MB 会把
        5.8GB 机器打进页回收（数字失真，非管线能力）。"""
        req = {"action": "iterate", "ghosts": req_bytes, "comp": comp}
        w = agent.peer_stream_writer(conn, comp, -1)
        rid = w.rpc_id()
        t0 = time.perf_counter()
        pickle.dump(req, w)
        w.finish()
        t_write = time.perf_counter() - t0
        del req   # 丢 dict 引用（bytes 由调用方持有）
        status, reader = agent.peer_stream_response_reader(rid, 0)
        if status != 1:
            print(f"[PERF-DBG] resp status={status}", flush=True)
            return None
        resp = pickle.load(reader)   # 拉动解压：消费与网络接收重叠
        t_total = time.perf_counter() - t0
        ok = zlib.crc32(resp["ghosts"]) == crc_expect
        del resp
        if not ok:
            print(f"[PERF-DBG] crc mismatch", flush=True)
            return None
        return t_write, t_total

    def bench(tag, payload_bytes, comp, rounds=ROUNDS, warmup=WARMUP):
        n = payload_bytes // 8
        rng = np.random.default_rng(42)
        lean = payload_bytes > BIG // 2
        if lean:
            # 大档：序列化一次，丢原数组，跨轮复用 bytes（峰值 ≈ 2×size）
            arr = rng.random(n)
            req_bytes = serialize_array(arr)
            crc_expect = zlib.crc32(req_bytes)
            del arr
            fn = lambda: stream_roundtrip_from_bytes(req_bytes, crc_expect, comp)
        else:
            arr = rng.random(n)
            fn = lambda: stream_roundtrip(arr, comp)
        times, wtimes = [], []
        for _ in range(warmup):
            r = fn()
            if r is None:
                raise RuntimeError(f"{tag}: WARMUP FAILED")
        for _ in range(rounds):
            r = fn()
            if r is None:
                raise RuntimeError(f"{tag}: FAILED")
            wtimes.append(r[0])
            times.append(r[1])
        med = sorted(times)[len(times) // 2]
        wmed = sorted(wtimes)[len(wtimes) // 2]
        mb = payload_bytes / 1024 / 1024
        results[tag] = {"med_s": round(med, 4),
                        "mbps": round(mb / med, 1) if med > 0 else 0}
        print(f"[PERF] {tag}: med={med*1000:.0f}ms ({mb:.0f}MB) "
              f"-> {mb/med:.0f} MB/s write_med={wmed*1000:.0f}ms", flush=True)
        if lean:
            del req_bytes
        else:
            del arr

    full = os.environ.get("PEER_RPC_PERF_FULL") == "1"
    sizes = SIZES + [BIG] if full else SIZES
    for size in sizes:
        tag_mb = f"{size/1024/1024:.0f}MB"
        bench(f"stream-read none {tag_mb}", size, "none")
        bench(f"stream-read lz4  {tag_mb}", size, "lz4")

    for tag, r in sorted(results.items()):
        print(f"[PERF-SUMMARY] {tag}: {r['med_s']}s {r['mbps']}MB/s", flush=True)
    # 结果落库（持久对象，master 可读）：任务失败时 wait_tasks 不报错，
    # main 凭该对象存在性 + 档位齐全性判 case 成败，防静默假绿。
    db.write_object(f"{gen}_result", {"tags": sorted(results.keys())})


def main():
    gen = os.urandom(4).hex()
    master = get_agent()
    # 双 worker 拓扑（对齐 solver 动态链真实形态：member 常驻与 check 驱动
    # 分属不同 worker 进程）。单 worker 自环下两业务线程同进程共享 GIL，
    # 大 payload 会被线程争抢拖垮 20 倍（512MB none 540MB/s→37MB/s），数字
    # 失真且与真实部署无关。
    master.launch_local_workers([{"attributes": ["member"]},
                                 {"attributes": ["check"]}])
    assert master.wait_workers_registered(timeout=60)
    db = open_db(get_config().get_str("log_dir") + "/db")
    member_serve(db, gen)
    check_run(db, gen)
    wait_tasks(1200)
    # 显式校验：bench 失败会在 check_run 内抛错 → 结果对象缺失/档位不齐
    res = db.read_object(f"{gen}_result")
    expected = 2 * len(SIZES + [BIG] if os.environ.get("PEER_RPC_PERF_FULL") == "1" else SIZES)
    assert res and len(res["tags"]) == expected, \
        f"bench incomplete: {len(res['tags']) if res else 0}/{expected}"


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
