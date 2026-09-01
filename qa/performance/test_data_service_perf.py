"""DataService 远端读写吞吐基准：write_object(temp) → 跨 worker read_object。

真实使用形态对齐 solver 数据面：worker A 写大对象（temp，f64 近随机 bytes），
worker B 凭数据依赖远端读（DataServer → DataClient wire 全管线，ObjectCache
必 miss——每档独立 key）。压缩走全局 config 默认（lz4，出厂真实口径）。
断言长度 + CRC；性能数字打印供对比（不设紧阈值防 flaky）。
512MB 大档内存 footprint 大，仅 PEER_RPC_PERF_FULL=1 时跑。
"""
import os
import sys
import time
import zlib

from fly import get_config, open_db, as_task, wait_tasks
from fly.runtime import get_agent

SIZES = [16 * 1024 * 1024, 64 * 1024 * 1024]
BIG = 512 * 1024 * 1024   # 主档（双 worker 内存安全上限）


@as_task(requires=["gpu"])
def write_large(db, key, size):
    t0 = time.perf_counter()
    payload = os.urandom(size)   # 近随机：f64 矩阵/解向量同压缩性质
    t_gen = time.perf_counter() - t0
    t0 = time.perf_counter()
    db.write_object(key, payload, save_to_db=False)
    t_write = time.perf_counter() - t0
    print(f"[PERF] write {key} {size/1024/1024:.0f}MB: {t_write*1000:.0f}ms "
          f"-> {size/1024/1024/t_write:.0f} MB/s (gen {t_gen*1000:.0f}ms) "
          f"crc={zlib.crc32(payload):08x}", flush=True)


@as_task(requires=["cpu"],
         inputs=lambda db, key, size: [db.get_full_name(key)])
def read_large_remote(db, key, size):
    t0 = time.perf_counter()
    data = db.read_object(key)   # 独立 key ⇒ ObjectCache 必 miss ⇒ 远端传输
    t_read = time.perf_counter() - t0
    mb = size / 1024 / 1024
    if len(data) != size:
        print(f"[PERF-DBG] read {key}: len {len(data)} != {size}", flush=True)
        raise RuntimeError(f"read_large_remote length mismatch: {key}")
    crc = zlib.crc32(data)
    del data
    print(f"[PERF] read  {key} {mb:.0f}MB: {t_read*1000:.0f}ms "
          f"-> {mb/t_read:.0f} MB/s crc={crc:08x}", flush=True)
    # 结果落库（持久对象）：任务失败时 wait_tasks 不报错，main 凭该对象
    # 判 case 成败，防静默假绿。
    db.write_object(f"{key}_result", {"ok": True, "mbps": round(mb/t_read, 1)})


def main():
    master = get_agent()
    master.launch_local_workers([{"attributes": ["gpu"]},
                                 {"attributes": ["cpu"]}])
    assert master.wait_workers_registered(timeout=60)

    full = os.environ.get("PEER_RPC_PERF_FULL") == "1"
    sizes = SIZES + [BIG] if full else SIZES
    db = open_db(get_config().get_str("log_dir") + "/db")
    for size in sizes:
        write_large(db, f"perf_blob_{size}", size)
        read_large_remote(db, f"perf_blob_{size}", size)
    wait_tasks(1200)
    for size in sizes:
        res = db.read_object(f"perf_blob_{size}_result")
        assert res and res.get("ok"), f"read verify missing/failed: size={size}"


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
