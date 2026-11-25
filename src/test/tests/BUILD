# src/test/tests — test 模块单元测试

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_test")
load("@pip//:requirements.bzl", "requirement")

# qa_record.py 是纯标准库模块（qa/runqa 按 importlib 路径加载），测试用同样
# 方式按 runfiles 内路径加载，无需 C++ 扩展依赖。numpy 依赖供规范化规则表
# 的 numpy 标量/数组分支使用。
py_test(
    name = "qa_record_test",
    srcs = ["qa_record_test.py"],
    data = ["//src/test/py:qa_record.py"],
    main = "qa_record_test.py",
    deps = [requirement("numpy")],
)
