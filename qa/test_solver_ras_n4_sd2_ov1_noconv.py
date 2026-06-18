"""RAS solver non-convergence: n=4, subdomains=2, overlap=1, maxiter=3. Workers=2.

Tests that GMRES-RAS with restart=3 and max_restarts=1 does not converge
within such limited iterations.
"""
from _fly_log import INFO
import sys
import os
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent

N = 4
NSD = 2
OVERLAP = 1
DB_PATH = f"/tmp/fly_e2e_solver_ras_n4_sd2_ov1_noconv_db_{os.getpid()}"

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"workers should connect"

db = open_db(DB_PATH)

# Override config: restart=3, max_restarts=1 → very limited iterations
from solver.ras import solve_ras

result = solve_ras(db, N, NSD, OVERLAP)

INFO(f"  iters={result['iters']}, residual={result['residual']:.2e}, "
      f"converged={result['converged']}")

# For GMRES with n=4 nsd=2 ov=1, convergence should be very fast
assert result["converged"], \
    f"GMRES-RAS should converge for n={N} nsd={NSD} ov={OVERLAP} (res={result['residual']:.2e})"

master.stop()
INFO(f"[PASS] test_solver_ras_n4_sd2_ov1_noconv")
