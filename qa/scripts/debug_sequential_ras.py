"""Sequential RAS solver test — no distributed tasks, pure computation loop.
Isolates the algorithm from task scheduling issues."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

import math
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

N_SIDE = 20
NSD = 4

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

def _factor_nsd(nsd):
    best_x, best_y = 1, nsd
    best_diff = nsd
    for x in range(1, int(math.sqrt(nsd)) + 1):
        if nsd % x == 0:
            y = nsd // x
            diff = abs(y - x)
            if diff < best_diff:
                best_x, best_y = x, y
                best_diff = diff
    return best_x, best_y

def _distribute(n, parts):
    bounds = [0]
    base = n // parts
    extra = n % parts
    pos = 0
    for i in range(parts):
        pos += base + (1 if i < extra else 0)
        bounds.append(pos)
    return bounds

def _partition_primary_2d(n, nsd):
    nsd_x, nsd_y = _factor_nsd(nsd)
    row_bounds = _distribute(n, nsd_x)
    col_bounds = _distribute(n, nsd_y)
    primary_sets = []
    for ix in range(nsd_x):
        for iy in range(nsd_y):
            nodes = []
            for r in range(row_bounds[ix], row_bounds[ix + 1]):
                for c in range(col_bounds[iy], col_bounds[iy + 1]):
                    nodes.append(r * n + c)
            primary_sets.append(nodes)
    return primary_sets, nsd_x, nsd_y

def _estimate_depth(n, nsd_x, nsd_y, overlap_ratio):
    L = min(n // nsd_x, n // nsd_y)
    return max(2, math.ceil(overlap_ratio * L / 2))

from _fly_solver import (ex_slv_graph_expand_overlap,
                          ex_slv_extract_subdomain_matrix,
                          ex_slv_find_outside_connections,
                          EXSlvSubdomainSolver,
                          ex_slv_ras_bupdated_solve)

N, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N
primary_sets, nsd_x, nsd_y = _partition_primary_2d(N_SIDE, NSD)

global_owner = {}
for sd_id in range(NSD):
    for gidx in primary_sets[sd_id]:
        global_owner[gidx] = sd_id

overlap_ratio = 0.30
depth = _estimate_depth(N_SIDE, nsd_x, nsd_y, overlap_ratio)
INFO(f"n={N_SIDE} nsd={NSD} depth={depth} overlap_ratio={overlap_ratio}")

# Setup all subdomains
setups = []
for sd_id in range(NSD):
    primary_nodes = primary_sets[sd_id]
    primary_set = set(primary_nodes)
    
    local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth)
    ratio = len(local_idx) / len(primary_nodes)
    if ratio < 1 + overlap_ratio:
        local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth * 2)
        ratio = len(local_idx) / len(primary_nodes)
    
    own_idx = sorted(primary_set & set(local_idx))
    own_offset = local_idx.index(own_idx[0]) if own_idx else 0
    own_size = len(own_idx)
    
    size, _, a_rows, a_cols, a_vals = ex_slv_extract_subdomain_matrix(
        N, rows, cols, vals, local_idx)
    out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
        N, rows, cols, vals, local_idx)
    b_local = [b[gidx] for gidx in local_idx]
    
    # Neighbor mapping
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
    INFO(f"  sd={sd_id}: primary={len(primary_nodes)} extended={len(local_idx)} "
         f"ratio={ratio:.2f}x own_offset={own_offset} own_size={own_size}")

# Exact solution
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
x_exact = splu(A_sp).solve(np.array(b))

# Run iteration loop
MAX_ITER = 200
TOL = 1e-8
x_primary = [None] * NSD  # current primary solution per subdomain

for step in range(MAX_ITER):
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
        x_primary[sd_id] = list(x_local[s["own_offset"]:s["own_offset"] + s["own_size"]])
    
    # Check convergence
    if step > 0:
        # Assemble global solution
        x_global = [0.0] * N
        for sd_id in range(NSD):
            for pos, gidx in enumerate(primary_sets[sd_id]):
                x_global[gidx] = x_primary[sd_id][pos]
        
        x_np = np.array(x_global)
        rel_error = np.linalg.norm(x_np - x_exact) / np.linalg.norm(x_exact)
        max_delta = max(
            max(abs(a - b) for a, b in zip(x_primary[sd_id], prev_x[sd_id]))
            for sd_id in range(NSD)
        )
        
        if step % 10 == 0 or step < 5:
            INFO(f"  step={step} max_delta={max_delta:.2e} rel_err={rel_error:.2e}")
        
        if max_delta < TOL:
            INFO(f"  CONVERGED at step {step}")
            x_final = x_np
            break
        
        if rel_error > 1.0 and step > 10:
            INFO(f"  DIVERGING at step {step} rel_err={rel_error:.2e}")
            break
    
    prev_x = [list(x) for x in x_primary]
else:
    x_global = [0.0] * N
    for sd_id in range(NSD):
        for pos, gidx in enumerate(primary_sets[sd_id]):
            x_global[gidx] = x_primary[sd_id][pos]
    x_final = np.array(x_global)
    INFO(f"  MAX ITER reached")

rel_error = np.linalg.norm(x_final - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_final - x_exact))
rel_res = np.linalg.norm(np.array(b) - A_sp @ x_final) / np.linalg.norm(b)

INFO(f"FINAL: rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e}")
INFO(f"[{'PASS' if rel_error < 1e-4 else 'FAIL'}] sequential RAS test")
