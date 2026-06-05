"""Distributed RAS (Restricted Additive Schwarz) solver for Fly.

Pattern:
- @as_task: submits task to Worker (no blocking)
- @wait_obj: Master-side blocking wait for specific data

Flow:
1. Master calls solve_ras() → submits setup task, then @wait_obj blocks for result
2. Setup builds matrix, partitions, writes data, submits iter-0 tasks
3. Each subdomain task reads from db, solves locally, writes result
4. Convergence check reads results, checks residual, submits next iter or finishes
5. When converged, "__ras__sol" written → Master's @wait_obj unblocks
"""
from fly import as_task, wait_obj


# ── Task: Setup ──

@as_task(inputs=lambda db, n, nsd, ov, rhs: [])
def ras_setup(db, n, num_subdomains, overlap, rhs):
    from _fly_solver import (
        ex_slv_build_poisson_2d, ex_slv_partition_1d,
        ex_slv_extract_subdomain_matrix,
    )

    size, _, rows, cols, vals = ex_slv_build_poisson_2d(n)
    db.write_object("__ras__A", {"size": size, "n": n,
                                  "rows": rows, "cols": cols, "values": vals})

    sds = ex_slv_partition_1d(n, num_subdomains, overlap)
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
        "n": n, "nsd": num_subdomains, "ov": overlap,
        "maxiter": 100, "tol": 1e-4,
    })
    db.write_object("__ras__x_0", [0.0] * size)

    for sd in sds:
        ras_sd_solve(db, sd.subdomain_id, 0)

    ras_check(db, 0, num_subdomains)


# ── Task: Subdomain Solve ──

@as_task(inputs=lambda db, sd_id, it: [])
def ras_sd_solve(db, sd_id, iteration):
    from _fly_solver import (EXSlvSubdomainSolver, EXSlvSubdomainInfo,
                              ex_slv_ras_subdomain_update)
    from fly import get_cache, put_cache, has_cache

    sd = db.read_object(f"__ras__sd_{sd_id}")
    A = db.read_object("__ras__A")
    x = db.read_object(f"__ras__x_{iteration}")
    b = [1.0] * A["size"]

    key = f"__ras__sv_{sd_id}"
    if not has_cache(key):
        solver = EXSlvSubdomainSolver.from_coo(
            sd["sz"], sd["lr"], sd["lc"], sd["lv"])
        put_cache(key, solver)
    solver = get_cache(key)

    info = EXSlvSubdomainInfo()
    info.subdomain_id = sd_id
    info.local_indices = sd["local"]
    info.own_indices = sd["own"]
    info.boundary_indices = sd["bnd"]

    x_new = ex_slv_ras_subdomain_update(
        A["size"], A["rows"], A["cols"], A["values"],
        b, x, info, solver)

    db.write_object(f"__ras__res_{sd_id}_{iteration}", {
        "id": sd_id, "it": iteration,
        "own": sd["own"], "x": x_new,
    })


# ── Task: Convergence Check ──

@as_task(inputs=lambda db, it, nsd: [])
def ras_check(db, iteration, num_subdomains):
    from _fly_solver import ex_slv_residual_norm

    cfg = db.read_object("__ras__cfg")
    A = db.read_object("__ras__A")
    size = A["size"]

    x = list(db.read_object(f"__ras__x_{iteration}"))
    for i in range(num_subdomains):
        r = db.read_object(f"__ras__res_{i}_{iteration}")
        for idx in r["own"]:
            x[idx] = r["x"][idx]

    b = [1.0] * size
    res = ex_slv_residual_norm(size, A["rows"], A["cols"], A["values"], x, b)
    nxt = iteration + 1

    if res < cfg["tol"] or nxt >= cfg["maxiter"]:
        db.write_object("__ras__sol", x)
        db.write_object("__ras__final_res", res)
        db.write_object("__ras__iters", nxt)
        db.write_object("__ras__ok", res < cfg["tol"])
        return

    db.write_object(f"__ras__x_{nxt}", x)

    for i in range(num_subdomains):
        ras_sd_solve(db, i, nxt)

    ras_check(db, nxt, num_subdomains)


# ── Master-side: wait for solution ──

@wait_obj(inputs=lambda db: [db.get_obj_name("__ras__sol")])
def get_ras_solution(db):
    """Block on Master until RAS solver writes final solution."""
    return {
        "x": db.read_object("__ras__sol"),
        "residual": db.read_object("__ras__final_res"),
        "iters": db.read_object("__ras__iters"),
        "converged": db.read_object("__ras__ok"),
    }


# ── Public API ──

def solve_ras(db, n, num_subdomains, overlap=1, rhs=None):
    """Start a distributed RAS solve and block until result is ready.

    Args:
        db: Fly database for storing intermediate data.
        n: Grid size (n x n grid → n*n x n*n matrix).
        num_subdomains: Number of subdomains (= number of workers).
        overlap: Overlap width in grid rows (default 1).
        rhs: RHS vector (default: all-ones for Poisson).

    Returns:
        dict with keys: x, residual, iters, converged
    """
    if rhs is None:
        rhs = [1.0] * (n * n)
    ras_setup(db, n, num_subdomains, overlap, rhs)
    return get_ras_solution(db)
