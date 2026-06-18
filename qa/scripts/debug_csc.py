"""Debug: check CSC neighbor lookup correctness."""
import numpy as np
from scipy import sparse

n = 20; N = n*n
diags = [4.0*np.ones(N), -1.0*np.ones(N-1), -1.0*np.ones(N-1),
         -1.0*np.ones(N-n), -1.0*np.ones(N-n)]
A = sparse.diags(diags, [0,1,-1,n,-n], shape=(N,N), format='lil')
for i in range(1,n): A[i*n-1,i*n]=0; A[i*n,i*n-1]=0
A_csc = A.tocsc()

# Node 0 (corner): should connect to self, 1 (right), 20 (below)
col0_rows = sorted(A_csc[:,0].nonzero()[0])
print(f'Node 0 CSC col neighbors: {col0_rows}')

# Node 21 (row=1, col=1): should connect to 20,21,22,1,41
col21_rows = sorted(A_csc[:,21].nonzero()[0])
print(f'Node 21 CSC col neighbors: {col21_rows}')

# Check symmetry
diff = (A_csc - A_csc.T).nnz
print(f'Symmetric: nnz(A-A^T) = {diff}')

# Node 0 via row
row0_cols = sorted(A_csc[0,:].nonzero()[1])
print(f'Node 0 CSC row neighbors: {row0_cols}')

# Manual BFS for 10x10 block, depth=1
block = set()
for r in range(10):
    for c in range(10):
        block.add(r*n + c)

depth1_new = set()
for node in block:
    for r in A_csc[:,node].nonzero()[0]:
        nbr = int(r)
        if nbr not in block:
            depth1_new.add(nbr)
print(f'\nManual BFS depth=1: {len(depth1_new)} new nodes')
print(f'Expected: ~360 (perimeter of 10x10 block * 1 layer)')
total = len(block) + len(depth1_new)
print(f'Total after depth=1: {total} (ratio={total/100:.2f}x)')
