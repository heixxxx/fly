# common/runtime —— 运行期小值类型与上下文（WriterID/WorkerContext/WriteContextHash/ErrorTypes）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_common_runtime",
    hdrs = glob(["cpp/*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/buffer:fly_common_buffer",
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)
