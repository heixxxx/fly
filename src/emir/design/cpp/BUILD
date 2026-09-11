# emir design 模块的 C++ 核心：DSDesign/DSStack 数据结构（T3；
# 解析适配层 ds_lef_adapter/ds_def_adapter T4/T5 追加）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_emir_design_ds_types",
    srcs = [
        "ds_def_adapter.cpp",
        "ds_instance_pipeline.cpp",
        "ds_lef_adapter.cpp",
        "ds_merge.cpp",
        "ds_name_mapper.cpp",
        "ds_net_pipeline.cpp",
        "ds_types.cpp",
    ],
    hdrs = [
        "ds_def_adapter.h",
        "ds_instance_pipeline.h",
        "ds_lef_adapter.h",
        "ds_merge.h",
        "ds_name_hasher.h",
        "ds_name_mapper.h",
        "ds_net_pipeline.h",
        "ds_transform_util.h",
        "ds_types.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@hat_trie",  # R8b：DSHasherBackendHatrie 的 tsl::htrie_map（header-only）
        "//src/common/serialization:fly_serialization",
        "//src/common/types:fly_common_types",
        "//src/container/cpp:fly_container",
        "//src/container/cpp:fly_container_aliases",
        "//src/geometry:fly_geometry",
        "//src/lefdef:fly_lefdef_def",
        "//src/lefdef:fly_lefdef_lef",
        "//src/emir/lib/cpp:fly_emir_lib",  # S3 merge_lib：LIBLibrary
        "//src/log/cpp:fly_log",  # MSG 宏链路的 log_write / logger.h
        "//src/message/cpp:fly_message",  # DSGN::0001-0005 消息
    ],
    visibility = ["//visibility:public"],
)

cc_shared_library(
    name = "fly_emir_design_ds_types_so",
    deps = [":fly_emir_design_ds_types"],
    exports_filter = [
        "//src/container/cpp:fly_container",
        "//src/common/serialization:fly_serialization",
        "//src/common/buffer:data_checksum",
        "//src/message/cpp:fly_message",
        "//src/log/cpp:fly_log",
        "//src/emir/lib/cpp:fly_emir_lib",
    ],
    visibility = ["//visibility:public"],
)
