"""Performance: 数据 locality 调度收益量化（增强前后对比）。

构造场景：3 worker 各持有一个大对象，提交 3 个 consume task 各依赖一个对象。
  - locality off: consume 按 worker_id 升序调度，多数跨网络拉取大对象（慢）
  - locality on:  consume 调度到持有者 worker，零跨网络拉取（快）

强制 OFF 劣化的设计：
  每个 worker 带唯一属性 seed_i，seed task requires=["seed_i"] 强制 payload_i 落 worker i。
  consume task 无 capability（纯按调度策略选 worker）。
  - OFF：consume 提交时所有 worker idle，按 worker_id 升序选 worker 1/2/3。
        consume_i 依赖 payload_{N-1-i}（反向依赖），与 holder 错开 → 多数跨网络。
  - ON：consume 调度到各自依赖的 holder（全部命中）。

high cache 预热放大差异：
  seed task 写入后立即用 cache="high" 读一次，预热本 worker 的 high tier cache。
  consume 用 cache="high" 读：
  - ON（落持有者 worker）：high cache 命中（seed 预热过），零反序列化
  - OFF（落非持有者 worker）：high cache 未命中 → 跨网络拉取 + 解压 + 反序列化

指标：
  - local_hits：consume 落在 holder 的次数（确定性亲和度指标，硬断言）
  - wall_clock：consume 阶段总耗时（报告指标；含反序列化差异）

数据由独立构造，不依赖 solver。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_config

N_WORKERS = 3
PAYLOAD_SIZE = 2_000_000   # ~16MB per object
ROUNDS = 3                 # 重复轮次取最小 wall_clock，降噪
READ_TIMES = 10            # consume 内重复读次数，放大反序列化成本差异
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup(db_path=None):
    target = db_path or DB_PATH
    if os.path.isdir(target):
        shutil.rmtree(target, ignore_errors=True)


def wait_completed(master, expected, timeout=180):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            return True
        time.sleep(0.2)
    return False


@as_task(requires=lambda db, idx, size: [f"seed_{idx}"])
def seed_payload(db, idx, size):
    """requires seed_idx → 强制落 worker idx。写 payload + 自报 worker_id。
    写入后用 cache="high" 读一次，预热本 worker 的 high tier cache（省后续反序列化）。"""
    from e2e_tasks import _get_wid
    db.write_object(f"payload_{idx}", list(range(size)))
    db.read_object(f"payload_{idx}", cache="high")  # 预热 high cache
    db.write_object(f"seed_worker_{idx}", _get_wid())


@as_task(inputs=lambda db, payload_idx, consume_id: [db.get_full_name(f"payload_{payload_idx}")])
def consume_payload(db, payload_idx, consume_id):
    """依赖 payload，用 cache="high" 读取 N 次，自报执行 worker + 实际读取耗时。
    无 capability，纯测 locality。task 内自测 read 耗时（排除调度/提交开销）。"""
    from e2e_tasks import _get_wid
    import time as _time
    t0 = _time.monotonic()
    for _ in range(READ_TIMES):
        db.read_object(f"payload_{payload_idx}", cache="high")
    read_elapsed = _time.monotonic() - t0
    db.write_object(f"consume_worker_{consume_id}", _get_wid())
    # 写入实际读取耗时（毫秒），用于精确对比 ON/OFF
    db.write_object(f"consume_read_ms_{consume_id}", int(read_elapsed * 1000))


def run_round(locality_on, round_idx):
    """跑一轮，返回 (wall_clock, local_hits, total_consumes)。"""
    # 每轮用独立 db_path，避免跨轮 DataService local_idx/remote_idx 残留导致
    # db.read_object 读到旧值（holders 错位 → local_hits 误判）。
    round_db_path = f"{DB_PATH}_r{round_idx}_{'on' if locality_on else 'off'}"
    cleanup(round_db_path)
    get_config().set_int("locality_scheduling_enabled", 1 if locality_on else 0)

    from fly.runtime import get_agent
    master = get_agent()
    worker_attrs = [{"attributes": [f"seed_{i}"]} for i in range(N_WORKERS)]
    master.launch_local_workers(worker_attrs)
    for _ in range(60):
        if master.worker_count >= N_WORKERS:
            break
        time.sleep(0.5)
    assert master.worker_count >= N_WORKERS, f"workers {master.worker_count}/{N_WORKERS}"

    db = open_db(round_db_path)

    # Phase 1: seed —— payload_i 落 worker i（requires seed_i），预热 high cache
    for i in range(N_WORKERS):
        seed_payload(db, i, PAYLOAD_SIZE)
    assert wait_completed(master, N_WORKERS, 180), "seed timeout"

    holders = {i: int(db.read_object(f"seed_worker_{i}")) for i in range(N_WORKERS)}
    INFO(f"[PERF r{round_idx} locality={'ON' if locality_on else 'OFF'}] holders={holders}")

    # Phase 2: consume —— 依赖顺序与 holder 反向（consume_i 依赖 payload_{N-1-i}）
    consume_specs = []
    cid = 0
    for i in range(N_WORKERS):
        pidx = N_WORKERS - 1 - i
        consume_specs.append((pidx, cid))
        cid += 1

    t1 = time.time()
    for pidx, c in consume_specs:
        consume_payload(db, pidx, c)
    assert wait_completed(master, N_WORKERS + len(consume_specs), 180), "consume timeout"
    wall = time.time() - t1

    local_hits = 0
    total_read_ms = 0
    for pidx, c in consume_specs:
        cw = int(db.read_object(f"consume_worker_{c}"))
        if cw == holders[pidx]:
            local_hits += 1
        # 读取该 consume 的实际 read 耗时（排除调度开销）
        total_read_ms += int(db.read_object(f"consume_read_ms_{c}"))

    master.stop()
    return wall, local_hits, len(consume_specs), total_read_ms


off_walls, on_walls = [], []
off_hits_total, on_hits_total = 0, 0
off_consumes_total, on_consumes_total = 0, 0
off_read_ms, on_read_ms = [], []

for r in range(ROUNDS):
    w, h, t, rms = run_round(locality_on=False, round_idx=r)
    off_walls.append(w); off_hits_total += h; off_consumes_total += t; off_read_ms.append(rms)
    w, h, t, rms = run_round(locality_on=True, round_idx=r)
    on_walls.append(w); on_hits_total += h; on_consumes_total += t; on_read_ms.append(rms)

off_wall_min = min(off_walls)
on_wall_min = min(on_walls)
off_read_min = min(off_read_ms)  # 单轮总 read 毫秒数
on_read_min = min(on_read_ms)
off_hit_rate = off_hits_total / off_consumes_total if off_consumes_total else 0
on_hit_rate = on_hits_total / on_consumes_total if on_consumes_total else 0

INFO("=" * 64)
INFO("[PERF RESULT] {} rounds, {} workers, {}MB/object, {} consumes/round".format(
    ROUNDS, N_WORKERS, PAYLOAD_SIZE * 8 // 1024 // 1024, N_WORKERS))
INFO("  locality OFF: wall_min={:.2f}s read_min={}ms local_hits={}/{} ({:.0%})".format(
    off_wall_min, off_read_min, off_hits_total, off_consumes_total, off_hit_rate))
INFO("  locality ON:  wall_min={:.2f}s read_min={}ms local_hits={}/{} ({:.0%})".format(
    on_wall_min, on_read_min, on_hits_total, on_consumes_total, on_hit_rate))
read_speedup = (off_read_min - on_read_min) / off_read_min * 100 if off_read_min > 0 else 0
INFO("  improvement:  read {}→{}ms ({:+.1f}%), hit_rate {:.0%}→{:.0%}".format(
    off_read_min, on_read_min, read_speedup, off_hit_rate, on_hit_rate))

# 硬断言
assert on_hit_rate == 1.0, \
    f"locality ON should hit all holders: {on_hits_total}/{on_consumes_total}"

assert off_hit_rate < 1.0, \
    f"locality OFF should miss some holders (reverse-dependency design): " \
    f"{off_hits_total}/{off_consumes_total}"

# ON 应不劣于 OFF（含 high cache 预热，ON 省 67% 的反序列化+跨网络）
assert on_wall_min <= off_wall_min * 1.05, \
    f"locality ON ({on_wall_min:.2f}s) should not be slower than OFF ({off_wall_min:.2f}s)"

INFO("[PASS] locality perf verified: ON {:.0%} hits vs OFF {:.0%}, read {}→{}ms".format(
    on_hit_rate, off_hit_rate, off_read_min, on_read_min))
