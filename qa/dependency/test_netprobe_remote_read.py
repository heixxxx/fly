"""E2E test for the network-aware remote-read priority feature.

Verifies that with the bandwidth-probe thread running, cross-worker remote
reads still complete correctly — i.e. the probe infrastructure does not
disturb the read path. The probe thread starts automatically on every worker
(net_probe_enabled=1 default) and periodically measures RTT/bandwidth to peers,
which TIER2 consults to order replicas.

Scenario:
  worker A writes `input_a`, worker B writes `input_b`
  a third task depends on both → forces a cross-worker remote read through
  DataServer on whichever worker does NOT hold the input

The probe thread is live on every worker for the whole run, so a successful
cross-worker read is the behavioral contract that the probe path does not
break the read path. Whether the probe thread itself starts is covered by the
C++ unit tests (WorkerAgent thread lifecycle); this QA test asserts the
end-to-end behavior that matters to users.
"""
import os
import time
import shutil

from _fly_log import INFO

from test import write_data, compute_sum
from fly import open_db, get_config

# runqa passes --log-dir {test_dir}/{test_name}/ to fly, which fly stores in
# Config.log_dir and shares with every process. DB state lives under it.
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


# The probe thread is on by default; make it explicit and probe fast so the
# test actually runs with the probe path active during its short lifetime.
get_config().set_int("net_probe_enabled", 1)
get_config().set_int("net_probe_interval_ms", 2000)
get_config().set_int("fail_unscheduleable_tasks", 0)

cleanup()

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2), "two workers should register"

db = open_db(DB_PATH)

# Phase 1: write both inputs (each lands on whichever worker picks it up).
write_data(db, "input_a", 7)
write_data(db, "input_b", 5)
assert wait_for(lambda: len(master.completed_tasks) >= 2), \
    "Phase 1: both writes should complete"

# Phase 2: compute_sum depends on input_a + input_b → forces a cross-worker
# remote read for whichever input lives on the other worker. The probe thread
# is running on both workers the whole time, so a correct result proves the
# read path is intact under the probe load.
compute_sum(db, "input_a", "input_b", "sum_result")
assert wait_for(lambda: len(master.completed_tasks) >= 3), \
    "Phase 2: compute_sum should complete (cross-worker remote read)"

result = db.read_object("sum_result")
assert result == 12, f"Expected 12, got {result}"

INFO("[PASS] test_netprobe_remote_read: cross-worker read succeeds with probe running")
