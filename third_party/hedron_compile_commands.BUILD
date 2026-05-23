load(":refresh_compile_commands.bzl", "refresh_compile_commands")

exports_files([
    "refresh_compile_commands.bzl",
    "refresh.template.py",
    "check_python_version.template.py",
])

cc_binary(
    name = "print_args",
    srcs = ["print_args.cpp"],
    visibility = ["//visibility:public"],
)
