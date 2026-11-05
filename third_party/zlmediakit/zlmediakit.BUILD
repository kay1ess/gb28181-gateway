load("@//third_party/bazel:cmake.bzl", "cmake")

filegroup(
    name = "sources",
    srcs = glob(
        ["**"],
        allow_empty = False,
    ),
)

cmake(
    name = "zlmediakit",
    cache_entries = {
        "ENABLE_API": "ON",
        "ENABLE_API_STATIC_LIB": "OFF",
        "ENABLE_RTPPROXY": "ON",
        "ENABLE_SERVER": "OFF",
        "ENABLE_TESTS": "OFF",
        "ENABLE_WEBRTC": "OFF",
        "ENABLE_SCTP": "OFF",
        "ENABLE_SRT": "OFF",
        "ENABLE_OPENSSL": "OFF",
        "ENABLE_PYTHON": "OFF",
        "ENABLE_OBJCOPY": "OFF",
        "ENABLE_MSVC_MT": "OFF",
        "DISABLE_REPORT": "ON",
        "CMAKE_POLICY_VERSION_MINIMUM": "3.5",
    },
    generate_args = ["-GNinja"],
    lib_source = ":sources",
    out_dll_dir = "bin",
    out_interface_libs = select({
        "@//third_party:windows": ["mk_api.lib"],
        "//conditions:default": [],
    }),
    out_shared_libs = select({
        "@//third_party:windows": ["mk_api.dll"],
        "//conditions:default": ["libmk_api.so"],
    }),
    visibility = ["//visibility:public"],
)
