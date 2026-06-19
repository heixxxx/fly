"""Debug: trace neighbor value mapping to find accuracy bug."""
import sys, os, shutil, math

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO, DBG

N_SIDE = 10
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
        start, end = A_csc.indptr[k], A_csc.shape[1]
        if k + 1 < len(A_csc.indptr):
            end = A_csc.indptr[k + 1]
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

# Exact solution for comparison
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
x_exact = splu(A_sp).solve(np.array(b))

DEPTH = 3

for sd_id in range(NSD):
    INFO(f"\n=== Subdomain {sd_id} ===")
    primary_nodes = primary_sets[sd_id]
    primary_set = set(primary_nodes)
    INFO(f"  Primary nodes: {len(primary_nodes)}")

    local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, DEPTH)
    local_set = set(local_idx)
    INFO(f"  Extended nodes: {len(local_idx)} ratio={len(local_idx)/len(primary_nodes):.2f}x")

    own_idx = sorted(primary_set & local_set)
    own_offset = local_idx.index(own_idx[0]) if own_idx else 0
    own_size = len(own_idx)
    INFO(f"  own_idx: {len(own_idx)}, own_offset: {own_offset}")

    # Verify own_idx corresponds to primary nodes
    own_set = set(own_idx)
    primary_not_in_local = primary_set - local_set
    if primary_not_in_local:
        ERR(f"  BUG: primary nodes not in local: {primary_not_in_local}")

    # Outside connections
    out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
        N, rows, cols, vals, local_idx)
    INFO(f"  Outside connections: {len(out_gidx)}")

    # Check which outside nodes are in which primary set
    for i, gidx in enumerate(out_gidx[:10]):
        owner = global_owner.get(gidx, -1)
        # Find position in owner's primary
        if owner >= 0:
            pos_in_owner = primary_sets[owner].index(gidx) if gidx in primary_sets[owner] else -1
            INFO(f"    conn[{i}]: gidx={gidx} owner={owner} pos_in_owner_primary={pos_in_owner}")

    # Neighbor mapping
    neighbor_needed = {}
    for i, gidx in enumerate(out_gidx):
        owner = global_owner.get(gidx, -1)
        if owner >= 0 and owner != sd_id:
            if owner not in neighbor_needed:
                neighbor_needed[owner] = []
            neighbor_needed[owner].append(i)

    neighbor_ids = sorted(neighbor_needed.keys())
    INFO(f"  Neighbors: {neighbor_ids}")

    neighbor_recv_idx = {}
    for nb_id in neighbor_ids:
        nb_primary_map = {gidx: pos for pos, gidx in enumerate(primary_sets[nb_id])}
        recv_positions = []
        for conn_i in neighbor_needed[nb_id]:
            outside_gidx = out_gidx[conn_i]
            pos = nb_primary_map.get(outside_gidx, -1)
            recv_positions.append(pos)
        neighbor_recv_idx[nb_id] = recv_positions
        missing = sum(1 for p in recv_positions if p < 0)
        INFO(f"    neighbor {nb_id}: {len(recv_positions)} conns, {missing} missing from primary")

# Now run one iteration manually for sd_id=0 to verify
sd_id = 0
INFO(f"\n=== Manual solve for sd={sd_id} ===")
primary_nodes = primary_sets[sd_id]
primary_set = set(primary_nodes)
local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, DEPTH)
own_idx = sorted(primary_set & set(local_idx))
own_offset = local_idx.index(own_idx[0])
own_size = len(own_idx)

size, _, a_rows, a_cols, a_vals = ex_slv_extract_subdomain_matrix(
    N, rows, cols, vals, local_idx)
out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
    N, rows, cols, vals, local_idx)
b_local = [b[gidx] for gidx in local_idx]

neighbor_values = [0.0] * len(out_coeffs)
x_local = ex_slv_ras_bupdated_solve(
    EXSlvSubdomainSolver.from_coo(size, a_rows, a_cols, a_vals),
    b_local, out_pos, out_coeffs, neighbor_values)
x_primary = x_local[own_offset:own_offset + own_size]

INFO(f"  x_primary range: [{min(x_primary):.4f}, {max(x_primary):.4f}]")
INFO(f"  x_exact[primary] range: [{min(x_exact[list(primary_set)]):.4f}, {max(x_exact[list(primary_set)]):.4f}]")

# Check: for sd_id=0, own_offset should map own_idx to primary_nodes
# own_idx = sorted(primary_set & local_set) — sorted by value
# primary_nodes from _partition_primary_2d — also sorted by value (row-major)
# So own_idx should equal primary_nodes
INFO(f"  own_idx == primary_nodes: {own_idx == primary_nodes}")
INFO(f"  own_offset={own_offset}, own_size={own_size}, len(primary_nodes)={len(primary_nodes)}")

# More importantly: where are the primary nodes in local_idx?
primary_positions = [local_idx.index(gidx) for gidx in primary_nodes[:5]]
INFO(f"  First 5 primary positions in local_idx: {primary_positions}")
INFO(f"  local_idx[own_offset:own_offset+5] = {local_idx[own_offset:own_offset+5]}")
INFO(f"  primary_nodes[:5] = {primary_nodes[:5]}")

INFO("\n=== DONE ===")
