"""Debug: verify subdomain matrix extraction and b-update correctness."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

N_SIDE = 6
N = N_SIDE * N_SIDE

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

from _fly_solver import (ex_slv_graph_expand_overlap,
                          ex_slv_extract_subdomain_matrix,
                          ex_slv_find_outside_connections,
                          EXSlvSubdomainSolver,
                          ex_slv_ras_bupdated_solve)

N_val, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N_val

A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))
x_exact = splu(A_sp).solve(np.array(b))

# Single subdomain test: take nodes 0..17 (first 3 rows × 6 cols)
# local = rows 0-2 of 6x6 grid = nodes 0-17
primary_nodes = list(range(18))
depth = 1
local_idx = ex_slv_graph_expand_overlap(N_val, rows, cols, vals, primary_nodes, depth)

INFO(f"n={N_SIDE}, primary={len(primary_nodes)}, extended={len(local_idx)}")
INFO(f"local_idx={local_idx}")
local_set = set(local_idx)

# Extract subdomain matrix
size, _, a_rows, a_cols, a_vals = ex_slv_extract_subdomain_matrix(
    N_val, rows, cols, vals, local_idx)

A_sub = sparse.csc_matrix((a_vals, (a_rows, a_cols)), shape=(size, size))
INFO(f"A_sub shape={A_sub.shape}, nnz={A_sub.nnz}")

# Find outside connections
out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
    N_val, rows, cols, vals, local_idx)
INFO(f"Outside connections: {len(out_gidx)}")
for i in range(len(out_gidx)):
    INFO(f"  conn[{i}]: local_pos={out_pos[i]} gidx={out_gidx[i]} coeff={out_coeffs[i]:.1f}")

# Now verify: build the full RHS update manually
# For each outside connection (local_pos, outside_gidx, coeff):
#   The equation for local node local_idx[local_pos] in the original matrix has
#   A[local_idx[local_pos], outside_gidx] * x[outside_gidx] on the LHS.
#   When we restrict, this term is removed from the matrix, so we need:
#   b_local[local_pos] -= A[local_idx[local_pos], outside_gidx] * x[outside_gidx]

# Verify by checking the global matrix A_sp
INFO(f"\nVerifying outside connections against global matrix:")
for i in range(len(out_gidx)):
    local_gidx = local_idx[out_pos[i]]
    outside_gidx = out_gidx[i]
    reported_coeff = out_coeffs[i]
    actual_A_val = A_sp[local_gidx, outside_gidx]
    actual_A_val2 = A_sp[outside_gidx, local_gidx]
    INFO(f"  local={local_gidx} outside={outside_gidx}: "
         f"reported_coeff={reported_coeff:.1f} "
         f"A[local,outside]={actual_A_val:.1f} "
         f"A[outside,local]={actual_A_val2:.1f}")

# Test: solve with exact neighbor values
b_local = [b[gidx] for gidx in local_idx]

# Build neighbor_values from exact solution
neighbor_values = []
for i in range(len(out_gidx)):
    neighbor_values.append(x_exact[out_gidx[i]])

x_local = ex_slv_ras_bupdated_solve(
    EXSlvSubdomainSolver.from_coo(size, a_rows, a_cols, a_vals),
    b_local, out_pos, out_coeffs, neighbor_values)

# Check: x_local should match x_exact at local positions
INFO(f"\nVerifying solution with exact neighbor values:")
max_err = 0
for i, gidx in enumerate(local_idx):
    err = abs(x_local[i] - x_exact[gidx])
    if err > max_err:
        max_err = err
    if err > 1e-6:
        INFO(f"  local_pos={i} gidx={gidx}: x_local={x_local[i]:.6f} "
             f"x_exact={x_exact[gidx]:.6f} err={err:.2e}")
INFO(f"Max error: {max_err:.2e}")

# Also verify: A_sub * x_local should equal b_updated
b_updated = np.array(b_local, dtype=float)
for i in range(len(out_pos)):
    b_updated[out_pos[i]] -= out_coeffs[i] * neighbor_values[i]
residual = A_sub @ np.array(x_local) - b_updated
INFO(f"A_sub * x_local - b_updated residual: {np.linalg.norm(residual):.2e}")

# Now check what A_sub * x_exact_local gives
x_exact_local = np.array([x_exact[gidx] for gidx in local_idx])
full_residual = A_sub @ x_exact_local - b_updated
INFO(f"A_sub * x_exact_local - b_updated residual: {np.linalg.norm(full_residual):.2e}")

# Check the FULL residual: A * x_exact - b
full_global = A_sp @ x_exact - np.array(b)
INFO(f"A * x_exact - b residual: {np.linalg.norm(full_global):.2e}")

# Verify: does restriction of global matrix give same as A_sub?
# For each (i,j) in A_sub, A_sub[i,j] should equal A[local_idx[i], local_idx[j]]
INFO(f"\nVerifying submatrix extraction:")
err_count = 0
for i in range(min(size, 30)):
    for j in range(min(size, 30)):
        sub_val = A_sub[i, j]
        global_val = A_sp[local_idx[i], local_idx[j]]
        if abs(sub_val - global_val) > 1e-10:
            if err_count < 5:
                INFO(f"  MISMATCH at ({i},{j}): sub={sub_val:.1f} global={global_val:.1f}")
            err_count += 1
INFO(f"Total mismatches: {err_count}")

INFO(f"\n=== DONE ===")
