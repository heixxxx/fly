"""Verify: are primary nodes contiguous in sorted local_idx for 2D partition?"""
import sys, os

import numpy as np
from _fly_log import INFO

N_SIDE = 10

def build_poisson_2d(n):
    N = n * n
    from scipy import sparse
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

from _fly_solver import ex_slv_graph_expand_overlap

N, rows, cols, vals = build_poisson_2d(N_SIDE)

# 2x2 partition
nsd_x, nsd_y = 2, 2
row_bounds = _distribute(N_SIDE, nsd_x)
col_bounds = _distribute(N_SIDE, nsd_y)

primary_sets = []
for ix in range(nsd_x):
    for iy in range(nsd_y):
        nodes = []
        for r in range(row_bounds[ix], row_bounds[ix + 1]):
            for c in range(col_bounds[iy], col_bounds[iy + 1]):
                nodes.append(r * N_SIDE + c)
        primary_sets.append(nodes)

depth = 2

for sd_id in range(4):
    primary_nodes = primary_sets[sd_id]
    primary_set = set(primary_nodes)
    
    local_idx = ex_slv_graph_expand_overlap(N, rows, cols, vals, primary_nodes, depth)
    
    own_idx = sorted(primary_set & set(local_idx))
    own_offset = local_idx.index(own_idx[0]) if own_idx else 0
    own_size = len(own_idx)
    
    # Check: are primary nodes contiguous in local_idx?
    primary_positions = sorted([local_idx.index(g) for g in primary_nodes])
    is_contiguous = (primary_positions == list(range(primary_positions[0], primary_positions[0] + len(primary_positions))))
    
    # Check: does x_local[own_offset:own_offset+own_size] give correct primary nodes?
    extracted_nodes = local_idx[own_offset:own_offset + own_size]
    matches = (extracted_nodes == own_idx)
    
    INFO(f"sd={sd_id}: primary={len(primary_nodes)} local={len(local_idx)} "
         f"own_offset={own_offset} own_size={own_size}")
    INFO(f"  primary_positions range: [{primary_positions[0]}, {primary_positions[-1]}]")
    INFO(f"  primary_positions contiguous: {is_contiguous}")
    INFO(f"  extracted matches own_idx: {matches}")
    
    if not is_contiguous:
        # Show where the gaps are
        gaps = []
        for i in range(len(primary_positions) - 1):
            if primary_positions[i+1] != primary_positions[i] + 1:
                gaps.append((primary_positions[i], primary_positions[i+1]))
        INFO(f"  GAPS at: {gaps[:10]}")
        
        # Show what's between primary positions
        non_primary_in_range = []
        for pos in range(primary_positions[0], primary_positions[-1] + 1):
            if pos not in primary_positions:
                non_primary_in_range.append((pos, local_idx[pos]))
        INFO(f"  Non-primary nodes in range: {len(non_primary_in_range)}")
        for pos, gidx in non_primary_in_range[:5]:
            r, c = gidx // N_SIDE, gidx % N_SIDE
            INFO(f"    pos={pos} gidx={gidx} (r={r},c={c})")

    # Also show first 10 elements of local_idx and which are primary
    tagged = [("P" if g in primary_set else "O", g) for g in local_idx[:15]]
    INFO(f"  local_idx[:15]: {tagged}")
