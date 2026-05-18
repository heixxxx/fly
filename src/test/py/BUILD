package(default_visibility = ["//visibility:public"])

py_library(
    name = "fly_test_py",
    srcs = ["__init__.py", "test_tasks.py"],
    deps = [
        "//src/test/export:_fly_test.so",
        "//src/fly:fly_py",
    ],
    imports = [
        "../export",
        "../../fly",
    ],
)
