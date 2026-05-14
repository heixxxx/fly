package(default_visibility = ["//visibility:public"])

# System compression libraries - headers in /usr/include, libs via linkopts
cc_library(
    name = "lz4",
    hdrs = [],  
    includes = ["/usr/include"],
    linkopts = ["-llz4"],
)

cc_library(
    name = "zlib",
    hdrs = [],
    includes = ["/usr/include"],
    linkopts = ["-lz"],
)

cc_library(
    name = "zstd",
    hdrs = [],
    includes = ["/usr/include"],
    linkopts = ["-lzstd"],
)

# Boost Preprocessor (header-only, system library)
cc_library(
    name = "boost_preprocessor",
    hdrs = [],
    includes = ["/usr/include"],
)