package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_test_py",
    srcs = ["__init__.py", "qa_tmp.py", "qa_wait.py", "qa_record.py", "e2e_tasks.py", "test_tasks.py", "demo_project.py", "test_export.py"],
    deps = [
        "//src/test/export:_fly_test.so",
        "//src/fly:fly_py",
    ],
    imports = [
        "../export",
        "../../fly",
    ],
)

# qa/runqa 与单元测试按路径加载本文件（runqa 无 .py 扩展名不能被导入，
# 共享逻辑都在 qa_record.py），导出文件 label 供 data 引用。
exports_files(["qa_record.py"])
