load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

# SQLite amalgamation 3.46.1（公有领域，vendor 进库保证离线确定性构建）。
# 供 src/monitor 的 MetricsDb（master 单写落盘）使用；GUI 侧用 Python 标准库
# sqlite3 只读同一文件。NFS 约束：必须用 rollback journal（PERSIST），
# 禁止 WAL（跨进程 mmap 共享内存在 NFS 上不可用）。
cc_library(
    name = "sqlite3",
    srcs = ["sqlite3.c"],
    hdrs = ["sqlite3.h", "sqlite3ext.h"],
    includes = ["."],
    copts = [
        "-w",  # amalgamation 自身警告与项目无关
        "-DSQLITE_THREADSAFE=1",
        "-DSQLITE_OMIT_LOAD_EXTENSION=1",
    ],
)
