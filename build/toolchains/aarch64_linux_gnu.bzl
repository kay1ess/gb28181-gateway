"""Discover the Debian Linux x86_64 -> AArch64 GCC toolchain on demand."""

def _impl(ctx):
    if ctx.os.name != "linux":
        fail("linux_arm64 requires Linux x86_64; run inside Linux, WSL or the development container")
    if not ctx.path("/usr/bin/qemu-aarch64").exists:
        fail("Missing QEMU for configure probes: run bash tools/linux/bootstrap_arm64.sh")
    prefix = "/usr/bin/aarch64-linux-gnu-"
    for tool in ["gcc", "g++", "ar", "ld", "nm", "objcopy", "objdump", "strip", "cpp", "gcov", "dwp"]:
        if not ctx.path(prefix + tool).exists:
            fail("Missing %s: run bash tools/linux/bootstrap_arm64.sh first" % (prefix + tool))
    machine = ctx.execute([prefix + "gcc", "-dumpmachine"])
    if machine.return_code or machine.stdout.strip() != "aarch64-linux-gnu":
        fail("Expected an aarch64-linux-gnu GCC compiler")
    includes = ctx.execute([prefix + "g++", "-E", "-x", "c++", "-v", "/dev/null"])
    if includes.return_code:
        fail(includes.stderr)
    directories = []
    recording = False
    for line in includes.stderr.splitlines():
        if "#include <...> search starts here:" in line:
            recording = True
        elif "End of search list." in line:
            recording = False
        elif recording:
            directories.append(str(ctx.path(line.strip()).realpath))
    if not directories:
        fail("Cannot determine cross compiler builtin include paths")
    if not ctx.path("/usr/lib/aarch64-linux-gnu/libuuid.so").exists:
        fail("Missing ARM64 UUID library: run bash tools/linux/bootstrap_arm64.sh")
    ctx.template("BUILD.bazel", ctx.attr._template, {"%{includes}": repr(directories)})

aarch64_linux_gnu = repository_rule(
    implementation = _impl,
    configure = True,
    local = True,
    attrs = {"_template": attr.label(default = "//build/toolchains:aarch64.BUILD.tpl")},
)
