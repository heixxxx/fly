#include <export/cpp/export_macros.h>
#include <solver/cpp/solver.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>

static std::vector<double> vec_to_std(const Eigen::VectorXd& v) {
    return std::vector<double>(v.data(), v.data() + v.size());
}

static Eigen::VectorXd std_to_vec(const std::vector<double>& v) {
    return Eigen::Map<const Eigen::VectorXd>(v.data(), static_cast<Eigen::Index>(v.size()));
}

struct EXSlvSparseMatrix {
    Eigen::SparseMatrix<double> mat;
    int size() const { return static_cast<int>(mat.rows()); }
};

FLY_EXPORT_MODULE(_fly_solver) {

FLY_EXPORT_CLASS(fly::SubdomainInfo, "EXSlvSubdomainInfo")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("subdomain_id", &fly::SubdomainInfo::subdomain_id)
    FLY_EXPORT_ATTR("local_indices", &fly::SubdomainInfo::local_indices)
    FLY_EXPORT_ATTR("own_indices", &fly::SubdomainInfo::own_indices)
    FLY_EXPORT_ATTR("boundary_indices", &fly::SubdomainInfo::boundary_indices);

FLY_EXPORT_CLASS(fly::SubdomainSolver, "EXSlvSubdomainSolver")
    FLY_EXPORT_INIT(const Eigen::SparseMatrix<double>&)
    FLY_EXPORT_DEF("from_coo", [](int size,
                                   const std::vector<int>& rows,
                                   const std::vector<int>& cols,
                                   const std::vector<double>& values) -> fly::SubdomainSolver* {
        Eigen::SparseMatrix<double> A(size, size);
        std::vector<Eigen::Triplet<double>> triplets;
        triplets.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            triplets.emplace_back(rows[i], cols[i], values[i]);
        }
        A.setFromTriplets(triplets.begin(), triplets.end());
        A.makeCompressed();
        return new fly::SubdomainSolver(A);
    })
    FLY_EXPORT_DEF("solve", [](const fly::SubdomainSolver& self,
                                const std::vector<double>& rhs) -> std::vector<double> {
        return vec_to_std(self.solve(std_to_vec(rhs)));
    });

FLY_EXPORT_FUNCTION("ex_slv_partition_1d", [](int n, int num_parts, int overlap) {
    return fly::partition_1d(n, num_parts, overlap);
});

m.def("ex_slv_build_poisson_2d", [](int n) -> fly_export::object {
    auto A = fly::build_poisson_2d(n);
    std::vector<int> rows, cols;
    std::vector<double> values;
    for (int k = 0; k < A.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(A, k); it; ++it) {
            rows.push_back(static_cast<int>(it.row()));
            cols.push_back(static_cast<int>(it.col()));
            values.push_back(it.value());
        }
    }
    return fly_export::make_tuple(A.rows(), A.cols(), rows, cols, values);
});

m.def("ex_slv_extract_subdomain_matrix", [](
    int matrix_size,
    const std::vector<int>& row_indices,
    const std::vector<int>& col_indices,
    const std::vector<double>& values,
    const std::vector<int>& local_indices) -> fly_export::object {
    Eigen::SparseMatrix<double> A(matrix_size, matrix_size);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        triplets.emplace_back(row_indices[i], col_indices[i], values[i]);
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();
    auto local_A = fly::extract_subdomain_matrix(A, local_indices);
    std::vector<int> out_rows, out_cols;
    std::vector<double> out_values;
    for (int k = 0; k < local_A.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(local_A, k); it; ++it) {
            out_rows.push_back(static_cast<int>(it.row()));
            out_cols.push_back(static_cast<int>(it.col()));
            out_values.push_back(it.value());
        }
    }
    return fly_export::make_tuple(
        local_A.rows(), local_A.cols(), out_rows, out_cols, out_values);
});

m.def("ex_slv_extract_subdomain_matrix_oras", [](
    int matrix_size,
    const std::vector<int>& row_indices,
    const std::vector<int>& col_indices,
    const std::vector<double>& values,
    const std::vector<int>& local_indices,
    const std::vector<int>& own_indices,
    double alpha) -> fly_export::object {
    Eigen::SparseMatrix<double> A(matrix_size, matrix_size);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        triplets.emplace_back(row_indices[i], col_indices[i], values[i]);
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    auto local_A = fly::extract_subdomain_matrix_oras(A, local_indices, own_indices, alpha);

    std::vector<int> out_rows, out_cols;
    std::vector<double> out_values;
    for (int k = 0; k < local_A.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(local_A, k); it; ++it) {
            out_rows.push_back(static_cast<int>(it.row()));
            out_cols.push_back(static_cast<int>(it.col()));
            out_values.push_back(it.value());
        }
    }
    return fly_export::make_tuple(
        local_A.rows(), local_A.cols(), out_rows, out_cols, out_values);
});

m.def("ex_slv_residual_norm", [](
    int matrix_size,
    const std::vector<int>& row_indices,
    const std::vector<int>& col_indices,
    const std::vector<double>& values,
    const std::vector<double>& x_vec,
    const std::vector<double>& b_vec) -> double {
    Eigen::SparseMatrix<double> A(matrix_size, matrix_size);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        triplets.emplace_back(row_indices[i], col_indices[i], values[i]);
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();
    return fly::residual_norm(A, std_to_vec(x_vec), std_to_vec(b_vec));
});

