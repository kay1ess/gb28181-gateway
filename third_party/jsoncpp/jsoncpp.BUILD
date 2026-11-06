load("@//third_party/bazel:cmake.bzl", "cmake")

filegroup(
    name = "sources",
    srcs = glob(
        ["**"],
        allow_empty = False,
    ),
)

cmake(
    name = "jsoncpp",
    cache_entries = {
        "BUILD_SHARED_LIBS": "OFF",
        "BUILD_STATIC_LIBS": "ON",
        "BUILD_OBJECT_LIBS": "OFF",
        "JSONCPP_WITH_TESTS": "OFF",
        "JSONCPP_WITH_POST_BUILD_UNITTEST": "OFF",
        "CMAKE_CXX_STANDARD": "17",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "CMAKE_POLICY_VERSION_MINIMUM": "3.5",
    },
    copts = select({
        "@//third_party:windows": ["/Zc:__cplusplus"],
        "//conditions:default": [],
    }),
    generate_args = ["-GNinja"],
    lib_source = ":sources",
    out_static_libs = select({
        "@//third_party:windows": ["jsoncpp.lib"],
        "//conditions:default": ["libjsoncpp.a"],
    }),
    visibility = ["//visibility:public"],
)
