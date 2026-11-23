# emir spef db 的 C++ 核心（阶段一）：SPEF（IEEE 1481）解析边界领域
# 结构（sp_types——解析产物聚合量 + 词法公共单点）与解析器/切块扫描
#（sp_parser——spef-db-plan §8 模块结构的 sp_parse_spef_file /
# sp_parse_spef_text / sp_plan_file_chunks 家族）+ 换算/路由/合并层
#（sp_partition——落库形态/名字换算/分区路由/按来源作用域合并/覆盖率
# 统计/汇总）。export/py 接线随后续批次补全——本 BUILD 不含 export、
# 不注册 emir 聚合（dev-rules 三段式结构标准不变）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_emir_spef",
    srcs = [
        "sp_parser.cpp",
        "sp_partition.cpp",
        "sp_types.cpp",
    ],
    hdrs = [
        "sp_parser.h",
        "sp_partition.h",
        "sp_types.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/serialization:fly_serialization",
        "//src/container/cpp:fly_container_aliases",
        "//src/emir/common/cpp:fly_emir_common_cpp",  # CM 族强类型 id
        "//src/emir/design/cpp:fly_emir_design_ds_types",  # 换算消费 design db 快照
    ],
    visibility = ["//visibility:public"],
)
