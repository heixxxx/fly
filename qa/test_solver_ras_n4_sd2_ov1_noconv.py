"""RAS solver non-convergence: n=4, subdomains=2, overlap=1, maxiter=3. Workers=2."""
from _fly_log import INFO
import sys
import os
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent
from _fly_solver import (ex_slv_build_poisson_2d, ex_slv_partition_1d,
                          ex_slv_extract_subdomain_matrix)
from solver.ras import ras_sd_solve, ras_check, get_ras_solution

N = 4
NSD = 2
OVERLAP = 1
DB_PATH = "/tmp/fly_e2e_solver_ras_n4_sd2_ov1_noconv_db"

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{}] * NSD)
assert master.wait_for_workers(NSD), f"workers should connect"

db = open_db(DB_PATH)

size, _, rows, cols, vals = ex_slv_build_poisson_2d(N)
db.write_object("__ras__A", {"size": size, "n": N,
                              "rows": rows, "cols": cols, "values": vals})
sds = ex_slv_partition_1d(N, NSD, OVERLAP)
for sd in sds:
    i = sd.subdomain_id
    _, _, lr, lc, lv = ex_slv_extract_subdomain_matrix(
        size, rows, cols, vals, sd.local_indices)
    db.write_object(f"__ras__sd_{i}", {
        "id": i,
        "local": list(sd.local_indices),
        "own": list(sd.own_indices),
        "bnd": list(sd.boundary_indices),
        "sz": len(sd.local_indices),
        "lr": lr, "lc": lc, "lv": lv,
    })
db.write_object("__ras__cfg", {
    "n": N, "nsd": NSD, "ov": OVERLAP,
    "maxiter": 3, "tol": 1e-4,
})
db.write_object("__ras__x_0", [0.0] * size)
for sd in sds:
    ras_sd_solve(db, sd.subdomain_id, 0)
ras_check(db, 0, NSD)

result = get_ras_solution(db)

assert not result["converged"], "Expected non-convergence"
assert result["iters"] == 3, f"Expected 3 iters, got {result['iters']}"
assert result["residual"] > 1e-4, f"Expected residual > 1e-4, got {result['residual']:.2e}"

INFO(f"NON-CONVERGE OK iters={result['iters']} res={result['residual']:.2e} workers={NSD}")

master.stop()
INFO(f"[PASS] test_solver_ras_n4_sd2_ov1_noconv")
