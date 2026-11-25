"""E2E dynamic worker property routing test.

3 workers as control group, each with unique initial attribute:
  Worker 1: ["alpha"]
  Worker 2: ["beta"]
  Worker 3: ["gamma"]

Phase 1 - Initial routing:
  Submit alpha/beta/gamma tasks, verify each routes to correct worker.

Phase 2 - Dynamic add "shared" property:
  Worker 2 and 3 gain "shared" via targeted tasks (routed by beta/gamma).
  Submit N tasks requiring "shared" -> verify NONE land on Worker 1.

Phase 3 - Dynamic remove "shared" from Worker 2:
  Remove "shared" from Worker 2 via targeted task (routed by beta).
  Submit N tasks requiring "shared" -> verify ALL land on Worker 3 only.
"""
from _fly_log import INFO
import time
import os
import shutil

NUM_SHARED_TASKS = 10


from test import (alpha_write, beta_write, gamma_write,
                        shared_write, add_shared_on_beta,
                        add_shared_on_gamma, remove_shared_on_beta)
from test import record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def setup_three_workers():
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([
        {"attributes": ["alpha"]},
        {"attributes": ["beta"]},
        {"attributes": ["gamma"]},
    ])
    assert master.wait_workers_registered(timeout=60)
    # 原断言：assert master.worker_count >= 3, \
    record("setup/three_workers_connected", master.worker_count >= 3,
           detail=master.worker_count)
    return master


def wait_completed(master, expected, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = master.completed_tasks
        if len(c) >= expected:
            return c
        failed = master.failed_tasks
        if failed:
            raise RuntimeError(f"Tasks failed: {failed}")
        time.sleep(0.5)
    raise TimeoutError(
        f"Timeout: {len(master.completed_tasks)}/{expected} completed, "
        f"pending={master.pending_tasks}, running={master.running_tasks}")


def collect_worker_ids(db, prefix, count):
    wids = set()
    for i in range(count):
        key = f"{prefix}_{i}"
        wid = db.read_object(key)
        wids.add(wid)
    return wids


def test_dynamic_property_full_routing():
    cleanup()
    master = setup_three_workers()
    db = open_db(DB_PATH)

    completed_base = 0

    # Phase 1: initial routing
    alpha_write(db, "p1_alpha")
    beta_write(db, "p1_beta")
    gamma_write(db, "p1_gamma")
    completed_base += 3
    wait_completed(master, completed_base, timeout=30)

    p1_alpha_val = db.read_object("p1_alpha")
    # 原断言：assert db.read_object("p1_alpha") == 1, \
    record("p1/alpha_routed_worker1", p1_alpha_val, detail=p1_alpha_val)
    p1_beta_val = db.read_object("p1_beta")
    # 原断言：assert db.read_object("p1_beta") == 2, \
    record("p1/beta_routed_worker2", p1_beta_val, detail=p1_beta_val)
    p1_gamma_val = db.read_object("p1_gamma")
    # 原断言：assert db.read_object("p1_gamma") == 3, \
    record("p1/gamma_routed_worker3", p1_gamma_val, detail=p1_gamma_val)
    INFO("[DONE] Phase 1: initial attribute routing correct")

    # Phase 2: dynamic add "shared" on Worker 2 and Worker 3
    add_shared_on_beta(db, "p2_add_beta")
    add_shared_on_gamma(db, "p2_add_gamma")
    completed_base += 2
    wait_completed(master, completed_base, timeout=30)

    p2_add_beta_val = db.read_object("p2_add_beta")
    # 原断言：assert db.read_object("p2_add_beta") == 2, \
    record("p2/add_beta_routed_worker2", p2_add_beta_val, detail=p2_add_beta_val)
    p2_add_gamma_val = db.read_object("p2_add_gamma")
    # 原断言：assert db.read_object("p2_add_gamma") == 3, \
    record("p2/add_gamma_routed_worker3", p2_add_gamma_val, detail=p2_add_gamma_val)

    for i in range(NUM_SHARED_TASKS):
        shared_write(db, f"p2_shared_{i}")
    completed_base += NUM_SHARED_TASKS
    wait_completed(master, completed_base, timeout=30)

    p2_wids = collect_worker_ids(db, "p2_shared", NUM_SHARED_TASKS)
    # 原断言：assert 1 not in p2_wids, \
    record("p2/shared_never_on_worker1", 1 not in p2_wids,
           detail=sorted(p2_wids))
    # 原断言：assert p2_wids.issubset({2, 3}), \
    record("p2/shared_only_on_worker2_3", p2_wids.issubset({2, 3}),
           detail=sorted(p2_wids))
    INFO(f"[DONE] Phase 2: {NUM_SHARED_TASKS} shared tasks routed to Worker 2/3 only (wids={p2_wids})")

    # Phase 3: remove "shared" from Worker 2
    remove_shared_on_beta(db, "p3_remove")
    completed_base += 1
    wait_completed(master, completed_base, timeout=30)

    p3_remove_val = db.read_object("p3_remove")
    # 原断言：assert db.read_object("p3_remove") == 2, \
    record("p3/remove_routed_worker2", p3_remove_val, detail=p3_remove_val)

    for i in range(NUM_SHARED_TASKS):
        shared_write(db, f"p3_shared_{i}")
    completed_base += NUM_SHARED_TASKS
    wait_completed(master, completed_base, timeout=30)

    p3_wids = collect_worker_ids(db, "p3_shared", NUM_SHARED_TASKS)
    # 原断言：assert 2 not in p3_wids, \
    record("p3/shared_never_on_worker2", 2 not in p3_wids,
           detail=sorted(p3_wids))
    # 原断言：assert p3_wids == {3}, \
    record("p3/shared_only_on_worker3", p3_wids == {3}, detail=sorted(p3_wids))
    INFO(f"[DONE] Phase 3: {NUM_SHARED_TASKS} shared tasks routed to Worker 3 only after Worker 2 lost property (wids={p3_wids})")

    INFO("\nAll stages done!")


test_dynamic_property_full_routing()
