# emir design 模块数据结构单测（T3）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "ds_types_test",
    srcs = ["ds_types_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    # 静态链 ds_types（逻辑验证不经过 cc_shared_library 绑定层，同 lib 先例）
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_merge_test",
    srcs = ["ds_merge_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_def_adapter_test",
    srcs = ["ds_def_adapter_test.cpp"],
    data = [
        "data/block_synth.def",
        "data/tech_synth.lef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/testing:fly_common_testing",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_transform_util_test",
    srcs = ["ds_transform_util_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_instance_pipeline_test",
    srcs = ["ds_instance_pipeline_test.cpp"],
    data = [
        "data/block_synth.def",
        "data/components_synth.def",
        "data/tech_synth.lef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_net_pipeline_test",
    srcs = ["ds_net_pipeline_test.cpp"],
    data = [
        "data/nets_synth.def",
        "data/tech_synth.lef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_hier_test",
    srcs = ["ds_hier_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/testing:fly_common_testing",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_name_mapper_test",
    srcs = ["ds_name_mapper_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_name_mapper_bench_test",
    srcs = ["ds_name_mapper_bench_test.cpp"],
    # R8c 分派查询路径性能基准（裁定 53）：DISABLED_ 默认不进全量测试集，
    # 手动 --gtest_filter='DSNameMapperBench.*' 运行（见文件头说明）
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_name_hasher_test",
    srcs = ["ds_name_hasher_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_partition_test",
    srcs = ["ds_partition_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "ds_lef_adapter_test",
    srcs = ["ds_lef_adapter_test.cpp"],
    data = [
        "data/bad_syntax.lef",
        "data/cells_synth.lef",
        "data/tech_synth.lef",
        "data/tech_synth_dbu4000.lef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/testing:fly_common_testing",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

# design flow 错误处理单测（流程错误处理范式 2026-09-13，dev-rules §7.2）
py_test(
    name = "ds_flow_error_test",
    srcs = ["test_ds_flow_error.py"],
    main = "test_ds_flow_error.py",
    data = [
        "data/cells_synth.lef",
        "data/tech_synth.lef",
    ],
    imports = ["../../..", "../export", "../../lib/export"],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)
