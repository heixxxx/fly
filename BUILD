# Top-level BUILD for Fly project

package(default_visibility = ["//visibility:public"])

load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        "//src/core/cpp:fly_core_cpp": "",
        "//src/core/export:_fly_core.so": "",
        "//src/serialization/cpp:fly_serialization_macros": "",
        "//src/export/cpp:fly_export_macros": "",
    },
)