load("@//third_party/bazel:cmake.bzl", "cmake")

filegroup(
    name = "sources",
    srcs = glob(
        ["**"],
        allow_empty = False,
    ),
)

cmake(
    name = "zlib",
    cache_entries = {
        "ZLIB_BUILD_EXAMPLES": "OFF",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_POLICY_VERSION_MINIMUM": "3.5",
    },
    generate_args = ["-GNinja"],
    lib_source = ":sources",
    out_static_libs = select({
        "@//third_party:windows": ["zlibstatic.lib"],
        "//conditions:default": ["libz.a"],
    }),
    visibility = ["//visibility:public"],
)
