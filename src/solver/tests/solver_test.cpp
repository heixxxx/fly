#include <gtest/gtest.h>
#include <solver/cpp/solver.h>
#include <cmath>
#include <algorithm>
#include <memory>

using namespace fly;

// --- build_poisson_2d tests ---

TEST(SolverTest, Poisson2D_SizeAndSparsity) {
    const int n = 4;
    auto A = build_poisson_2d(n);
    const int size = n * n;

    ASSERT_EQ(A.rows(), size);
    ASSERT_EQ(A.cols(), size);

    // Count non-zeros per row
    for (int i = 0; i < size; ++i) {
        int nnz_per_row = 0;
        for (Eigen::SparseMatrix<double>::InnerIterator it(A, i); it; ++it) {
            nnz_per_row++;
        }
        // Interior points have 5 entries, edges have 4, corners have 3
        EXPECT_GE(nnz_per_row, 3);
        EXPECT_LE(nnz_per_row, 5);
    }
}

TEST(SolverTest, Poisson2D_DiagonalPositive) {
    const int n = 4;
    auto A = build_poisson_2d(n);
    const int size = n * n;

    // All diagonal entries should be 4
    for (int i = 0; i < size; ++i) {
        EXPECT_DOUBLE_EQ(A.coeff(i, i), 4.0);
    }
}

TEST(SolverTest, Poisson2D_OffDiagonalNegative) {
    const int n = 4;
    auto A = build_poisson_2d(n);
    const int size = n * n;

    // Off-diagonal entries should be -1 or 0
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            if (i != j) {
                double val = A.coeff(i, j);
                EXPECT_TRUE(val == 0.0 || val == -1.0)
                    << "A(" << i << "," << j << ") = " << val;
            }
        }
    }
}

TEST(SolverTest, Poisson2D_Symmetric) {
    const int n = 4;
    auto A = build_poisson_2d(n);
    const int size = n * n;

    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            EXPECT_DOUBLE_EQ(A.coeff(i, j), A.coeff(j, i))
                << "Asymmetry at (" << i << "," << j << ")";
        }
    }
}

// --- partition_1d tests ---

TEST(SolverTest, Partition1D_TwoParts) {
    const int n = 4;
    auto parts = partition_1d(n, 2, 1);

    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].subdomain_id_, 0);
    EXPECT_EQ(parts[1].subdomain_id_, 1);

    EXPECT_EQ(parts[0].own_indices_[0], 0);
    EXPECT_EQ(parts[0].own_indices_.back(), 7);
    EXPECT_EQ(parts[1].own_indices_[0], 8);
    EXPECT_EQ(parts[1].own_indices_.back(), 15);

    EXPECT_TRUE(parts[0].local_indices_.size() > parts[0].own_indices_.size());
    EXPECT_TRUE(parts[1].local_indices_.size() > parts[1].own_indices_.size());
}

TEST(SolverTest, Partition1D_AllOwnIndicesCoverFullRange) {
    const int n = 4;
    auto parts = partition_1d(n, 3, 1);
    const int total = n * n;

    // Collect all own indices
    std::vector<int> all_own;
    for (const auto& p : parts) {
        all_own.insert(all_own.end(), p.own_indices_.begin(), p.own_indices_.end());
    }
    EXPECT_EQ(static_cast<int>(all_own.size()), total);
    std::sort(all_own.begin(), all_own.end());
    for (int i = 0; i < total; ++i) {
        EXPECT_EQ(all_own[i], i);
    }
}

// --- SubdomainSolver tests ---

TEST(SolverTest, SubdomainSolver_SmallSystem) {
    // Create a simple 3x3 SPD matrix
    Eigen::SparseMatrix<double> A(3, 3);
    std::vector<Eigen::Triplet<double>> triplets = {
        {0, 0, 4.0}, {0, 1, -1.0}, {0, 2, -1.0},
        {1, 0, -1.0}, {1, 1, 4.0}, {1, 2, -1.0},
        {2, 0, -1.0}, {2, 1, -1.0}, {2, 2, 4.0},
    };
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    SubdomainSolver solver(A);

    Eigen::VectorXd rhs(3);
    rhs << 2.0, 0.0, 0.0;

    Eigen::VectorXd x = solver.solve(rhs);

    // Verify A*x = rhs
    Eigen::VectorXd residual = rhs - A * x;
    EXPECT_LT(residual.norm(), 1e-10);
}

