# Fly storage Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_storage_py",
    srcs = [
        "__init__.py",
        "database.py",
    ],
    deps = ["//src/storage/export:_fly_storage.so"],
)
