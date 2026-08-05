package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

# OpenMP 可选开关：--define=solver_openmp=1 启用
# 用法：./fly.sh build --define=solver_openmp=1 //src/main/cpp:fly
config_setting(
    name = "openmp_enabled",
    define_values = {"solver_openmp": "1"},
)

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
    # OpenMP 可选启用（--define=solver_openmp=1）：让 Eigen LDLT 分解并行化，
    # 在多核机器上可显著加速分解（~2-3x）。但需注意：nsd+1 个 worker 进程
    # 各占多核会导致 CPU 过竞争，在核数 ≤ 2×nsd 的机器上反而变慢。
    # 适合 worker 进程复用（单进程内 nsd 个并行分解）或核数充裕的场景。
    copts = select({
        ":openmp_enabled": ["-std=c++20", "-O2", "-fopenmp"],
        "//conditions:default": ["-std=c++20", "-O2"],
    }),
    linkopts = select({
        ":openmp_enabled": ["-fopenmp"],
        "//conditions:default": [],
    }),
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
