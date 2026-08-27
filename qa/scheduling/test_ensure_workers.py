"""E2E: ensure_workers 基本语义（分配/简写/幂等/追加/exclude/静态预检）。

Worker: 5 个空属性 hybrid。
覆盖：
  1. 多元素申请（str 单属性简写 + list）全部生效且落点互不相同；
  2. requires task 真实调度到持有属性的 worker（端到端）；
  3. 幂等：同规格重复调用不改变任何属性、不新增 worker；
  4. 追加去重：目标必为已带属性的 worker——既有属性保留；
  5. exclude 正向（强制选干净 worker）+ 池空后静态预检立即失败
     （不消耗时限，明细含 need/have 数字）；
  6. 无 exclude 的总量预检：申请数 > 全部 worker → 立即失败。

属性命名用 qaew:（QA ensure workers）前缀避免与 solver 的 rasg: 混淆；
本 case 不涉及 SolveDb，直接字符串构造等价属性。
"""
import os
import shutil
import time

from _fly_log import INFO

from fly import as_task, ensure_workers, open_db, get_work_directory
from fly.runtime import get_agent

N_WORKERS = 5
DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def caps_by_worker(master):
    """worker_id -> set(capabilities)，hybrid 非宽限全集（idle+busy 并集）。"""
    result = {}
    agent = master._agent
    for wid in list(agent.get_idle_workers()) + list(agent.get_busy_workers()):
        if wid not in result:
            result[wid] = set(agent.get_worker_capabilities(wid))
    return result


def holders(caps_map, attr):
    return {wid for wid, caps in caps_map.items() if attr in caps}


master = get_agent()
master.launch_local_workers([{} for _ in range(N_WORKERS)])

# ── 0. launch 后不等待直接申请：已唤起未注册的占位符计入预检容量，注册
#      等待受声明的 timeout 约束（预检不因"还没注册完"误判池不足）。
ok = ensure_workers([
    "qaew:t:a",
    ["qaew:t:b", "qaew:t:shared"],
    "qaew:t:c",
    "qaew:t:d",
], timeout=30.0)
assert ok is True
assert master.wait_for_workers(N_WORKERS, timeout=30), "workers failed to connect"
INFO("[PASS] ensure right after launch (placeholder capacity)")

# ── 1. 基本申请：4 元素（含 str 简写与共享属性），全部生效且互不共址 ──
t0 = time.time()
ok = ensure_workers([
    "qaew:t:a",
    ["qaew:t:b", "qaew:t:shared"],
    "qaew:t:c",
    "qaew:t:d",
], timeout=30.0)
assert ok is True
elapsed = time.time() - t0
INFO(f"[PASS] basic ensure ok in {elapsed:.2f}s")

caps = caps_by_worker(master)
assert len(holders(caps, "qaew:t:a")) == 1, f"a 恰落一个 worker: {caps}"
a_holder = next(iter(holders(caps, "qaew:t:a")))
b_holder = next(iter(holders(caps, "qaew:t:b")))
c_holder = next(iter(holders(caps, "qaew:t:c")))
d_holder = next(iter(holders(caps, "qaew:t:d")))
holders_set = {a_holder, b_holder, c_holder, d_holder}
assert len(holders_set) == 4, \
    f"每个申请元素必须落在不同 worker（一个 worker 只分给一个元素）: {caps}"
assert {"qaew:t:b", "qaew:t:shared"} <= caps[b_holder], "list 元素多属性同落"


@as_task(requires=["qaew:t:a"])
def write_a(db):
    db.write_object("mark_a", 1)


@as_task(requires=["qaew:t:d"])
def write_d(db):
    db.write_object("mark_d", 2)


cleanup()
db = open_db(DB_PATH)

# 端到端：requires task 必须调度成功（无匹配属性则死等失败）
write_a(db)
write_d(db)
deadline = time.time() + 30
while len(master.completed_tasks) < 2:
    assert time.time() < deadline, \
        f"requires tasks must complete: failed={master.failed_tasks}"
    assert not master.failed_tasks, \
        f"unexpected failures: {master.get_task_error(master.failed_tasks[0])}"
    time.sleep(0.2)
INFO("[PASS] requires tasks scheduled onto ensured workers")

# ── 2. 幂等 + 零等待：同规格重复调用不改变属性、不加 worker、立即返回 ──
before = caps_by_worker(master)
count_before = master.worker_count
t0 = time.time()
ensure_workers(["qaew:t:a", ["qaew:t:b", "qaew:t:shared"], "qaew:t:c", "qaew:t:d"],
               timeout=10.0)
elapsed = time.time() - t0
after = caps_by_worker(master)
assert before == after, f"idempotent recall must not mutate attributes\n{before}\nvs\n{after}"
assert master.worker_count == count_before, "idempotent recall must not spawn workers"
assert elapsed < 0.5, \
    f"存活池已满足申请时必须零等待立即返回, took {elapsed:.2f}s"
INFO("[PASS] idempotent re-call (zero wait when satisfied)")

# ── 3. 追加去重：候选 5 选 1 的确定序（wid 升序）必落已带属性者——既有属性保留 ──
ensure_workers(["qaew:t:e"], timeout=10.0)
caps = caps_by_worker(master)
e_holders = holders(caps, "qaew:t:e")
assert len(e_holders) == 1
e_caps = caps[next(iter(e_holders))]
assert len(e_caps) >= 2, \
    f"追加不得覆盖既有属性（append 而非 replace）: {e_caps}"
INFO("[PASS] append semantics")

# ── 4a. exclude 正向：只剩 1 个干净 worker 时强制选中它 ──
clean_before = {wid for wid, c in caps.items()
                if not any(a.startswith("qaew:") for a in c)}
assert len(clean_before) == 1, f"应恰剩 1 个干净 worker（wid5）: {caps}"
ensure_workers(["qaew:t:f"], timeout=10.0, exclude=r"^qaew:")
caps = caps_by_worker(master)
f_holders = holders(caps, "qaew:t:f")
assert f_holders == clean_before, \
    f"exclude 后只能选干净 worker: f={f_holders} clean={clean_before}"

# ── 4b. exclude 排除后池空 → 静态预检立即失败（不等超时），明细含数字 ──
t0 = time.time()
try:
    ensure_workers(["qaew:t:z"], timeout=5.0, exclude=r"^qaew:")
    raise AssertionError("must raise when excluded pool is exhausted")
except RuntimeError as e:
    msg = str(e)
    assert "unsatisfied" in msg and "requested 1 worker(s)" in msg, f"bad detail: {msg}"
elapsed = time.time() - t0
assert elapsed < 2.0, f"静态预检必须立即失败（不消耗时限）, took {elapsed:.1f}s"
INFO("[PASS] exclude guard + fail-fast")

# ── 5. 无 exclude 的总量预检：申请 > 全部 worker → 立即失败 ──
t0 = time.time()
try:
    ensure_workers([f"qaew:p:{i}" for i in range(N_WORKERS + 1)], timeout=0.5)
    raise AssertionError("must raise when total pool < request")
except RuntimeError as e:
    assert "requested 6 worker(s)" in str(e), f"bad detail: {str(e)}"
elapsed = time.time() - t0
assert elapsed < 1.0, f"precheck must be immediate, took {elapsed:.1f}s"
INFO("[PASS] global capacity precheck")

master.stop()
INFO("[PASS] ensure_workers basic semantics")
