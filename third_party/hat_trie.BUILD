load("@rules_cc//cc:defs.bzl", "cc_library")

# hat-trie BUILD file
# tsl::htrie_map — header-only hat-trie string-keyed map, used by the
# design db name hasher backend (DSHasherBackendHatrie). MIT license
# (LICENSE in the archive root).

cc_library(
    name = "hat_trie",
    hdrs = glob(["include/tsl/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
