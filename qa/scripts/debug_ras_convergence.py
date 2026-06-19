"""Debug: compare converged RAS solution with exact for each subdomain."""
import sys, os

import math
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

N_SIDE = 6
NSD = 2

def build_poisson_2d(n):
    N = n * n
    diags = [4.0 * np.ones(N),
             -1.0 * np.ones(N - 1), -1.0 * np.ones(N - 1),
             -1.0 * np.ones(N - n), -1.0 * np.ones(N - n)]
    A = sparse.diags(diags, [0, 1, -1, n, -n], shape=(N, N), format='lil')
    for i in range(1, n):
        A[i * n - 1, i * n] = 0.0
        A[i * n, i * n - 1] = 0.0
    A_csc = A.tocsc()
    rows, cols, vals = [], [], []
    for k in range(A_csc.shape[1]):
        start, end = A_csc.indptr[k], A_csc.indptr[k + 1]
        for p in range(start, end):
            rows.append(int(A_csc.indices[p]))
            cols.append(int(k))
            vals.append(float(A_csc.data[p]))
    return N, rows, cols, vals

def _distribute(n, parts):
    bounds = [0]
    base = n // parts
    extra = n % parts
    pos = 0
    for i in range(parts):
        pos += base + (1 if i < extra else 0)
        bounds.append(pos)
    return bounds

from _fly_solver import (ex_slv_graph_expand_overlap,
                          ex_slv_extract_subdomain_matrix,
                          ex_slv_find_outside_connections,
                          EXSlvSubdomainSolver,
                          ex_slv_ras_bupdated_solve)

N, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
x_exact = splu(A_sp).solve(np.array(b))

