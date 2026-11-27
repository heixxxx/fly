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

# 推送门禁（scripts/push 与 scripts/pre-push）的行为测试：在临时 git 仓库里做真实的
# git push（本地裸仓库当远端，不联网、不跑门禁），核对标记文件的放行与拦截规则，以及
# scripts/push 在门禁之前的前置检查。两个脚本按 runfiles 内路径读取，无 deps。
py_test(
    name = "push_hook_test",
    srcs = ["push_hook_test.py"],
    data = [
        "//scripts:pre-push",
        "//scripts:push",
    ],
    main = "push_hook_test.py",
)

# qa_wait.py 同样按路径加载：wait_tasks_no_failure 的记录调用经模块属性转发，测试里
# 替换成桩，无需 fly 与真实记录接口（纯标准库，无额外 deps）。
py_test(
    name = "qa_wait_test",
    srcs = ["qa_wait_test.py"],
    data = ["//src/test/py:qa_wait.py"],
    main = "qa_wait_test.py",
)
