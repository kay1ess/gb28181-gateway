load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "sqlite",
    srcs = ["sqlite3.c"],
    hdrs = [
        "sqlite3.h",
        "sqlite3ext.h",
    ],
    defines = ["SQLITE_THREADSAFE=1"],
    includes = ["."],
    linkopts = select({
        "@//third_party:macos": [],
        "@//third_party:windows": [],
        "//conditions:default": [
            "-pthread",
            "-ldl",
            "-lm",
        ],
    }),
    visibility = ["//visibility:public"],
)
