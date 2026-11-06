load("@//third_party/bazel:cmake.bzl", "cmake")

filegroup(
    name = "sources",
    srcs = glob(
        ["**"],
        allow_empty = False,
    ),
)

cmake(
    name = "resiprocate",
    cache_entries = {
        "CMAKE_CXX_STANDARD": "17",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "BUILD_SHARED_LIBS": "OFF",
        "WITH_SSL": "OFF",
        "WITH_C_ARES": "OFF",
        "USE_POPT": "OFF",
        "USE_MAXMIND_GEOIP": "OFF",
        "USE_NUGET": "OFF",
        "USE_CONTRIB": "ON",
        "USE_DTLS": "OFF",
        "BUILD_REPRO": "OFF",
        "BUILD_RETURN": "OFF",
        "BUILD_REFLOW": "OFF",
        "BUILD_MEDIA": "OFF",
        "BUILD_REND": "OFF",
        "BUILD_TFM": "OFF",
        "BUILD_RECON": "OFF",
        "BUILD_TESTING": "OFF",
        "BUILD_QPID_PROTON": "OFF",
        "BUILD_DSO_PLUGINS": "OFF",
        "REGENERATE_MEDIA_SAMPLES": "OFF",
        "ENABLE_LOG_REPOSITORY_DETAILS": "OFF",
    },
    defines = select({
        "@//third_party:windows": ["WIN32"],
        "//conditions:default": [],
    }),
    generate_args = ["-GNinja"],
    lib_source = ":sources",
    linkopts = select({
        "@//third_party:windows": [
            "ws2_32.lib",
            "iphlpapi.lib",
            "winmm.lib",
            "advapi32.lib",
        ],
        "//conditions:default": [
            "-pthread",
            "-ldl",
        ],
    }),
    out_static_libs = select({
        "@//third_party:windows": [
            "dum.lib",
            "resiprocate.lib",
            "rutil.lib",
            "resipares.lib",
        ],
        "//conditions:default": [
            "libdum.a",
            "libresip.a",
            "librutil.a",
            "libresipares.a",
        ],
    }),
    visibility = ["//visibility:public"],
)
