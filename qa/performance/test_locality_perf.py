"""Performance: data locality 调度收益量化。

构造场景：N 个 worker 各持有一个大对象，提交 N 个 task 各依赖一个对象。
报告 locality ON 时的 local_hits（consume 落在持有者 worker 的次数）+ wall_clock。

locality 功能正确性已由 test_locality_basic.py 验证（OFF 不命中、ON 命中）。
本测试聚焦量化：在多 worker 并发场景下，locality ON 能否让多数 consume 本地命中，
以及 wall_clock 表现。

数据由独立构造，不依赖 solver。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_config

N_WORKERS = 3
PAYLOAD_SIZE = 3_000_000  # ~24MB per object
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_completed(master, expected, timeout=180):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            return time.time() - t0
        time.sleep(0.3)
    return time.time() - t0


@as_task(requires=lambda db, idx, size: [f"seed_{idx}"])
def seed_payload(db, idx, size):
    from e2e_tasks import _get_wid
    db.write_object(f"payload_{idx}", list(range(size)))
    db.write_object(f"seed_worker_{idx}", _get_wid())


@as_task(inputs=lambda db, payload_idx, consume_id: [db.get_full_name(f"payload_{payload_idx}")])
def consume_payload(db, payload_idx, consume_id):
    from e2e_tasks import _get_wid
    db.read_object(f"payload_{payload_idx}")
    db.write_object(f"consume_worker_{consume_id}", _get_wid())


cleanup()
# 启用 data locality 调度
get_config().set_int("locality_scheduling_enabled", 1)

from fly.runtime import get_agent
master = get_agent()
worker_attrs = [{"attributes": [f"seed_{i}"]} for i in range(N_WORKERS)]
master.launch_local_workers(worker_attrs)
for _ in range(60):
    if master.worker_count >= N_WORKERS:
        break
    time.sleep(0.5)
assert master.worker_count >= N_WORKERS, \
    f"Only {master.worker_count}/{N_WORKERS} workers connected"

db = open_db(DB_PATH)

# Phase 1: N 个 seed task 写大对象（各落唯一 worker）。
for i in range(N_WORKERS):
    seed_payload(db, i, PAYLOAD_SIZE)
wait_completed(master, N_WORKERS, timeout=180)

holders = {}
for i in range(N_WORKERS):
    holders[i] = int(db.read_object(f"seed_worker_{i}"))
INFO(f"[PERF] holders={holders}")

# Phase 2: N 个 consume task 各依赖一个 payload（1:1，保证 holder 调度时 idle）。
t1 = time.time()
for i in range(N_WORKERS):
    consume_payload(db, i, i)
wall = wait_completed(master, N_WORKERS + N_WORKERS, timeout=180)

# 统计 local_hits
local_hits = 0
for i in range(N_WORKERS):
    consume_w = int(db.read_object(f"consume_worker_{i}"))
    if consume_w == holders[i]:
        local_hits += 1
    INFO(f"[PERF] consume_{i} ran on worker {consume_w}, holder={holders[i]}, "
         f"{'LOCAL' if consume_w == holders[i] else 'REMOTE'}")

master.stop()
INFO(f"[PERF RESULT] wall_consume={wall:.2f}s local_hits={local_hits}/{N_WORKERS}")

# 硬断言：locality ON 时所有 consume 应命中 holder（1:1 场景，holder 调度时 idle）。
assert local_hits == N_WORKERS, \
    f"locality ON should schedule all {N_WORKERS} consumes to holders: {local_hits}/{N_WORKERS}"

INFO("[PASS] locality perf: all consumes scheduled to data-holding workers (zero remote transfer)")