m.def("ex_slv_ras_subdomain_update", [](
    int matrix_size,
    const std::vector<int>& row_indices,
    const std::vector<int>& col_indices,
    const std::vector<double>& values,
    const std::vector<double>& b_vec,
    const std::vector<double>& x_vec,
    const fly::SubdomainInfo& subdomain,
    const fly::SubdomainSolver& solver) -> std::vector<double> {
    Eigen::SparseMatrix<double> A(matrix_size, matrix_size);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        triplets.emplace_back(row_indices[i], col_indices[i], values[i]);
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();
    return vec_to_std(fly::ras_subdomain_update(
        A, std_to_vec(b_vec), std_to_vec(x_vec), subdomain, solver));
});

// -- Cached sparse matrix for fast matvec --

FLY_EXPORT_CLASS(EXSlvSparseMatrix, "EXSlvSparseMatrix")
    FLY_EXPORT_DEF("from_coo", [](int size,
                                   const std::vector<int>& rows,
                                   const std::vector<int>& cols,
                                   const std::vector<double>& values) -> EXSlvSparseMatrix* {
        auto* m = new EXSlvSparseMatrix();
        m->mat.resize(size, size);
        std::vector<Eigen::Triplet<double>> triplets;
        triplets.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            triplets.emplace_back(rows[i], cols[i], values[i]);
        }
        m->mat.setFromTriplets(triplets.begin(), triplets.end());
        m->mat.makeCompressed();
        return m;
    })
    FLY_EXPORT_DEF("matvec", [](const EXSlvSparseMatrix& self,
                                 const std::vector<double>& v) -> std::vector<double> {
        return vec_to_std(self.mat * std_to_vec(v));
    })
    FLY_EXPORT_DEF("size", [](const EXSlvSparseMatrix& self) -> int {
        return static_cast<int>(self.mat.rows());
    });

// -- Vector operations (Eigen-accelerated) --

m.def("ex_slv_vec_norm", [](const std::vector<double>& v) -> double {
    return std_to_vec(v).norm();
});

m.def("ex_slv_vec_dot", [](const std::vector<double>& a,
                            const std::vector<double>& b) -> double {
    return std_to_vec(a).dot(std_to_vec(b));
});

m.def("ex_slv_vec_scale", [](const std::vector<double>& v, double alpha) -> std::vector<double> {
    Eigen::VectorXd result = Eigen::Map<const Eigen::VectorXd>(v.data(), static_cast<Eigen::Index>(v.size())) * alpha;
    return vec_to_std(result);
});

m.def("ex_slv_vec_axpy", [](const std::vector<double>& y, double alpha,
                              const std::vector<double>& x) -> std::vector<double> {
    Eigen::VectorXd result = Eigen::Map<const Eigen::VectorXd>(y.data(), static_cast<Eigen::Index>(y.size())) + alpha * Eigen::Map<const Eigen::VectorXd>(x.data(), static_cast<Eigen::Index>(x.size()));
    return vec_to_std(result);
});

m.def("ex_slv_vec_sub", [](const std::vector<double>& a,
                            const std::vector<double>& b) -> std::vector<double> {
    return vec_to_std(std_to_vec(a) - std_to_vec(b));
});

m.def("ex_slv_vec_back_solve", [](
    const std::vector<std::vector<double>>& H_rows,
    const std::vector<double>& S,
    int m) -> std::vector<double> {
    Eigen::VectorXd y = Eigen::Map<const Eigen::VectorXd>(S.data(), m);
    for (int i = m - 1; i >= 0; --i) {
        if (y(i) == 0.0) continue;
        double diag = H_rows[i][i];
        if (std::abs(diag) < 1e-30) { y(i) = 0.0; continue; }
        y(i) /= diag;
        for (int k = 0; k < i; ++k) {
            y(k) -= H_rows[i][k] * y(i);
        }
    }
    return vec_to_std(y);
});

m.def("ex_slv_vec_xpay", [](const std::vector<double>& x, const std::vector<double>& v_col,
                             double yk, std::vector<double>& out) {
    Eigen::Map<Eigen::VectorXd> om(out.data(), static_cast<Eigen::Index>(out.size()));
    om += yk * std_to_vec(v_col);
});

m.def("ex_slv_graph_expand_overlap", [](
    int matrix_size,
    const std::vector<int>& row_indices,
    const std::vector<int>& col_indices,
    const std::vector<double>& values,
    const std::vector<int>& primary_indices,
    int depth) -> std::vector<int> {
    Eigen::SparseMatrix<double> A(matrix_size, matrix_size);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        triplets.emplace_back(row_indices[i], col_indices[i], values[i]);
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();
    return fly::graph_expand_overlap(A, primary_indices, depth);
});

m.def("ex_slv_find_outside_connections", [](
    int matrix_size,
    const std::vector<int>& row_indices,
    const std::vector<int>& col_indices,
    const std::vector<double>& values,
    const std::vector<int>& local_indices) -> fly_export::object {
    Eigen::SparseMatrix<double> A(matrix_size, matrix_size);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        triplets.emplace_back(row_indices[i], col_indices[i], values[i]);
    }
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    std::vector<int> local_positions, outside_indices;
    std::vector<double> coefficients;
    fly::find_outside_connections(A, local_indices, local_positions, outside_indices, coefficients);
    return fly_export::make_tuple(local_positions, outside_indices, coefficients);
});

m.def("ex_slv_ras_bupdated_solve", [](
    const fly::SubdomainSolver& solver,
    const std::vector<double>& b_orig,
    const std::vector<int>& outside_local_positions,
    const std::vector<double>& outside_coefficients,
    const std::vector<double>& outside_neighbor_values) -> std::vector<double> {
    return vec_to_std(fly::ras_bupdated_solve(
        solver,
        std_to_vec(b_orig),
        outside_local_positions,
        outside_coefficients,
        outside_neighbor_values));
});

} // FLY_EXPORT_MODULE
