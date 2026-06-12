"""Generate Poisson 2D matrices (n=1000,1200,1500) with golden solutions.
Runs direct solve (scipy splu) and reports timing + memory usage.
Saves .npz files to big_qa/matrices/.
"""
import sys
import os
import time
import resource

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.join(PROJECT_ROOT, 'src'))

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

MATRICES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'matrices')
os.makedirs(MATRICES_DIR, exist_ok=True)

SIZES = [1000, 1200, 1500, 1800]


def memory_gb():
    """Return RSS in GB."""
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024 ** 3)


def generate_poisson_matrix(n, path):
    """Generate Poisson 2D matrix and save to .npz with golden solution."""
    N = n * n
    mem_before = memory_gb()

    t0 = time.time()
    diags = [4.0 * np.ones(N),
             -1.0 * np.ones(N - 1), -1.0 * np.ones(N - 1),
             -1.0 * np.ones(N - n), -1.0 * np.ones(N - n)]
    A = sparse.diags(diags, [0, 1, -1, n, -n], shape=(N, N), format='lil')
    for i in range(1, n):
        A[i * n - 1, i * n] = 0.0
        A[i * n, i * n - 1] = 0.0
    A_csc = A.tocsc()
    t_build = time.time() - t0

    rows, cols, vals = [], [], []
    for k in range(A_csc.shape[1]):
        start, end = A_csc.indptr[k], A_csc.indptr[k + 1]
        for p in range(start, end):
            rows.append(int(A_csc.indices[p]))
            cols.append(int(k))
            vals.append(float(A_csc.data[p]))

    b = np.ones(N, dtype=np.float64)

    t1 = time.time()
    lu = splu(A_csc)
    t_factor = time.time() - t1

    t2 = time.time()
    x_exact = lu.solve(b)
    t_solve = time.time() - t2

    mem_after = memory_gb()

    np.savez(path,
             n=np.int64(n), N=np.int64(N),
             rows=np.array(rows, dtype=np.int64),
             cols=np.array(cols, dtype=np.int64),
             vals=np.array(vals, dtype=np.float64),
             b=b, x_exact=x_exact)

    file_size_mb = os.path.getsize(path) / (1024 ** 2)

    # Verify golden solution
    rel_res = np.linalg.norm(b - A_csc @ x_exact) / np.linalg.norm(b)

    print(f"[MATRIX] n={n} N={N:,} nnz={len(vals):,}")
    print(f"  build={t_build:.1f}s  factor={t_factor:.1f}s  solve={t_solve:.2f}s  total={t_build+t_factor+t_solve:.1f}s")
    print(f"  memory: {mem_before:.1f}GB → {mem_after:.1f}GB (peak RSS)")
    print(f"  file={file_size_mb:.0f}MB  rel_res={rel_res:.2e}")
    print()

    return {
        'n': n, 'N': N, 'nnz': len(vals),
        't_build': t_build, 't_factor': t_factor, 't_solve': t_solve,
        'mem_peak_gb': mem_after, 'file_size_mb': file_size_mb,
        'rel_res': rel_res,
    }


if __name__ == '__main__':
    results = []
    for n in SIZES:
        path = os.path.join(MATRICES_DIR, f'poisson_n{n}.npz')
        if os.path.exists(path):
            print(f"[SKIP] {path} already exists")
            d = np.load(path, allow_pickle=False)
            results.append({'n': n, 'N': int(d['N']), 'nnz': len(d['rows']), 'skipped': True})
            continue
        r = generate_poisson_matrix(n, path)
        r['path'] = path
        results.append(r)
        del r  # free memory before next matrix

    print("=" * 60)
    print("Matrix Generation Summary")
    print("=" * 60)
    for r in results:
        if r.get('skipped'):
            print(f"  n={r['n']}: skipped (exists)")
        else:
            print(f"  n={r['n']}: factor={r['t_factor']:.1f}s solve={r['t_solve']:.2f}s "
                  f"mem={r['mem_peak_gb']:.1f}GB file={r['file_size_mb']:.0f}MB")
