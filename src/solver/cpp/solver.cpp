#include <solver/cpp/solver.h>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#ifdef EIGEN_HAS_OPENMP
#include <omp.h>
#endif

namespace fly {

Eigen::SparseMatrix<double> build_poisson_2d(int n) {
    const int size = n * n;
    Eigen::SparseMatrix<double> A(size, size);
    A.reserve(Eigen::VectorXi::Constant(size, 5));

    for (int idx = 0; idx < size; ++idx) {
        const int i = idx / n;  // row in grid
        const int j = idx % n;  // col in grid

        // Diagonal: 4
        A.insert(idx, idx) = 4.0;

        // Left neighbor: (i, j-1)
        if (j > 0) {
            A.insert(idx, idx - 1) = -1.0;
        }
        // Right neighbor: (i, j+1)
        if (j < n - 1) {
            A.insert(idx, idx + 1) = -1.0;
        }
        // Top neighbor: (i-1, j)
        if (i > 0) {
            A.insert(idx, idx - n) = -1.0;
        }
        // Bottom neighbor: (i+1, j)
        if (i < n - 1) {
            A.insert(idx, idx + n) = -1.0;
        }
    }
    A.makeCompressed();
    return A;
}

std::vector<SubdomainInfo> partition_1d(int n, int num_parts, int overlap) {
    const int total = n * n;

    // Split grid rows [0, n) into num_parts contiguous row-blocks.
    // Each row i contributes n grid points with global indices [i*n, i*n + n).
    const int base_rows = n / num_parts;
    const int remainder = n % num_parts;

    std::vector<SubdomainInfo> partitions;
    partitions.reserve(num_parts);

    int row_start = 0;
    for (int p = 0; p < num_parts; ++p) {
        SubdomainInfo info;
        info.subdomain_id_ = p;

        const int num_rows = base_rows + (p < remainder ? 1 : 0);
        const int row_end = row_start + num_rows;

        // Own indices: all grid points in [row_start, row_end) rows
        const int own_start = row_start * n;
        const int own_end = row_end * n;
        info.own_indices_.reserve(own_end - own_start);
        for (int idx = own_start; idx < own_end; ++idx) {
            info.own_indices_.push_back(idx);
        }

        // Local indices: extend by overlap rows on each side
        const int local_row_start = std::max(0, row_start - overlap);
        const int local_row_end = std::min(n, row_end + overlap);
        const int local_start = local_row_start * n;
        const int local_end = local_row_end * n;

        info.local_indices_.reserve(local_end - local_start);
        for (int idx = local_start; idx < local_end; ++idx) {
            info.local_indices_.push_back(idx);
        }

        // Boundary indices: overlap rows not in own rows
        // Left overlap: [local_start, own_start)
        for (int idx = local_start; idx < own_start; ++idx) {
            info.boundary_indices_.push_back(idx);
        }
        // Right overlap: [own_end, local_end)
        for (int idx = own_end; idx < local_end; ++idx) {
            info.boundary_indices_.push_back(idx);
        }

        partitions.push_back(std::move(info));
        row_start = row_end;
    }

    return partitions;
}

Eigen::SparseMatrix<double> extract_subdomain_matrix(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& local_indices) {
    const int local_size = static_cast<int>(local_indices.size());

    Eigen::SparseMatrix<double> R(local_size, A.rows());
    R.reserve(Eigen::VectorXi::Constant(A.rows(), 1));
    for (int i = 0; i < local_size; ++i) {
        R.insert(i, local_indices[i]) = 1.0;
    }
    R.makeCompressed();

    // A_local = R * A * R^T
    Eigen::SparseMatrix<double> RA = R * A;
    Eigen::SparseMatrix<double> A_local = RA * R.transpose();
    A_local.makeCompressed();
    return A_local;
}

// 静态成员初始化：0 = 用 Eigen 默认（omp_get_max_threads），> 0 = 指定线程数
int SubdomainSolver::num_threads_ = 0;

void SubdomainSolver::set_num_threads(int num_threads) {
    num_threads_ = num_threads;
    // Eigen 用环境变量 EIGEN_NUM_THREADS 或 omp_set_num_threads 控制并行
#ifdef EIGEN_HAS_OPENMP
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }
#endif
}

int SubdomainSolver::get_num_threads() {
    return num_threads_;
}

SubdomainSolver::SubdomainSolver(const Eigen::SparseMatrix<double>& local_A) {
    solver_.compute(local_A);
    if (solver_.info() != Eigen::Success) {
        throw std::runtime_error("SubdomainSolver: factorization failed");
    }
}

