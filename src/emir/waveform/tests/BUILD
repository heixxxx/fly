# waveform db 单测：解析边界（wf_parser_test——plan §11.2 解析矩阵）+
# 换算/路由/合并/物化（wf_partition_test——plan §11.2 换算矩阵：名字换
# 算两形态/块绑定三形态 + strip_prefix/向量位级匹配/块首快照统计/合并
# 场景 a 直和与场景 b 覆盖/settings-only 物化/真实样张全链换算）+
# Python 层（flow 错误处理/入口参数校验/三段式结构——同 timing 三件套
# 先例）。
# 手写样张 data/ 补特征覆盖（100ps 档、位区间、real/event 与 EVCD 强度
# 行跳过、破损流、no-timescale）；真实样张为 Icarus Verilog 确定性产物
#（qa/emir/data/waveform/README.md，除 $date 头外双跑逐字节一致），按
# timing db 先例拷入 tests/data 供 cc_test data 引用。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "wf_parser_test",
    srcs = ["wf_parser_test.cpp"],
    data = [
        "data/basic.vcd",
        "data/bad_dumpvars_unclosed.vcd",
        "data/bad_missing_enddefs.vcd",
        "data/bad_scope_leak.vcd",
        "data/bad_ts_regress.vcd",
        "data/bad_unclosed_version.vcd",
        "data/no_timescale.vcd",
        "data/small.vcd",
        "data/unsupported.vcd",
        "data/xz.vcd",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/waveform/cpp:fly_emir_waveform",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "wf_partition_test",
    srcs = ["wf_partition_test.cpp"],
    data = [
        "data/block_b1.vcd",
        "data/block_b2.vcd",
        "data/instpin.vcd",
        "data/part_a1.vcd",
        "data/part_a2.vcd",
        "data/small.vcd",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/testing:fly_common_testing",
        "//src/emir/waveform/cpp:fly_emir_waveform",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

# waveform flow 错误处理单测（同 test_tm_flow_error.py 先例）
py_test(
    name = "test_wf_flow_error",
    srcs = ["test_wf_flow_error.py"],
    main = "test_wf_flow_error.py",
    imports = ["../../../..", "../export", "../../design/export",
               "../../lib/export", "../../timing/export"],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)

# build_waveform_db 入口参数校验负例单测（2026-09-17 裁定范式：header
# Schema 直接校验 raise + 白名单严格模式）
py_test(
    name = "test_wf_db_validation",
    srcs = ["test_wf_db_validation.py"],
    main = "test_wf_db_validation.py",
    imports = ["../../..", "../export", "../../design/export",
               "../../lib/export", "../../timing/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)

# build_waveform_db 三段式结构单测（master 提交面：O(1) 任务 + freeze
# 固定标记依赖 + 预处理无内容 IO + 双模式根任务体目录）
py_test(
    name = "test_wf_flow_structure",
    srcs = ["test_wf_flow_structure.py"],
    main = "test_wf_flow_structure.py",
    imports = ["../../..", "../export", "../../design/export",
               "../../lib/export", "../../timing/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)
