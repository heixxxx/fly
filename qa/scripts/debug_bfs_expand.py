"""Debug: BFS expansion for different nsd and block positions."""
from _fly_log import INFO
import sys, os, math
import numpy as np
from scipy import sparse

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))
from _fly_solver import ex_slv_graph_expand_overlap

def build_poisson_2d(n):
    N = n * n
    diags = [4.0 * np.ones(N), -1.0 * np.ones(N - 1), -1.0 * np.ones(N - 1),
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


def make_block(n, r0, r1, c0, c1):
    return [r * n + c for r in range(r0, r1) for c in range(c0, c1)]


# ── n=500, different partitions ──
n = 500
N, rows, cols, vals = build_poisson_2d(n)
INFO(f"=== n={n} N={N} ===")

for grid, bsize in [("2x2", 250), ("4x4", 125), ("8x8", 62)]:
    INFO(f"\n--- {grid} partition, block={bsize}x{bsize}={bsize*bsize} ---")
    # Corner block (0,0) and center block
    corner = make_block(n, 0, bsize, 0, bsize)
    mid = n // 2
    center = make_block(n, mid, mid + bsize, mid, mid + bsize)

    for d in [5, 10, 20, 30, 40]:
        ext_c = ex_slv_graph_expand_overlap(N, rows, cols, vals, corner, d)
        ext_m = ex_slv_graph_expand_overlap(N, rows, cols, vals, center, d)
        rc = len(ext_c) / len(corner)
        rm = len(ext_m) / len(center)
        marker = " <<<" if rc >= 1.4 else ""
        INFO(f"  depth={d:2d}: corner={rc:.3f}x center={rm:.3f}x{marker}")
        if rc >= 1.8:
            break

INFO("[DONE]")