Eigen::VectorXd SubdomainSolver::solve(const Eigen::VectorXd& local_rhs) const {
    Eigen::VectorXd result = solver_.solve(local_rhs);
    if (solver_.info() != Eigen::Success) {
        throw std::runtime_error("SubdomainSolver: solve failed");
    }
    return result;
}

double residual_norm(
    const Eigen::SparseMatrix<double>& A,
    const Eigen::VectorXd& x,
    const Eigen::VectorXd& b) {
    Eigen::VectorXd r = b - A * x;
    return r.norm();
}

Eigen::VectorXd ras_subdomain_update(
    const Eigen::SparseMatrix<double>& A,
    const Eigen::VectorXd& b,
    const Eigen::VectorXd& x,
    const SubdomainInfo& subdomain,
    const SubdomainSolver& solver) {
    const int local_size = static_cast<int>(subdomain.local_indices_.size());

    Eigen::SparseMatrix<double> R(local_size, A.rows());
    R.reserve(Eigen::VectorXi::Constant(A.rows(), 1));
    for (int i = 0; i < local_size; ++i) {
        R.insert(i, subdomain.local_indices_[i]) = 1.0;
    }
    R.makeCompressed();

    // Compute global residual and restrict to local
    Eigen::VectorXd global_residual = b - A * x;
    Eigen::VectorXd local_residual = R * global_residual;

    // Solve local correction
    Eigen::VectorXd delta_local = solver.solve(local_residual);

    // Assemble: only update owned indices (RAS partition of unity = I on owned nodes)
    // local_indices layout: [left_overlap... | own... | right_overlap...]
    // own_indices start at offset = local_size - own_count - right_boundary_count
    // Simpler: count boundary indices on the left side
    Eigen::VectorXd x_new = x;
    const int own_count = static_cast<int>(subdomain.own_indices_.size());
    // Left boundary count = position of first own index in local_indices
    int left_boundary = 0;
    if (own_count > 0) {
        int first_own = subdomain.own_indices_[0];
        for (int j = 0; j < local_size; ++j) {
            if (subdomain.local_indices_[j] == first_own) {
                left_boundary = j;
                break;
            }
        }
    }
    for (int i = 0; i < own_count; ++i) {
        x_new(subdomain.own_indices_[i]) += delta_local(left_boundary + i);
    }

    return x_new;
}

std::vector<int> graph_expand_overlap(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& primary_indices,
    int depth)
{
    std::unordered_set<int> expanded(primary_indices.begin(), primary_indices.end());
    std::unordered_set<int> current(primary_indices.begin(), primary_indices.end());

    for (int layer = 0; layer < depth; ++layer) {
        std::unordered_set<int> frontier;
        for (int node : current) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(A, node); it; ++it) {
                int row = static_cast<int>(it.row());
                if (row != node && it.value() != 0.0 && expanded.find(row) == expanded.end()) {
                    frontier.insert(row);
                }
            }
        }
        if (frontier.empty()) break;
        expanded.insert(frontier.begin(), frontier.end());
        current = std::move(frontier);
    }

    std::vector<int> result(expanded.begin(), expanded.end());
    std::sort(result.begin(), result.end());
    return result;
}

void find_outside_connections(
    const Eigen::SparseMatrix<double>& A,
    const std::vector<int>& local_indices,
    std::vector<int>& out_local_positions,
    std::vector<int>& out_outside_indices,
    std::vector<double>& out_coefficients)
{
    std::unordered_set<int> local_set(local_indices.begin(), local_indices.end());
    out_local_positions.clear();
    out_outside_indices.clear();
    out_coefficients.clear();

    int local_size = static_cast<int>(local_indices.size());
    for (int i = 0; i < local_size; ++i) {
        int gidx = local_indices[i];
        for (Eigen::SparseMatrix<double>::InnerIterator it(A, gidx); it; ++it) {
            int row = static_cast<int>(it.row());
            double val = it.value();
            if (row != gidx && local_set.find(row) == local_set.end() && val != 0.0) {
                out_local_positions.push_back(i);
                out_outside_indices.push_back(row);
                out_coefficients.push_back(val);
            }
        }
    }
}

Eigen::VectorXd ras_bupdated_solve(
    const SubdomainSolver& solver,
    const Eigen::VectorXd& b_orig_local,
    const std::vector<int>& outside_local_positions,
    const std::vector<double>& outside_coefficients,
    const std::vector<double>& outside_neighbor_values,
    double omega)
{
    Eigen::VectorXd b_updated = b_orig_local;
    for (size_t i = 0; i < outside_local_positions.size(); ++i) {
        b_updated[outside_local_positions[i]] -=
            omega * outside_coefficients[i] * outside_neighbor_values[i];
    }
    return solver.solve(b_updated);
}

} // namespace fly
