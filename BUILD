# Top-level BUILD for Fly project

package(default_visibility = ["//visibility:public"])

load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        "//src/core/cpp:config": "",
        "//src/core/export:_core.so": "",
        "//src/serialization/cpp:serialization_macros": "",
        "//src/export/cpp:export_macros": "",
    },
)