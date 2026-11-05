load("@rules_cc//cc/toolchains:cc_toolchain.bzl", "cc_toolchain")
# This implementation matches the rules_cc version pinned in MODULE.bazel.
load("@rules_cc//cc/private/toolchain:unix_cc_toolchain_config.bzl", "cc_toolchain_config")

package(default_visibility = ["//visibility:public"])

filegroup(name = "empty")

cc_toolchain_config(
    name = "config",
    cpu = "aarch64",
    compiler = "gcc",
    toolchain_identifier = "debian-aarch64-linux-gnu",
    host_system_name = "x86_64-linux-gnu",
    target_system_name = "aarch64-linux-gnu",
    target_libc = "glibc",
    abi_version = "gcc",
    abi_libc_version = "glibc",
    cxx_builtin_include_directories = %{includes},
    compile_flags = ["-fstack-protector-strong", "-fno-omit-frame-pointer"],
    # Preserve declared Bazel paths through installed-header symlinks.
    unfiltered_compile_flags = ["-fno-canonical-system-headers"],
    cxx_flags = ["-std=c++17"],
    dbg_compile_flags = ["-g"],
    opt_compile_flags = ["-O2", "-DNDEBUG"],
    link_flags = ["-Wl,-z,relro,-z,now"],
    link_libs = ["-lstdc++", "-lm"],
    tool_paths = {
        "gcc": "/usr/bin/aarch64-linux-gnu-gcc",
        "ld": "/usr/bin/aarch64-linux-gnu-ld",
        "ar": "/usr/bin/aarch64-linux-gnu-ar",
        "cpp": "/usr/bin/aarch64-linux-gnu-cpp",
        "gcov": "/usr/bin/aarch64-linux-gnu-gcov",
        "nm": "/usr/bin/aarch64-linux-gnu-nm",
        "objcopy": "/usr/bin/aarch64-linux-gnu-objcopy",
        "objdump": "/usr/bin/aarch64-linux-gnu-objdump",
        "strip": "/usr/bin/aarch64-linux-gnu-strip",
        "dwp": "/usr/bin/aarch64-linux-gnu-dwp",
    },
)

cc_toolchain(
    name = "compiler",
    toolchain_identifier = "debian-aarch64-linux-gnu",
    toolchain_config = ":config",
    all_files = ":empty",
    ar_files = ":empty",
    as_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
)

toolchain(
    name = "toolchain",
    exec_compatible_with = ["@platforms//os:linux", "@platforms//cpu:x86_64"],
    target_compatible_with = ["@platforms//os:linux", "@platforms//cpu:aarch64"],
    toolchain = ":compiler",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

