load("@rules_cc//cc:defs.bzl", "cc_library")

# bitsery BUILD file
# bitsery is a header-only C++ serialization library with versioning support

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "bitsery",
    hdrs = glob(["include/bitsery/**/*.h"]),
    includes = ["include"],
    copts = ["-std=c++20"],
)