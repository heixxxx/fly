"""Quick test: n=20 with different nsd configs to isolate the bug."""
import sys, os

import math
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

N_SIDE = 20

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

def _partition_primary_1d(n, nsd):
    bounds = _distribute(n * n, nsd)
    primary_sets = []
    for i in range(nsd):
        primary_sets.append(list(range(bounds[i], bounds[i + 1])))
    return primary_sets

from _fly_solver import (ex_slv_graph_expand_overlap,
                          ex_slv_extract_subdomain_matrix,
                          ex_slv_find_outside_connections,
                          EXSlvSubdomainSolver,
                          ex_slv_ras_bupdated_solve)

N, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
x_exact = splu(A_sp).solve(np.array(b))

def run_ras_test(label, primary_sets, depth):
    nsd = len(primary_sets)
    global_owner = {}
    for sd_id in range(nsd):
        for gidx in primary_sets[sd_id]:
            global_owner[gidx] = sd_id
    
    setups = []
    for sd_id in range(nsd):
        primary_nodes = primary_sets[sd_id]
        primary_set = set(primary_nodes)
        
        local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth)
        local_idx_map = {gidx: pos for pos, gidx in enumerate(local_idx)}
        primary_local_pos = [local_idx_map[gidx] for gidx in primary_nodes]
        
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
        setups.append({
            "local_idx": local_idx,
            "primary_local_pos": primary_local_pos,
            "b_orig": b_local,
            "outside_local_pos": out_pos,
            "outside_global_idx": out_gidx,
            "outside_coeffs": out_coeffs,
            "neighbor_ids": neighbor_ids,
            "neighbor_recv_idx": neighbor_recv_idx,
            "neighbor_needed": neighbor_needed,
            "solver": solver,
        })
    
    x_primary = [None] * nsd
    for step in range(200):
        new_x = [None] * nsd
        for sd_id in range(nsd):
            s = setups[sd_id]
            nv = [0.0] * len(s["outside_coeffs"])
            if step > 0:
                for nb_id in s["neighbor_ids"]:
                    nb_x = x_primary[nb_id]
                    recv_pos = s["neighbor_recv_idx"][nb_id]
                    conn_idx = s["neighbor_needed"][nb_id]
                    for i, ci in enumerate(conn_idx):
                        p = recv_pos[i]
                        if p >= 0:
                            nv[ci] = nb_x[p]
            
            xl = ex_slv_ras_bupdated_solve(
                s["solver"], s["b_orig"],
                s["outside_local_pos"], s["outside_coeffs"], nv)
            new_x[sd_id] = [xl[pos] for pos in s["primary_local_pos"]]
        
        x_primary = new_x
        
        if step > 0 and step % 20 == 0:
            xg = [0.0] * N
            for sid in range(nsd):
                for pos, gidx in enumerate(primary_sets[sid]):
                    xg[gidx] = x_primary[sid][pos]
            re = np.linalg.norm(np.array(xg) - x_exact) / np.linalg.norm(x_exact)
            md = max(max(abs(a-b) for a,b in zip(x_primary[sid], prev_x[sid])) for sid in range(nsd))
            if md < 1e-10 or re < 1e-12:
                break
        prev_x = [list(x) for x in x_primary]
    
    xg = [0.0] * N
    for sid in range(nsd):
        for pos, gidx in enumerate(primary_sets[sid]):
            xg[gidx] = x_primary[sid][pos]
    re = np.linalg.norm(np.array(xg) - x_exact) / np.linalg.norm(x_exact)
    rr = np.linalg.norm(np.array(b) - A_sp @ np.array(xg)) / np.linalg.norm(b)
    INFO(f"{label}: rel_err={re:.2e} rel_res={rr:.2e} steps={step+1}")

# Test 1: nsd=2, 1D split (first half / second half)
psets_1d_2 = _partition_primary_1d(N_SIDE, 2)
run_ras_test("nsd=2 1D depth=3", psets_1d_2, 3)
run_ras_test("nsd=2 1D depth=5", psets_1d_2, 5)

# Test 2: nsd=4, 1D split
psets_1d_4 = _partition_primary_1d(N_SIDE, 4)
run_ras_test("nsd=4 1D depth=3", psets_1d_4, 3)
run_ras_test("nsd=4 1D depth=5", psets_1d_4, 5)

# Test 3: nsd=4, 2D split (2x2)
psets_2d_4, nsd_x, nsd_y = _partition_primary_2d(N_SIDE, 4)
d = _estimate_depth(N_SIDE, nsd_x, nsd_y, 0.30)
run_ras_test(f"nsd=4 2D depth={d}", psets_2d_4, d)
run_ras_test("nsd=4 2D depth=3", psets_2d_4, 3)
run_ras_test("nsd=4 2D depth=5", psets_2d_4, 5)

# Test 4: nsd=4, 2D split with larger depth
run_ras_test("nsd=4 2D depth=10", psets_2d_4, 10)

INFO("\n=== DONE ===")
