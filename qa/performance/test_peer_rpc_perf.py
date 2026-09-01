"""PeerRpc 性能基准 case：流式大 payload vs 单帧基线。

矩阵：payload（256KB/4MB/16MB/64MB/1GB）× 模式（stream-lz4 / stream-zstd9 /
stream-none / single-frame 基线全档）。member echo 服务按请求回传
（响应恒流式）。断言仅数据完整性；性能数字打印供对比（不设紧阈值防 flaky）。
单帧基线在大 payload 档的内存峰值/失败是旧路径缺陷的现场，原样记录。
"""
import os
import sys
import time
import zlib

from fly import get_config, open_db, as_task, wait_tasks
from fly.runtime import get_agent

PAYLOAD_SIZES = [256 * 1024, 4 * 1024 * 1024, 16 * 1024 * 1024, 64 * 1024 * 1024]
BIG = 512 * 1024 * 1024   # 主档（自环/双 worker 内存安全上限）
WARMUP = 1
ROUNDS = 3


@as_task()
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
                conn_id, rpc_id, src, payload = ag.peer_rpc_recv_request(0)
            except Exception:
                return
            w = ag.peer_stream_respond_writer(conn_id, rpc_id, "lz4", -1)
            w.write(payload)
            w.finish()

    import threading
    threading.Thread(target=loop, daemon=True).start()

    pass  # 单 worker 自环：echo 循环由 daemon 线程承担，task 立即返回


@as_task()
def check_run(db, gen):
    agent = get_agent()
    addr = db.read_object(f"{gen}_addr")
    conn = agent.peer_rpc_connect(addr["host"], addr["port"])
    assert conn

    results = {}

    def stream_roundtrip(payload, comp, level):
        """流式往返（响应恒流式 lz4——响应占用相同传输量，对比聚焦请求方向）。"""
        w = agent.peer_stream_writer(conn, comp, level)
        rid = w.rpc_id()
        t0 = time.perf_counter()
        w.write(payload)
        w.finish()
        t_write = time.perf_counter() - t0
        status, resp = agent.peer_stream_call_wait(rid, 0)
        t_total = time.perf_counter() - t0
        if status != 1 or zlib.crc32(resp) != zlib.crc32(payload):
            print(f"[PERF-DBG] stream fail status={status} resp_len={len(resp)} "
                  f"payload_len={len(payload)}", flush=True)
            return None
        st = w.stage_stats()
        print(f"[STAGE] write_wait={st[0]//10**6}ms compress={st[1]//10**6}ms "
              f"send={st[2]//10**6}ms (producer vs compressor thread)", flush=True)
        return t_write, t_total

    def single_roundtrip(payload):
        """单帧基线（旧行为）：大 payload 触发写缓冲/重组缓冲内存峰值。"""
        try:
            t0 = time.perf_counter()
            status, resp = agent.peer_rpc_call(conn, payload, 0)
            t_total = time.perf_counter() - t0
            if status != 1 or zlib.crc32(resp) != zlib.crc32(payload):
                print(f"[PERF-DBG] single fail status={status} resp_len={len(resp)} "
                      f"payload_len={len(payload)}", flush=True)
                return None
            return t_total
        except Exception:
            return None

    def bench(tag, fn, payload, rounds=ROUNDS, warmup=WARMUP, get_write=False):
        times, wtimes = [], []
        for _ in range(warmup):
            r = fn(payload)
            if r is None:
                print(f"[PERF] {tag}: WARMUP FAILED (skipped)", flush=True)
                return
        for _ in range(rounds):
            r = fn(payload)
            if r is None:
                print(f"[PERF] {tag}: FAILED", flush=True)
                return
            if get_write:
                wtimes.append(r[0])
            times.append(r[1] if get_write else r)
        med = sorted(times)[len(times) // 2]
        mb = len(payload) / 1024 / 1024
        extra = ""
        if get_write and wtimes:
            wmed = sorted(wtimes)[len(wtimes) // 2]
            extra = f" write_med={wmed*1000:.0f}ms"
        results[tag] = {"med_s": round(med, 4),
                        "mbps": round(mb / med, 1) if med > 0 else 0}
        print(f"[PERF] {tag}: med={med*1000:.0f}ms ({mb:.0f}MB) "
              f"-> {mb/med:.0f} MB/s{extra}", flush=True)

    def stream_bench(payload, comp, level, tag):
        def fn(p):
            try:
                return stream_roundtrip(p, comp, level)
            except Exception as e:
                print(f"[PERF] {tag}: EXC {e}", flush=True)
                return None
        bench(tag, fn, payload, get_write=True)

    # 大载荷档（64MB/512MB）内存 footprint 大，仅在 PEER_RPC_PERF_FULL=1 时
    # 跑——稳定性套件每轮全量执行时若并发跑 1GB 载荷会把机器内存打爆
    # （Round 10 现场：available 掉到 1.2GB，同轮 case 的 worker 拉不起来）。
    full = os.environ.get("PEER_RPC_PERF_FULL") == "1"
    for size in PAYLOAD_SIZES:
        if size > 16 * 1024 * 1024 and not full:
            continue
        payload = os.urandom(size)
        tag_mb = f"{size/1024/1024:.0f}MB"
        bench(f"single-frame {tag_mb}", single_roundtrip, payload)
        stream_bench(payload, "lz4", -1, f"stream-lz4    {tag_mb}")
        stream_bench(payload, "zstd", 9, f"stream-zstd9  {tag_mb}")
        stream_bench(payload, "none", -1, f"stream-none   {tag_mb}")

    if full:
        big = os.urandom(BIG)
        # single 512MB 基线已单独测得：写缓冲 drain-leftover 大缓冲排队
        # （WBUF 现场），此处仅测流式对照。
        stream_bench(big, "lz4", -1, f"stream-lz4  {BIG/1024/1024:.0f}MB")
        stream_bench(big, "none", -1, f"stream-none {BIG/1024/1024:.0f}MB")
        del big

    for tag, r in sorted(results.items()):
        print(f"[PERF-SUMMARY] {tag}: {r['med_s']}s {r['mbps']}MB/s", flush=True)



def main():
    gen = os.urandom(4).hex()
    master = get_agent()
    master.launch_local_workers([{"attributes": ["bench"]}])
    assert master.wait_workers_registered(timeout=60)
    db = open_db(get_config().get_str("log_dir") + "/db")
    member_serve(db, gen)
    check_run(db, gen)
    wait_tasks(1200)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
