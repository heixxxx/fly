#include <solver/cpp/solver.h>
#include <cmath>
#include <algorithm>

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
    const int base_size = total / num_parts;
    const int remainder = total % num_parts;

    std::vector<SubdomainInfo> partitions;
    partitions.reserve(num_parts);

    int start = 0;
    for (int p = 0; p < num_parts; ++p) {
        SubdomainInfo info;
        info.subdomain_id = p;

        const int part_size = base_size + (p < remainder ? 1 : 0);
        const int part_start = start;
        const int part_end = start + part_size;  // exclusive

        // Own indices: [part_start, part_end)
        info.own_indices.reserve(part_size);
        for (int idx = part_start; idx < part_end; ++idx) {
            info.own_indices.push_back(idx);
        }

        // Local indices: extend by overlap on each side, clamped to [0, total)
        const int local_start = std::max(0, part_start - overlap);
        const int local_end = std::min(total, part_end + overlap);

        info.local_indices.reserve(local_end - local_start);
        for (int idx = local_start; idx < local_end; ++idx) {
            info.local_indices.push_back(idx);
        }

        // Boundary indices: overlap zone indices that belong to neighbors
        // Left overlap: [local_start, part_start)
        // Right overlap: [part_end, local_end)
        for (int idx = local_start; idx < part_start; ++idx) {
            info.boundary_indices.push_back(idx);
        }
        for (int idx = part_end; idx < local_end; ++idx) {
            info.boundary_indices.push_back(idx);
        }

        partitions.push_back(std::move(info));
        start = part_end;
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
    const int local_size = static_cast<int>(subdomain.local_indices.size());

    Eigen::SparseMatrix<double> R(local_size, A.rows());
    R.reserve(Eigen::VectorXi::Constant(A.rows(), 1));
    for (int i = 0; i < local_size; ++i) {
        R.insert(i, subdomain.local_indices[i]) = 1.0;
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
    const int own_count = static_cast<int>(subdomain.own_indices.size());
    // Left boundary count = position of first own index in local_indices
    int left_boundary = 0;
    if (own_count > 0) {
        int first_own = subdomain.own_indices[0];
        for (int j = 0; j < local_size; ++j) {
            if (subdomain.local_indices[j] == first_own) {
                left_boundary = j;
                break;
            }
        }
    }
    for (int i = 0; i < own_count; ++i) {
        x_new(subdomain.own_indices[i]) += delta_local(left_boundary + i);
    }

    return x_new;
}

} // namespace fly
