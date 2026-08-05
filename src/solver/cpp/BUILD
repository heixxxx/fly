package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_solver",
    srcs = ["solver.cpp"],
    hdrs = ["solver.h"],
    strip_include_prefix = "/src",
    # Eigen's template-heavy sparse solvers (SimplicialLDLT) are extremely
    # sensitive to optimization: unoptimized (-O0 fastbuild) LDLT of an 82k-dim
    # Poisson submatrix takes ~1.5s, dominating setup. -O2 brings it down
    # substantially. Pin -O2 here regardless of the global compilation_mode so
    # the solver stays fast even in fastbuild/debug builds.
    #
    # 始终链接 -fopenmp：让 Eigen 分解支持运行时并行控制。
    # 默认单线程（SubdomainSolver::set_num_threads 不调用时），
    # 运行时 Config set_int("solver_openmp_threads", N) 可启用多线程分解。
    # 注意：nsd+1 个 worker 进程各占多核会导致 CPU 过竞争，
    # 仅在核数充裕（≥ 2×nsd）时建议启用。
    copts = ["-std=c++20", "-O2", "-fopenmp"],
    linkopts = ["-fopenmp"],
    linkstatic = False,
    deps = [
        "@eigen//:eigen",
        "//src/common/cpp:fly_common_types",
    ],
)

cc_shared_library(
    name = "fly_solver_so",
    deps = [":fly_solver"],
    visibility = ["//visibility:public"],
)
