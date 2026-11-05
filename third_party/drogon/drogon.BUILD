load("@//third_party/bazel:cmake.bzl", "cmake")

filegroup(
    name = "sources",
    srcs = glob(
        ["**"],
        allow_empty = False,
    ),
)

cmake(
    name = "drogon",
    cache_entries = {
        "CMAKE_CXX_STANDARD": "17",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "BUILD_SHARED_LIBS": "OFF",
        "BUILD_CTL": "OFF",
        "BUILD_EXAMPLES": "OFF",
        "BUILD_TESTING": "OFF",
        "BUILD_ORM": "OFF",
        "BUILD_BROTLI": "OFF",
        "BUILD_YAML_CONFIG": "OFF",
        "BUILD_C-ARES": "OFF",
        "USE_SUBMODULE": "ON",
        "USE_STATIC_LIBS_ONLY": "ON",
        "TRANTOR_USE_TLS": "none",
        "CMAKE_TRY_COMPILE_CONFIGURATION": "Release",
        "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDLL",
        "CMAKE_DISABLE_FIND_PACKAGE_OpenSSL": "ON",
    },
    copts = select({
        "@//third_party:windows": ["/D_WIN32_WINNT=0x0A00"],
        "//conditions:default": [],
    }),
    defines = select({
        "@//third_party:windows": ["_WIN32_WINNT=0x0A00"],
        "//conditions:default": [],
    }),
    generate_args = ["-GNinja"],
    lib_source = ":sources",
    linkopts = select({
        "@//third_party:windows": [
            "ws2_32.lib",
            "crypt32.lib",
            "advapi32.lib",
            "rpcrt4.lib",
            "iphlpapi.lib",
        ],
        "//conditions:default": [
            "-pthread",
            "-ldl",
            "-luuid",
        ],
    }),
    out_static_libs = select({
        "@//third_party:windows": [
            "drogon.lib",
            "trantor.lib",
        ],
        "//conditions:default": [
            "libdrogon.a",
            "libtrantor.a",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "@//third_party:jsoncpp",
        "@//third_party:zlib",
    ],
)
