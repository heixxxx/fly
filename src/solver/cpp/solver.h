#pragma once

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <vector>

namespace fly {

struct SubdomainInfo {
    int subdomain_id_;
    std::vector<int> local_indices_;
    std::vector<int> own_indices_;
    std::vector<int> boundary_indices_;
};

// Build 2D Poisson matrix (5-point stencil) of size n*n x n*n.
// For a grid point (i,j):
//   4*u(i,j) - u(i-1,j) - u(i+1,j) - u(i,j-1) - u(i,j+1) = f(i,j)
Eigen::SparseMatrix<double> build_poisson_2d(int n);

// 1D block partitioning with overlap.
// Partitions n*n unknowns into num_parts contiguous blocks,
// each extended by `overlap` rows on each side.
std::vector<SubdomainInfo> partition_1d(int n, int num_parts, int overlap);

// Extract subdomain local matrix: A_local = R_i * A * R_i^T
Eigen::SparseMatrix<double> extract_subdomain_matrix(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& local_indices);

// Extract subdomain local matrix with Robin boundary conditions.
// Same as extract_subdomain_matrix but adds alpha to diagonal entries
// of boundary/interface nodes. This creates absorbing (Robin) BC:
//   B_local = A_local + alpha * D_interface
// where D_interface has 1 on diagonal for boundary nodes.
// alpha: Robin impedance parameter (alpha > 0, typically alpha ~ gamma/h)
Eigen::SparseMatrix<double> extract_subdomain_matrix_oras(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& local_indices,
    const std::vector<int>& own_indices,
    double alpha);

// Subdomain solver — caches SimplicialLDLT factorization
class SubdomainSolver {
public:
    explicit SubdomainSolver(const Eigen::SparseMatrix<double>& local_A);
    Eigen::VectorXd solve(const Eigen::VectorXd& local_rhs) const;

private:
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver_;
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
std::vector<int> graph_expand_overlap(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& primary_indices,
    int depth);

// Find all connections from local nodes to nodes outside the local region.
// Returns three parallel flat arrays:
//   local_positions[i], outside_global_indices[i], coefficients[i]
// such that A[local_indices[local_positions[i]], outside_global_indices[i]] = coefficients[i]
// and outside_global_indices[i] is NOT in local_indices.
void find_outside_connections(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& local_indices,
    std::vector<int>& out_local_positions,
    std::vector<int>& out_outside_indices,
    std::vector<double>& out_coefficients);

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
    const std::vector<int>& outside_local_positions,
    const std::vector<double>& outside_coefficients,
    const std::vector<double>& outside_neighbor_values,
    double omega = 1.0);

} // namespace fly
