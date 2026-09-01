#pragma once

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <common/cpp/common_types.h>
#include <vector>

namespace fly {

struct SubdomainInfo {
    int subdomain_id_;
    CMVector<int> local_indices_;
    CMVector<int> own_indices_;
    CMVector<int> boundary_indices_;
};

// Build 2D Poisson matrix (5-point stencil) of size n*n x n*n.
// For a grid point (i,j):
//   4*u(i,j) - u(i-1,j) - u(i+1,j) - u(i,j-1) - u(i,j+1) = f(i,j)
Eigen::SparseMatrix<double> build_poisson_2d(int n);

// 1D block partitioning with overlap.
// Partitions n*n unknowns into num_parts contiguous blocks,
// each extended by `overlap` rows on each side.
CMVector<SubdomainInfo> partition_1d(int n, int num_parts, int overlap);

// Extract subdomain local matrix: A_local = R_i * A * R_i^T
Eigen::SparseMatrix<double> extract_subdomain_matrix(
    const Eigen::SparseMatrix<double>& A,
    const CMVector<int>& local_indices);

// Subdomain solver — caches SimplicialLDLT factorization
class SubdomainSolver {
public:
    explicit SubdomainSolver(const Eigen::SparseMatrix<double>& local_A);
    Eigen::VectorXd solve(const Eigen::VectorXd& local_rhs) const;

    // 设置分解并行线程数（运行时控制）。
    // Eigen 的 SimplicialLDLT 在分解阶段支持 OpenMP 并行（需编译时 -fopenmp）。
    // num_threads <= 0 表示用 Eigen 默认（omp_get_max_threads）。
    // 必须在构造前调用（分解在构造函数中执行）。
    static void set_num_threads(int num_threads);
    static int get_num_threads();

private:
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver_;
    static int num_threads_;
};

// Compute residual: ||b - A*x||_2
double residual_norm(
    const Eigen::SparseMatrix<double>& A,
    const Eigen::VectorXd& x,
    const Eigen::VectorXd& b);

// RAS subdomain update:
// 1. r_i = R_i * (b - A*x)
// 2. delta_i = A_i^{-1} * r_i
// 3. x_new = x; x_new[own_indices] += delta_i[own_indices_offset]
Eigen::VectorXd ras_subdomain_update(
    const Eigen::SparseMatrix<double>& A,
    const Eigen::VectorXd& b,
    const Eigen::VectorXd& x,
    const SubdomainInfo& subdomain,
    const SubdomainSolver& solver);

// Graph-based overlap expansion.
// Given primary node indices and the sparse matrix adjacency,
// expand by following edges for `depth` layers (BFS).
// Returns all reachable nodes (primary + extended overlap).
CMVector<int> graph_expand_overlap(
    const Eigen::SparseMatrix<double>& A,
    const CMVector<int>& primary_indices,
    int depth);

// Find all connections from local nodes to nodes outside the local region.
// Returns three parallel flat arrays:
//   local_positions[i], outside_global_indices[i], coefficients[i]
// such that A[local_indices[local_positions[i]], outside_global_indices[i]] = coefficients[i]
// and outside_global_indices[i] is NOT in local_indices.
void find_outside_connections(
    const Eigen::SparseMatrix<double>& A,
    const CMVector<int>& local_indices,
    CMVector<int>& out_local_positions,
    CMVector<int>& out_outside_indices,
    CMVector<double>& out_coefficients);

// RAS b-update + solve.
// Updates b_local for all nodes with outside connections:
//   b_updated[local_pos] = b_orig[local_pos] - omega * sum(coeff * neighbor_value)
// Then solves A_local * x = b_updated using cached LDLT.
// omega: relaxation parameter. omega=1.0 is standard RAS.
//   omega < 1: under-relaxation (stabilizes convergence)
//   omega > 1: over-relaxation (accelerates convergence)
Eigen::VectorXd ras_bupdated_solve(
    const SubdomainSolver& solver,
    const Eigen::VectorXd& b_orig_local,
    const CMVector<int>& outside_local_positions,
    const CMVector<double>& outside_coefficients,
    const CMVector<double>& outside_neighbor_values,
    double omega = 1.0);

} // namespace fly
