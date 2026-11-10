"""Apply target system settings to all project-owned foreign CMake rules."""

load("@rules_foreign_cc//foreign_cc:defs.bzl", _cmake = "cmake")

def cmake(name, cache_entries = {}, **kwargs):
    # These entries are for the x64-hosted aarch64 cross compile only.
    # A native aarch64 host has the same OS and CPU constraints, and its
    # system compiler is not the Debian cross toolchain.
    cross_entries = dict(cache_entries)
    cross_entries.update({
        "CMAKE_CROSSCOMPILING_EMULATOR": "/usr/bin/qemu-aarch64;-L;/usr/aarch64-linux-gnu",
        # rules_foreign_cc 0.16 derives SYSTEM_NAME/PROCESSOR from the Bazel
        # platform. Repeating them here concatenates the values in that version.
        "CMAKE_FIND_ROOT_PATH": "$$EXT_BUILD_DEPS;/usr/aarch64-linux-gnu;/usr",
        "CMAKE_FIND_ROOT_PATH_MODE_PROGRAM": "NEVER",
        "CMAKE_FIND_ROOT_PATH_MODE_LIBRARY": "ONLY",
        "CMAKE_FIND_ROOT_PATH_MODE_INCLUDE": "ONLY",
        "CMAKE_FIND_ROOT_PATH_MODE_PACKAGE": "ONLY",
        "CMAKE_LIBRARY_ARCHITECTURE": "aarch64-linux-gnu",
    })
    if name == "drogon":
        cross_entries.update({
            "UUID_LIBRARY": "/usr/lib/aarch64-linux-gnu/libuuid.so",
            "UUID_INCLUDE_DIR": "/usr/include/uuid",
        })
    _cmake(
        name = name,
        cache_entries = select({
            Label("//third_party:linux_arm64_cross"): cross_entries,
            "//conditions:default": cache_entries,
        }),
        env = select({
            Label("//third_party:linux_arm64_cross"): {
                "CC": "/usr/bin/aarch64-linux-gnu-gcc",
                "CXX": "/usr/bin/aarch64-linux-gnu-g++",
            },
            "//conditions:default": {},
        }),
        **kwargs
    )
