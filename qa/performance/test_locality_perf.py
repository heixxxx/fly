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

指标：
  - local_hits：consume 落在 holder 的次数（确定性亲和度指标，硬断言）
  - wall_clock：consume 阶段总耗时（报告指标；单机回环网络下传输成本被 ObjectCache +
    计算开销掩盖，差异主要在跨机网络环境显现，故仅作参考不作硬断言）

数据由独立构造，不依赖 solver。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_config

N_WORKERS = 3
PAYLOAD_SIZE = 4_000_000   # ~32MB per object，使跨网络传输成本显著
ROUNDS = 3                 # 重复轮次取最小 wall_clock，降噪
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_completed(master, expected, timeout=180):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            return True
        time.sleep(0.2)
    return False


@as_task(requires=lambda db, idx, size: [f"seed_{idx}"])
def seed_payload(db, idx, size):
    """requires seed_idx → 强制落 worker idx。写 payload + 自报 worker_id。"""
    from e2e_tasks import _get_wid
    db.write_object(f"payload_{idx}", list(range(size)))
    db.write_object(f"seed_worker_{idx}", _get_wid())


@as_task(inputs=lambda db, payload_idx, consume_id: [db.get_full_name(f"payload_{payload_idx}")])
def consume_payload(db, payload_idx, consume_id):
    """依赖 payload，自报执行 worker。无 capability，纯测 locality。
    - ON（落持有者 worker）：本地 low cache 命中，省跨网络传输
    - OFF（落非持有者 worker）：跨网络拉取（TIER2）+ 解压"""
    from e2e_tasks import _get_wid
    db.read_object(f"payload_{payload_idx}")
    db.write_object(f"consume_worker_{consume_id}", _get_wid())


def run_round(locality_on, round_idx):
    """跑一轮，返回 (wall_clock, local_hits, total_consumes)。"""
    cleanup()
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

    db = open_db(DB_PATH)

    # Phase 1: seed —— payload_i 落 worker i（requires seed_i）
    for i in range(N_WORKERS):
        seed_payload(db, i, PAYLOAD_SIZE)
    assert wait_completed(master, N_WORKERS, 180), "seed timeout"

    holders = {i: int(db.read_object(f"seed_worker_{i}")) for i in range(N_WORKERS)}
    INFO(f"[PERF r{round_idx} locality={'ON' if locality_on else 'OFF'}] holders={holders}")

    # Phase 2: consume —— 依赖顺序与 holder 反向（consume_i 依赖 payload_{N-1-i}）
    # OFF 时按 worker_id 升序调度，与反向 holder 错开 → 多数跨网络
    consume_specs = []
    cid = 0
    for i in range(N_WORKERS):
        pidx = N_WORKERS - 1 - i  # 反向依赖：consume_i 依赖 payload_{N-1-i}
        consume_specs.append((pidx, cid))
        cid += 1

    t1 = time.time()
    for pidx, c in consume_specs:
        consume_payload(db, pidx, c)
    assert wait_completed(master, N_WORKERS + len(consume_specs), 180), "consume timeout"
    wall = time.time() - t1

    # 统计 local_hits：consume_worker_cid == 其依赖 payload 的 holder
    local_hits = 0
    for pidx, c in consume_specs:
        cw = int(db.read_object(f"consume_worker_{c}"))
        if cw == holders[pidx]:
            local_hits += 1

    master.stop()
    return wall, local_hits, len(consume_specs)


# 跑 ROUNDS 轮，每轮 OFF/ON 各一次
off_walls, on_walls = [], []
off_hits_total, on_hits_total = 0, 0
off_consumes_total, on_consumes_total = 0, 0

for r in range(ROUNDS):
    w, h, t = run_round(locality_on=False, round_idx=r)
    off_walls.append(w); off_hits_total += h; off_consumes_total += t
    w, h, t = run_round(locality_on=True, round_idx=r)
    on_walls.append(w); on_hits_total += h; on_consumes_total += t

# 汇总
off_wall_min = min(off_walls)
on_wall_min = min(on_walls)
off_hit_rate = off_hits_total / off_consumes_total if off_consumes_total else 0
on_hit_rate = on_hits_total / on_consumes_total if on_consumes_total else 0

INFO("=" * 64)
INFO("[PERF RESULT] {} rounds, {} workers, {}MB/object, {} consumes/round".format(
    ROUNDS, N_WORKERS, PAYLOAD_SIZE * 8 // 1024 // 1024, N_WORKERS))
INFO("  locality OFF: wall_min={:.2f}s local_hits={}/{} ({:.0%})".format(
    off_wall_min, off_hits_total, off_consumes_total, off_hit_rate))
INFO("  locality ON:  wall_min={:.2f}s local_hits={}/{} ({:.0%})".format(
    on_wall_min, on_hits_total, on_consumes_total, on_hit_rate))
speedup = (off_wall_min - on_wall_min) / off_wall_min * 100 if off_wall_min > 0 else 0
INFO("  improvement:  wall {:.2f}→{:.2f}s ({:+.1f}%), hit_rate {:.0%}→{:.0%}".format(
    off_wall_min, on_wall_min, speedup, off_hit_rate, on_hit_rate))

# 硬断言（local_hits 是确定性亲和度指标，wall_clock 在单机回环下噪声大仅作报告）
# 1. ON 的 local_hit_rate 必须 == 100%（所有 consume 落 holder）
assert on_hit_rate == 1.0, \
    f"locality ON should hit all holders: {on_hits_total}/{on_consumes_total}"

# 2. OFF 的 local_hit_rate 必须 < 100%（反向依赖确保错开，否则场景无区分度）
assert off_hit_rate < 1.0, \
    f"locality OFF should miss some holders (reverse-dependency design): " \
    f"{off_hits_total}/{off_consumes_total}. 场景无区分度，检查 seed/consume 顺序"

# 3. ON 的 wall_clock 不应显著劣于 OFF（容忍 ±10% 噪声；单机回环网络下传输成本被
#    ObjectCache + 计算开销掩盖，wall 差异主要在跨机网络环境显现）
wall_ratio = on_wall_min / off_wall_min if off_wall_min > 0 else 1.0
assert wall_ratio <= 1.10, \
    f"locality ON ({on_wall_min:.2f}s) should not be >10% slower than OFF ({off_wall_min:.2f}s)"

INFO("[PASS] locality perf verified: ON achieves {:.0%} local hits vs OFF {:.0%}, "
     "wall {:.2f}s→{:.2f}s".format(on_hit_rate, off_hit_rate, off_wall_min, on_wall_min))
