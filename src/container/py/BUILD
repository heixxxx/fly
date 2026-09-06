# Fly container Python package（框架级自定义容器的 Python 绑定）

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_container_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/container/export:_fly_container.so",
    ],
)
