# src/lefdef —— Si2 LEF/DEF 解析器 fork 接入 Bazel（演进三步走第 1 步，
# 见 README 与 docs/emir/design-db-s1-s4b-implementation-plan.md §1 P0-3）。
# 上游源码零改动；bison 生成物 lef.tab.cpp/def.tab.cpp 随 .y 变更手动
# `make -j1` 再生成并签入（src/lefdef/.gitignore 已放行这四个产物）。
# 不接入：工具目录（lefrw/lefwrite/defrw/defwrite/lefdiff/defdiff/TEST）
# 与 zlib 系（lefzlib/clefzlib/defzlib，不定义任何 zlib 宏）。
# 上游老式 C++，-w 抑制告警（精确 -Wno 列表待魔改阶段收敛）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

# LEF 解析器核心：lef/lef 全部 .cpp（23 个，含 bison 产物 lef.tab.cpp，
# 与上游 Makefile LIBSRCS 口径一致）。内部 include 均为同目录引号形式，
# 无跨目录依赖；对外 include 路径 <lefdef/lef/lef/lefrReader.hpp>。
cc_library(
    name = "fly_lefdef_lef",
    srcs = glob(
        ["lef/lef/*.cpp"],
        exclude = ["lef/lef/lefwrite*"],
    ),
    hdrs = glob([
        "lef/lef/*.hpp",
        "lef/lef/*.h",
    ]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20", "-O2", "-w"],
    visibility = ["//visibility:public"],
)

# DEF 解析器核心：def/def 全部 .cpp（33 个，含 bison 产物 def.tab.cpp，
# 与上游 Makefile LIBSRCS 口径一致）。对外 include 路径
# <lefdef/def/def/defrReader.hpp>。
cc_library(
    name = "fly_lefdef_def",
    srcs = glob(["def/def/*.cpp"]),
    hdrs = glob([
        "def/def/*.hpp",
        "def/def/*.h",
    ]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20", "-O2", "-w"],
    visibility = ["//visibility:public"],
)

# 上游金标数据（tests 基线单测经 data 引用；文件保持上游原样）
exports_files([
    "def/TEST/complete.5.8.def",
    "lef/TEST/complete.5.8.lef",
])