# Simple 1D partition: 2 subdomains, first half and second half
primary_sets = [
    list(range(0, N//2)),   # nodes 0-17
    list(range(N//2, N)),   # nodes 18-35
]

global_owner = {}
for sd_id in range(NSD):
    for gidx in primary_sets[sd_id]:
        global_owner[gidx] = sd_id

depth = 2

setups = []
for sd_id in range(NSD):
    primary_nodes = primary_sets[sd_id]
    primary_set = set(primary_nodes)
    
    local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth)
    local_set = set(local_idx)
    
    own_idx = sorted(primary_set & local_set)
    own_offset = local_idx.index(own_idx[0]) if own_idx else 0
    own_size = len(own_idx)
    
    size, _, a_rows, a_cols, a_vals = ex_slv_extract_subdomain_matrix(
        N, rows, cols, vals, local_idx)
    out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
        N, rows, cols, vals, local_idx)
    b_local = [b[gidx] for gidx in local_idx]
    
    neighbor_needed = {}
    for i, gidx in enumerate(out_gidx):
        owner = global_owner.get(gidx, -1)
        if owner >= 0 and owner != sd_id:
            if owner not in neighbor_needed:
                neighbor_needed[owner] = []
            neighbor_needed[owner].append(i)
    
    neighbor_ids = sorted(neighbor_needed.keys())
    neighbor_recv_idx = {}
    for nb_id in neighbor_ids:
        nb_primary_map = {gidx: pos for pos, gidx in enumerate(primary_sets[nb_id])}
        recv_positions = []
        for conn_i in neighbor_needed[nb_id]:
            outside_gidx = out_gidx[conn_i]
            recv_positions.append(nb_primary_map.get(outside_gidx, -1))
        neighbor_recv_idx[nb_id] = recv_positions
    
    solver = EXSlvSubdomainSolver.from_coo(size, a_rows, a_cols, a_vals)
    
    INFO(f"sd={sd_id}: primary={len(primary_nodes)} extended={len(local_idx)} "
         f"own_offset={own_offset} own_size={own_size}")
    INFO(f"  own_idx[:5]={own_idx[:5]} primary_nodes[:5]={primary_nodes[:5]}")
    INFO(f"  own_idx==primary_nodes: {own_idx == primary_nodes}")
    INFO(f"  outside: {len(out_gidx)} connections")
    INFO(f"  neighbors: {neighbor_ids}")
    
    setups.append({
        "sd_id": sd_id,
        "local_idx": local_idx,
        "own_offset": own_offset,
        "own_size": own_size,
        "b_orig": b_local,
        "outside_local_pos": out_pos,
        "outside_global_idx": out_gidx,
        "outside_coeffs": out_coeffs,
        "neighbor_ids": neighbor_ids,
        "neighbor_recv_idx": neighbor_recv_idx,
        "neighbor_needed": neighbor_needed,
        "solver": solver,
    })

# Run with exact neighbor values — should give exact answer
INFO(f"\n=== Test with exact neighbor values ===")
for sd_id in range(NSD):
    s = setups[sd_id]
    neighbor_values = [x_exact[gidx] for gidx in s["outside_global_idx"]]
    x_local = ex_slv_ras_bupdated_solve(
        s["solver"], s["b_orig"],
        s["outside_local_pos"], s["outside_coeffs"],
        neighbor_values)
    x_primary = list(x_local[s["own_offset"]:s["own_offset"] + s["own_size"]])
    max_err = max(abs(x_primary[i] - x_exact[primary_sets[sd_id][i]])
                  for i in range(len(x_primary)))
    INFO(f"  sd={sd_id}: max_err vs exact={max_err:.2e}")

# Run RAS iteration (Jacobi: save all before update)
INFO(f"\n=== RAS iteration (Jacobi) ===")
x_primary = [None] * NSD

for step in range(50):
    new_x = [None] * NSD
    for sd_id in range(NSD):
        s = setups[sd_id]
        neighbor_values = [0.0] * len(s["outside_coeffs"])
        if step > 0:
            for nb_id in s["neighbor_ids"]:
                nb_x = x_primary[nb_id]
                recv_positions = s["neighbor_recv_idx"][nb_id]
                conn_indices = s["neighbor_needed"][nb_id]
                for i, conn_i in enumerate(conn_indices):
                    pos = recv_positions[i]
                    if pos >= 0:
                        neighbor_values[conn_i] = nb_x[pos]
        
        x_local = ex_slv_ras_bupdated_solve(
            s["solver"], s["b_orig"],
            s["outside_local_pos"], s["outside_coeffs"],
            neighbor_values)
        new_x[sd_id] = list(x_local[s["own_offset"]:s["own_offset"] + s["own_size"]])
    
    x_primary = new_x
    
    if step % 10 == 0 or step < 5:
        x_global = [0.0] * N
        for sd_id in range(NSD):
            for pos, gidx in enumerate(primary_sets[sd_id]):
                x_global[gidx] = x_primary[sd_id][pos]
        x_np = np.array(x_global)
        rel_err = np.linalg.norm(x_np - x_exact) / np.linalg.norm(x_exact)
        rel_res = np.linalg.norm(np.array(b) - A_sp @ x_np) / np.linalg.norm(b)
        max_delta = max(
            max(abs(a - b) for a, b in zip(x_primary[sd_id], prev_x[sd_id]))
            for sd_id in range(NSD)
        ) if step > 0 else float('inf')
        INFO(f"  step={step} rel_err={rel_err:.2e} rel_res={rel_res:.2e} "
             f"max_delta={max_delta:.2e}")
    
    prev_x = [list(x) for x in x_primary]

x_global = [0.0] * N
for sd_id in range(NSD):
    for pos, gidx in enumerate(primary_sets[sd_id]):
        x_global[gidx] = x_primary[sd_id][pos]
x_np = np.array(x_global)
rel_err = np.linalg.norm(x_np - x_exact) / np.linalg.norm(x_exact)
INFO(f"\nFinal rel_err={rel_err:.2e}")

# Now: what if we DON'T restrict — use full local solution?
INFO(f"\n=== Full local solution (no RAS restriction) at step 0 ===")
for sd_id in range(NSD):
    s = setups[sd_id]
    neighbor_values = [0.0] * len(s["outside_coeffs"])
    x_local = ex_slv_ras_bupdated_solve(
        s["solver"], s["b_orig"],
        s["outside_local_pos"], s["outside_coeffs"],
        neighbor_values)
    max_err = max(abs(x_local[i] - x_exact[s["local_idx"][i]])
                  for i in range(len(s["local_idx"])))
    max_err_primary = max(abs(x_local[s["own_offset"] + i] - x_exact[primary_sets[sd_id][i]])
                          for i in range(len(primary_sets[sd_id])))
    INFO(f"  sd={sd_id}: max_err_full_local={max_err:.2e} max_err_primary={max_err_primary:.2e}")

INFO(f"\n=== DONE ===")
