"""RAS benchmark with per-process resource monitoring."""
from _fly_log import INFO
import sys
import os
import shutil
import time
import threading
import subprocess

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras


def get_all_fly_pids():
    """Get all fly worker PIDs via ps."""
    try:
        out = subprocess.check_output(
            ["ps", "--no-headers", "-o", "pid,comm", "-L"],
            text=True, timeout=5)
        pids = []
        for line in out.strip().split('\n'):
            if not line.strip():
                continue
            parts = line.strip().split(None, 1)
            if len(parts) == 2 and 'fly' in parts[1]:
                pids.append(int(parts[0]))
        return pids
    except Exception:
        return []


class ResourceMonitor:
    def __init__(self, interval=3.0):
        self.interval = interval
        self.running = False
        self.thread = None
        self.hz = os.sysconf('SC_CLK_TCK')
        self.prev = {}  # pid -> (utime, stime, total_cpu)

    def _read_proc_stat(self, pid):
        try:
            with open(f'/proc/{pid}/stat') as f:
                fields = f.read().split()
            utime = int(fields[13])
            stime = int(fields[14])
            starttime = int(fields[21])
            return utime, stime, starttime
        except Exception:
            return None

    def _read_total_cpu(self):
        try:
            with open('/proc/stat') as f:
                line = f.readline()
            parts = line.split()[1:]
            return sum(int(x) for x in parts if x)
        except Exception:
            return 0

    def _read_rss(self, pid):
        try:
            with open(f'/proc/{pid}/status') as f:
                for line in f:
                    if line.startswith('VmRSS:'):
                        return int(line.split()[1])
        except Exception:
            return 0
        return 0

    def start(self):
        self.running = True
        self.prev_total = self._read_total_cpu()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)

    def _loop(self):
        while self.running:
            time.sleep(self.interval)
            if not self.running:
                break

            total_cpu = self._read_total_cpu()
            d_total = total_cpu - self.prev_total
            self.prev_total = total_cpu

            if d_total <= 0:
                continue

            my_pid = os.getpid()
            pids = get_all_fly_pids()
            parts = []
            for pid in sorted(pids):
                if pid == my_pid:
                    label = "master"
                else:
                    label = f"w{pid}"

                info = self._read_proc_stat(pid)
                if info is None:
                    continue
                utime, stime, starttime = info

                rss_mb = self._read_rss(pid) // 1024

                prev = self.prev.get(pid)
                if prev is not None:
                    d_proc = (utime - prev[0] + stime - prev[1])
                    cpu_pct = d_proc / d_total * 100.0
                else:
                    cpu_pct = -1.0

                self.prev[pid] = (utime, stime)

                cpu_str = f"{cpu_pct:5.1f}%" if cpu_pct >= 0 else "  init"
                parts.append(f"{label}={cpu_str} rss={rss_mb}MB")

            INFO(f"[RES] {' | '.join(parts)}")


# -- Main --

get_config().set_int("fail_unscheduleable_tasks", 1)

N = int(os.environ.get("BENCH_N", "1000"))
NSD = int(os.environ.get("BENCH_NSD", "4"))

INFO(f"=== RAS Profile: n={N} nsd={NSD} ===")

monitor = ResourceMonitor(interval=3.0)
monitor.start()

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"Failed to connect {NSD} workers"
INFO(f"Workers connected: {master.worker_count}")

db_path = f"/tmp/fly_bench_ras_profile_n{N}_nsd{NSD}"
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

db = open_db(db_path)

t0 = time.time()
result = solve_ras(db, N, NSD)
elapsed = time.time() - t0

monitor.stop()

INFO(f"=== RESULT: n={N} nsd={NSD} "
     f"iters={result['iters']} converged={result['converged']} "
     f"res={result['residual']:.2e} time={elapsed:.3f}s ===")

if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

master.stop()
INFO(f"[DONE] RAS profile complete")
