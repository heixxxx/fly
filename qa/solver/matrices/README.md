# Pre-generated Poisson matrices

Each `poisson_n{n}.npz` contains the deterministic 5-point stencil Poisson
matrix for an n×n grid, RHS b=ones, and the exact solution `x_exact` computed
offline via scipy `splu` (so QA never pays the LU factorization cost).

Small matrices (n ≤ 50) are checked in. Large ones (n ≥ 500) are gitignored
(too big) — generate them with:

```bash
./build/bin/fly qa/solver/gen_matrices.py
```

The Poisson stencil is fully deterministic (no random seed), so regenerated
files are byte-identical.
