# 新思 Open Liberty 参考解析器（C 语言）
# 许可：SYNOPSYS Open Source License Version 1.0（COPYING.pdf，随源码保留）。
#
# 引入方式（用户裁定）：源码原样保留于本目录（以便后续优化），用上游原构建
# 方式编译（build_so.sh，等价上游 CMake：glob 全部 src/*.c 静态库 + 按需提取
# 语义），bazel 以预编译静态库 srcs 引入——不在 bazel 内编上游 C 源（解析套
# 与语法检查套存在同名内部辅助函数，全量目标文件链接冲突）。fly 侧改动集中
# 在适配层（../../cpp/lib_parser.cpp）。lib/*.so 为动态形态产物，供运行时
# 动态引入场景备用。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "liberty_parser_c",
    srcs = ["lib/libsi2dr_liberty.a"],
    hdrs = glob(["include/*.h"]),
    includes = ["include"],
    alwayslink = True,   # 适配层跨 .o 引用（si2dr 入口分散于多编译单元），
                         # 防止链接器丢弃未直接引用的目标文件成员
    visibility = ["//visibility:public"],
)
