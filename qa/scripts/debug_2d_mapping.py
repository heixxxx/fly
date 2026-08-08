"""Debug: trace 2D partition neighbor mapping in detail.
Compare with 1D partition to find the difference."""
import sys, os

import math
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO, DBG

N_SIDE = 10

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

def _partition_primary_2d(n, nsd):
    nsd_x, nsd_y = 2, 2
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

primary_sets = _partition_primary_2d(N_SIDE, 4)

global_owner = {}
for sd_id in range(4):
    for gidx in primary_sets[sd_id]:
        global_owner[gidx] = sd_id

depth = 2

# For each subdomain, trace the full neighbor value pipeline
for sd_id in range(4):
    INFO(f"\n{'='*60}")
    INFO(f"Subdomain {sd_id}")
    INFO(f"{'='*60}")
    primary_nodes = primary_sets[sd_id]
    primary_set = set(primary_nodes)
    
    # Show primary region in grid
    r_min = min(g // N_SIDE for g in primary_nodes)
    r_max = max(g // N_SIDE for g in primary_nodes)
    c_min = min(g % N_SIDE for g in primary_nodes)
    c_max = max(g % N_SIDE for g in primary_nodes)
    INFO(f"  Primary region: rows [{r_min},{r_max}] cols [{c_min},{c_max}]")
    
    local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth)
    local_set = set(local_idx)
    
    own_idx = sorted(primary_set & local_set)
    own_offset = local_idx.index(own_idx[0]) if own_idx else 0
    own_size = len(own_idx)
    
    INFO(f"  local_idx: {len(local_idx)} nodes, own_offset={own_offset}, own_size={own_size}")
    
    # Extended region in grid
    ext_r_min = min(g // N_SIDE for g in local_idx)
    ext_r_max = max(g // N_SIDE for g in local_idx)
    ext_c_min = min(g % N_SIDE for g in local_idx)
    ext_c_max = max(g % N_SIDE for g in local_idx)
    INFO(f"  Extended region: rows [{ext_r_min},{ext_r_max}] cols [{ext_c_min},{ext_c_max}]")
    
    out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
        N, rows, cols, vals, local_idx)
    
    # Group outside connections by owner
    by_owner = {}
    for i, gidx in enumerate(out_gidx):
        owner = global_owner.get(gidx, -1)
        if owner not in by_owner:
            by_owner[owner] = []
        by_owner[owner].append((i, gidx, out_pos[i], out_coeffs[i]))
    
    INFO(f"  Outside connections: {len(out_gidx)} total")
    for owner, conns in sorted(by_owner.items()):
        if owner == sd_id:
            INFO(f"    owner=self ({sd_id}): {len(conns)} connections (SHOULD BE 0!)")
        elif owner == -1:
            INFO(f"    owner=NONE: {len(conns)} connections")
            for i, gidx, lpos, coeff in conns[:5]:
                r, c = gidx // N_SIDE, gidx % N_SIDE
                INFO(f"      gidx={gidx} (r={r},c={c}) local_pos={lpos} coeff={coeff}")
        else:
            nb_primary_map = {g: p for p, g in enumerate(primary_sets[owner])}
            INFO(f"    owner={owner}: {len(conns)} connections")
            missing = 0
            for i, gidx, lpos, coeff in conns:
                pos_in_nb = nb_primary_map.get(gidx, -1)
                if pos_in_nb < 0:
                    missing += 1
                    r, c = gidx // N_SIDE, gidx % N_SIDE
                    INFO(f"      MISSING: gidx={gidx} (r={r},c={c}) NOT in sd={owner} primary!")
            if missing:
                INFO(f"    {missing}/{len(conns)} outside nodes NOT in neighbor's primary!")
    
    # Check: are there any outside nodes that belong to self?
    self_conns = [(i, gidx) for i, gidx in enumerate(out_gidx) if global_owner.get(gidx) == sd_id]
    if self_conns:
        INFO(f"  BUG: {len(self_conns)} outside connections belong to self!")
        for i, gidx in self_conns[:5]:
            r, c = gidx // N_SIDE, gidx % N_SIDE
            INFO(f"    gidx={gidx} (r={r},c={c}) is in local_idx={gidx in local_set}")

# Now: run one iteration manually and compare with exact
INFO(f"\n{'='*60}")
INFO("Manual 1-step verification")
INFO(f"{'='*60}")

setups = []
for sd_id in range(4):
    primary_nodes = primary_sets[sd_id]
    primary_set = set(primary_nodes)
    local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth)
    own_idx = sorted(primary_set & set(local_idx))
    own_offset = local_idx.index(own_idx[0])
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
    setups.append({
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

# Step 0: zero neighbor values
for sd_id in range(4):
    s = setups[sd_id]
    nv = [0.0] * len(s["outside_coeffs"])
    xl = ex_slv_ras_bupdated_solve(s["solver"], s["b_orig"],
                                    s["outside_local_pos"], s["outside_coeffs"], nv)
    x_primary = list(xl[s["own_offset"]:s["own_offset"] + s["own_size"]])
    max_err = max(abs(x_primary[i] - x_exact[primary_sets[sd_id][i]]) for i in range(len(x_primary)))
    INFO(f"  sd={sd_id} step=0: max_err={max_err:.2e}")

# Step 1: use step 0 primary solutions as neighbors  
x_prev = []
for sd_id in range(4):
    s = setups[sd_id]
    nv = [0.0] * len(s["outside_coeffs"])
    xl = ex_slv_ras_bupdated_solve(s["solver"], s["b_orig"],
                                    s["outside_local_pos"], s["outside_coeffs"], nv)
    x_prev.append(list(xl[s["own_offset"]:s["own_offset"] + s["own_size"]]))

for sd_id in range(4):
    s = setups[sd_id]
    nv = [0.0] * len(s["outside_coeffs"])
    for nb_id in s["neighbor_ids"]:
        nb_x = x_prev[nb_id]
        recv_pos = s["neighbor_recv_idx"][nb_id]
        conn_idx = s["neighbor_needed"][nb_id]
        for i, ci in enumerate(conn_idx):
            p = recv_pos[i]
            if p >= 0:
                nv[ci] = nb_x[p]
    
    # Verify: what are the actual neighbor values we're using?
    for nb_id in s["neighbor_ids"]:
        recv_pos = s["neighbor_recv_idx"][nb_id]
        conn_idx = s["neighbor_needed"][nb_id]
        INFO(f"  sd={sd_id} -> nb={nb_id}: {len(conn_idx)} values")
        for i, ci in enumerate(conn_idx[:3]):
            outside_gidx = s["outside_global_idx"][ci]
            nb_val = nb_x[recv_pos[i]] if recv_pos[i] >= 0 else -999
            exact_val = x_exact[outside_gidx]
            INFO(f"    outside_gidx={outside_gidx} nb_x[{recv_pos[i]}]={nb_val:.4f} exact={exact_val:.4f}")
    
    xl = ex_slv_ras_bupdated_solve(s["solver"], s["b_orig"],
                                    s["outside_local_pos"], s["outside_coeffs"], nv)
    x_primary = list(xl[s["own_offset"]:s["own_offset"] + s["own_size"]])
    max_err = max(abs(x_primary[i] - x_exact[primary_sets[sd_id][i]]) for i in range(len(x_primary)))
    INFO(f"  sd={sd_id} step=1: max_err={max_err:.2e}")

INFO("\n=== DONE ===")
