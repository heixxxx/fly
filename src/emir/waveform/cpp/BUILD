# emir waveform db 的 C++ 核心：VCD（值变化转储文件）解析器 + 解析边界
# 领域结构（wf_parser/wf_types，第一段编码任务）+ 换算/路由/合并层
#（wf_partition——落库形态/名字换算/分区合并/settings 物化，plan
# docs/emir/waveform-db-plan.md §5.2/§5.3/§6/§7/§8；py/export 随 flow 批
# 次补全，dev-rules 三段式结构标准不变——本 BUILD 不含 export/.so 注册接入）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_emir_waveform",
    srcs = [
        "wf_parser.cpp",
        "wf_partition.cpp",
        "wf_types.cpp",
    ],
    hdrs = [
        "wf_parser.h",
        "wf_partition.h",
        "wf_types.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/common/serialization:fly_serialization",
        "//src/emir/common/cpp:fly_emir_common_cpp",  # CM 族强类型 id
        "//src/emir/design/cpp:fly_emir_design_ds_types",  # 换算消费 design db 快照
    ],
    visibility = ["//visibility:public"],
)
