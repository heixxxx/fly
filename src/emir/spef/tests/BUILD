# emir spef db 单测（阶段一）：SPEF 解析器单测（sp_parser_test——
# spef-db-plan §9.2 解析矩阵：头部全构造/单位换算/多值取值/名字映射/
# 节点分类与合计口径/方言容忍/跳过项/注释/文件级与段级错误边界/切块
# 计划/整文件与分段等价/真实工具样张回归〔文件存在才跑〕）+ 换算/路
# 由/合并层单测（sp_partition_test——spef-db-plan §9.3 换算矩阵：名字
# 换算与容错/块绑定三形态 + strip_prefix/分区路由与复制/按来源作用域
# 合并/覆盖率统计/汇总/序列化 round-trip；输入经 sp_parse_spef_text
# 走解析 → 换算 → 合并真实链路）。
# 手写样张 data/ 每个文件头注释写明用途与钉死的期望值；真实工具样张
# 为 GPL 许可（OpenSTA examples），不拷入仓库——唯一开关是环境变量
# FLY_SPEF_REAL_SAMPLE（未设置即跳过；设置了但文件不存在是显式失败）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "sp_parser_test",
    srcs = ["sp_parser_test.cpp"],
    data = [
        "data/bad_first_line.spef",
        "data/bad_missing_end.spef",
        "data/basic.spef",
        "data/comments.spef",
        "data/dialect_conn.spef",
        "data/dialect_misc.spef",
        "data/namemap.spef",
        "data/nodes_self_other.spef",
        "data/rnet_pnet.spef",
        "data/units_ff.spef",
        "data/units_ff100.spef",
        "data/units_mix.spef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/spef/cpp:fly_emir_spef",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "sp_partition_test",
    srcs = ["sp_partition_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/spef/cpp:fly_emir_spef",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

# spef flow 错误处理单测（同 test_wf_flow_error.py 先例）
py_test(
    name = "test_sp_flow_error",
    srcs = ["test_sp_flow_error.py"],
    main = "test_sp_flow_error.py",
    imports = ["../../..", "../export", "../../design/export",
               "../../lib/export", "../../timing/export",
               "../../waveform/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/emir/spef/export:_fly_emir_spef.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)

# build_spef_db 入口参数校验负例单测（2026-09-17 裁定范式：header
# Schema 直接校验 raise + 白名单严格模式）
py_test(
    name = "test_sp_db_validation",
    srcs = ["test_sp_db_validation.py"],
    main = "test_sp_db_validation.py",
    imports = ["../../..", "../export", "../../design/export",
               "../../lib/export", "../../timing/export",
               "../../waveform/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/emir/spef/export:_fly_emir_spef.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)

# build_spef_db 三段式结构单测（master 提交面：O(1) 任务 + freeze
# 固定标记依赖 + 预处理无内容 IO + 作用域编号/消息族/切块数接线）
py_test(
    name = "test_sp_flow_structure",
    srcs = ["test_sp_flow_structure.py"],
    main = "test_sp_flow_structure.py",
    imports = ["../../..", "../export", "../../design/export",
               "../../lib/export", "../../timing/export",
               "../../waveform/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/emir/spef/export:_fly_emir_spef.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)