// --- residual_norm tests ---

TEST(SolverTest, ResidualNorm_ExactSolution) {
    const int n = 4;
    auto A = build_poisson_2d(n);
    const int size = n * n;

    // Exact solution: all ones
    Eigen::VectorXd x = Eigen::VectorXd::Ones(size);
    // For Poisson with u=1 everywhere, b = A*x should be 0 (boundary-adjusted)
    // Actually, for interior points: 4*1 - 4*1 = 0 (if all neighbors exist)
    // Corner points: 4*1 - 2*1 = 2, edge points: 4*1 - 3*1 = 1
    // So A*x ≠ 0 generally. Let's compute b = A*x and check residual is 0.
    Eigen::VectorXd b = A * x;
    double rn = residual_norm(A, x, b);
    EXPECT_NEAR(rn, 0.0, 1e-12);
}

TEST(SolverTest, ResidualNorm_Nonzero) {
    const int n = 3;
    auto A = build_poisson_2d(n);
    const int size = n * n;

    Eigen::VectorXd x = Eigen::VectorXd::Zero(size);
    Eigen::VectorXd b = Eigen::VectorXd::Ones(size);
    double rn = residual_norm(A, x, b);
    EXPECT_GT(rn, 0.0);
}

// --- extract_subdomain_matrix tests ---

TEST(SolverTest, ExtractSubdomainMatrix) {
    const int n = 4;
    auto A = build_poisson_2d(n);

    // Extract a small subdomain: indices 0,1,4,5 (2x2 block)
    std::vector<int> local = {0, 1, 4, 5};
    auto A_local = extract_subdomain_matrix(A, local);

    EXPECT_EQ(A_local.rows(), 4);
    EXPECT_EQ(A_local.cols(), 4);

    // Diagonal should still be 4
    for (int i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(A_local.coeff(i, i), 4.0);
    }
}

// --- Full RAS convergence test ---

TEST(SolverTest, RAS_Convergence) {
    const int n = 8;
    const int size = n * n;
    const int num_parts = 2;
    const int overlap = 2;
    const int max_iter = 100;
    const double tol = 1e-8;

    auto A = build_poisson_2d(n);

    // RHS: b = ones (so we're solving Ax = 1)
    Eigen::VectorXd b = Eigen::VectorXd::Ones(size);

    // Reference solution via direct solve
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> direct_solver;
    direct_solver.compute(A);
    ASSERT_EQ(direct_solver.info(), Eigen::Success);
    Eigen::VectorXd x_ref = direct_solver.solve(b);
    ASSERT_EQ(direct_solver.info(), Eigen::Success);

    // Partition
    auto parts = partition_1d(n, num_parts, overlap);

    // Build subdomain solvers (SimplicialLDLT is non-copyable, non-movable)
    std::vector<std::unique_ptr<SubdomainSolver>> solvers;
    solvers.reserve(num_parts);
    for (int p = 0; p < num_parts; ++p) {
        auto A_local = extract_subdomain_matrix(A, parts[p].local_indices_);
        solvers.push_back(std::make_unique<SubdomainSolver>(A_local));
    }

    // Initial guess: zero
    Eigen::VectorXd x = Eigen::VectorXd::Zero(size);

    // RAS iteration
    for (int iter = 0; iter < max_iter; ++iter) {
        double res = residual_norm(A, x, b);
        if (res < tol) break;

        for (int p = 0; p < num_parts; ++p) {
            x = ras_subdomain_update(A, b, x, parts[p], *solvers[p]);
        }
    }

    // Check convergence: solution should be close to reference
    double final_residual = residual_norm(A, x, b);
    EXPECT_LT(final_residual, 1e-6)
        << "RAS did not converge. Final residual: " << final_residual;

    // Check solution matches reference
    Eigen::VectorXd diff = x - x_ref;
    EXPECT_LT(diff.norm() / x_ref.norm(), 1e-4)
        << "RAS solution doesn't match direct solve. Relative error: "
        << diff.norm() / x_ref.norm();
}
