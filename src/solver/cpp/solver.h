#pragma once

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <vector>

namespace fly {

struct SubdomainInfo {
    int subdomain_id;
    std::vector<int> local_indices;
    std::vector<int> own_indices;
    std::vector<int> boundary_indices;
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

} // namespace fly
