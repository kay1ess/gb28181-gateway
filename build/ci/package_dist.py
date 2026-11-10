#!/usr/bin/env python3
"""Package one native gateway build into a zip or tar.gz.

The archive contains the gateway executable, the ZLMediaKit shared library
from the same build, the C SDK shared library, the example server config,
the project LICENSE, and license texts from the pinned third-party sources.

Run this from the workspace root after `bazel build`. It packages the
bazel-bin tree of the current output base, so the Bazel config must be the
one just built. Native builds only: the host running this script is the
target platform.
"""

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

PINNED_REPOS = (
    "drogon_src",
    "jsoncpp_src",
    "resiprocate_src",
    "sqlite_src",
    "tinyxml2_src",
    "zlib_src",
    "zlmediakit_src",
)

LICENSE_NAMES = {
    "license",
    "license.txt",
    "license.md",
    "license.mit",
    "copying",
    "copying.txt",
    "copying.md",
    "notice",
    "notice.txt",
    "notice.md",
    "copyright",
}

SKIP_SOURCE_DIRS = {
    ".git",
    "android",
    "doc",
    "docs",
    "example",
    "examples",
    "node_modules",
    "test",
    "tests",
    "testing",
    "www",
}

SKIP_OUTPUT_DIRS = {"_objs", "include", "testlogs", "__pycache__"}

# Imports satisfied by the OS or the Visual C++ redistributable.
WINDOWS_SYSTEM_DLLS = {
    "advapi32.dll",
    "bcrypt.dll",
    "cfgmgr32.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "crypt32.dll",
    "dbghelp.dll",
    "dnsapi.dll",
    "dwmapi.dll",
    "gdi32.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "mpr.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "mswsock.dll",
    "netapi32.dll",
    "normaliz.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "powrprof.dll",
    "psapi.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "setupapi.dll",
    "shell32.dll",
    "shlwapi.dll",
    "ucrtbase.dll",
    "user32.dll",
    "userenv.dll",
    "uxtheme.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "version.dll",
    "winhttp.dll",
    "wininet.dll",
    "winmm.dll",
    "ws2_32.dll",
}

LINUX_SYSTEM_PREFIXES = (
    "ld-linux",
    "libc.so",
    "libcrypt.so",
    "libdl.so",
    "libgcc_s.so",
    "libm.so",
    "libnsl.so",
    "libpthread.so",
    "libresolv.so",
    "librt.so",
    "libstdc++.so",
    "libutil.so",
    "libuuid.so",
    "linux-vdso",
)


def log(message):
    print(message, flush=True)


def run(command, cwd=None):
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise SystemExit(
            "command failed ({})\n{}\nstdout:\n{}\nstderr:\n{}".format(
                result.returncode, " ".join(command), result.stdout, result.stderr
            )
        )
    return result.stdout


def bazel_output_base():
    result = subprocess.run(
        ["bazel", "info", "output_base"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise SystemExit("bazel info output_base failed:\n" + result.stderr)
    for line in reversed(result.stdout.splitlines()):
        candidate = line.strip()
        if not candidate:
            continue
        if candidate.startswith("/") or (len(candidate) > 2 and candidate[1] == ":"):
            return Path(candidate)
    raise SystemExit("cannot parse bazel output_base from:\n" + result.stdout)


def read_header(path, size):
    with path.open("rb") as handle:
        return handle.read(size)


def detect_arch(path):
    header = read_header(path, 64)
    if header.startswith(b"\x7fELF"):
        machine = int.from_bytes(header[18:20], "little")
        if machine == 62:
            return "x86_64"
        if machine == 183:
            return "aarch64"
        raise SystemExit("{} is an ELF binary with e_machine {}".format(path, machine))
    if header[:2] == b"MZ":
        e_lfanew = int.from_bytes(header[0x3C:0x40], "little")
        pe = read_header(path, e_lfanew + 6)
        if pe[e_lfanew : e_lfanew + 4] != b"PE\0\0":
            raise SystemExit("{} is not a PE binary".format(path))
        machine = int.from_bytes(pe[e_lfanew + 4 : e_lfanew + 6], "little")
        if machine == 0x8664:
            return "x86_64"
        if machine == 0xAA64:
            return "aarch64"
        raise SystemExit("{} is a PE binary with machine {:#x}".format(path, machine))
    if len(header) >= 8 and int.from_bytes(header[:4], "little") == 0xFEEDFACF:
        cputype = int.from_bytes(header[4:8], "little")
        if cputype == 0x01000007:
            return "x86_64"
        if cputype == 0x0100000C:
            return "aarch64"
        raise SystemExit("{} is a Mach-O binary with cputype {:#x}".format(path, cputype))
    raise SystemExit("cannot detect the architecture of " + str(path))


def is_elf(path):
    return read_header(path, 4) == b"\x7fELF"


def is_macho(path):
    header = read_header(path, 4)
    return len(header) == 4 and int.from_bytes(header, "little") == 0xFEEDFACF


def is_pe(path):
    return read_header(path, 2) == b"MZ"


def elf_attribute(path, tag):
    output = run(["readelf", "-d", str(path)])
    match = re.search(r"\(" + tag + r"\)\s+.*\[([^\]]+)\]", output)
    return match.group(1) if match else ""


def elf_needed(path):
    output = run(["readelf", "-d", str(path)])
    return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", output)


def elf_rpath(path):
    output = run(["readelf", "-d", str(path)])
    values = re.findall(r"\(R(?:UN)?PATH\)\s+Library (?:rpath|runpath): \[([^\]]+)\]", output)
    return values


def iter_output_files(root):
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        dirnames[:] = [name for name in dirnames if name not in SKIP_OUTPUT_DIRS]
        for filename in filenames:
            yield Path(dirpath) / filename


def score_output(path, prefer):
    text = str(path)
    score = len(text) / 1000.0
    if "runfiles" in text:
        score += 10
    if "_solib" in text:
        score += 5
    for token in prefer:
        if token in text:
            score -= 3
    return score


def find_named(bazel_bin, names, prefer):
    matches = [path for path in iter_output_files(bazel_bin) if path.name in names]
    if not matches:
        return None
    return min(matches, key=lambda path: score_output(path, prefer))


def versioned_library_name(name):
    if name == "libmk_api.so":
        return True
    prefix = "libmk_api.so."
    if not name.startswith(prefix):
        return False
    return all(part.isdigit() for part in name[len(prefix) :].split("."))


def find_with_prefix(bazel_bin, prefix):
    matches = [
        path
        for path in iter_output_files(bazel_bin)
        if path.name == prefix or path.name.startswith(prefix + ".")
    ]
    return matches


def copy_file(src, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest, follow_symlinks=True)
    # Bazel outputs are often mode 0555. patchelf and install_name_tool need
    # to rewrite the copy.
    mode = dest.stat().st_mode
    dest.chmod(mode | stat.S_IRUSR | stat.S_IWUSR)
    return dest


def place_shared_library(src, dest_dir):
    real = src.resolve()
    names = {src.name, real.name}
    if is_elf(real):
        soname = elf_attribute(real, "SONAME")
        if soname:
            names.add(soname)
    placed = []
    for name in sorted(names):
        target = dest_dir / name
        if target.exists():
            placed.append(target)
            continue
        copy_file(real, target)
        placed.append(target)
        log("packed {} from {}".format(name, src))
    return placed


def linux_is_system(name):
    return name.startswith(LINUX_SYSTEM_PREFIXES)


def fix_linux_rpath(binaries):
    for binary in binaries:
        if not is_elf(binary):
            continue
        run(["patchelf", "--set-rpath", "$ORIGIN", str(binary)])
        rpaths = elf_rpath(binary)
        if rpaths != ["$ORIGIN"]:
            raise SystemExit("{} rpath is {}, want $ORIGIN".format(binary, rpaths))


def verify_linux(binaries):
    needed = []
    for binary in binaries:
        if not is_elf(binary):
            continue
        libraries = elf_needed(binary)
        needed.extend(libraries)
        missing = [name for name in libraries if not linux_is_system(name)]
        present = {path.name for path in binary.parent.iterdir()}
        absent = [name for name in missing if name not in present]
        if absent:
            raise SystemExit("{} is missing bundled libraries: {}".format(binary.name, ", ".join(absent)))
        output = run(["ldd", str(binary)], cwd=binary.parent)
        if "not found" in output:
            raise SystemExit("ldd could not resolve {}:\n{}".format(binary.name, output))
        log(binary.name + " ldd:\n" + output.rstrip())
    if not any("mk_api" in name for name in needed):
        raise SystemExit("neither the gateway nor the C SDK depends on libmk_api")


def macho_load_commands(path):
    output = run(["otool", "-l", str(path)])
    rpaths = []
    lines = output.splitlines()
    for index, line in enumerate(lines):
        if line.strip() != "cmd LC_RPATH":
            continue
        for follow in lines[index + 1 : index + 6]:
            if "path " not in follow:
                continue
            rpaths.append(follow.split("path ", 1)[1].rsplit(" (", 1)[0].strip())
            break
    return rpaths


def macho_deps(path):
    output = run(["otool", "-L", str(path)])
    deps = []
    for line in output.splitlines()[1:]:
        text = line.strip()
        if not text:
            continue
        deps.append(text.split(" (", 1)[0])
    return deps


def fix_macos(binaries, library_names):
    for binary in binaries:
        if not is_macho(binary):
            continue
        if binary.suffix == ".dylib":
            run(["install_name_tool", "-id", "@rpath/" + binary.name, str(binary)])
        for dep in macho_deps(binary):
            base = Path(dep).name
            if base not in library_names:
                continue
            updated = "@rpath/" + base
            if dep != updated:
                run(["install_name_tool", "-change", dep, updated, str(binary)])
        for rpath in macho_load_commands(binary):
            run(["install_name_tool", "-delete_rpath", rpath, str(binary)])
        for rpath in ("@executable_path", "@loader_path"):
            run(["install_name_tool", "-add_rpath", rpath, str(binary)])
        run(["codesign", "--force", "--sign", "-", str(binary)])
    for binary in binaries:
        if not is_macho(binary):
            continue
        deps = macho_deps(binary)
        stale = [dep for dep in deps if "bazel" in dep or dep.startswith("/")]
        allowed = [dep for dep in stale if dep.startswith("/usr/lib/") or dep.startswith("/System/")]
        unexpected = [dep for dep in stale if dep not in allowed]
        if unexpected:
            raise SystemExit("{} still references build paths: {}".format(binary.name, ", ".join(unexpected)))
        log(binary.name + " otool -L:\n  " + "\n  ".join(deps))
    joined = "\n".join(dep for binary in binaries for dep in macho_deps(binary))
    if "mk_api" not in joined:
        raise SystemExit("neither the gateway nor the C SDK depends on libmk_api.dylib")


def pe_data_directories(path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise SystemExit(str(path) + " is not a PE file")
    e_lfanew = int.from_bytes(data[0x3C:0x40], "little")
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise SystemExit(str(path) + " is not a PE file")
    coff = e_lfanew + 4
    section_count = int.from_bytes(data[coff + 2 : coff + 4], "little")
    optional_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    optional = coff + 20
    magic = int.from_bytes(data[optional : optional + 2], "little")
    if magic == 0x20B:
        directories = optional + 112
    elif magic == 0x10B:
        directories = optional + 96
    else:
        raise SystemExit("{} has unknown PE magic {:#x}".format(path, magic))
    directory_count = int.from_bytes(data[directories - 4 : directories], "little")
    sections = []
    section = optional + optional_size
    for index in range(section_count):
        offset = section + index * 40
        virtual_size = int.from_bytes(data[offset + 8 : offset + 12], "little")
        virtual_address = int.from_bytes(data[offset + 12 : offset + 16], "little")
        raw_size = int.from_bytes(data[offset + 16 : offset + 20], "little")
        raw_pointer = int.from_bytes(data[offset + 20 : offset + 24], "little")
        sections.append((virtual_address, virtual_size or raw_size, raw_pointer))

    def rva_to_offset(rva):
        for virtual_address, size, raw_pointer in sections:
            if virtual_address <= rva < virtual_address + size:
                return raw_pointer + (rva - virtual_address)
        raise SystemExit("RVA {:#x} is outside {} sections".format(rva, path))

    return data, directories, directory_count, rva_to_offset


def read_cstring(data, offset):
    end = data.index(b"\x00", offset)
    return data[offset:end].decode("ascii")


def pe_imports(path):
    data, directories, directory_count, rva_to_offset = pe_data_directories(path)
    names = []
    if directory_count > 1:
        import_rva = int.from_bytes(data[directories + 8 : directories + 12], "little")
        if import_rva:
            cursor = rva_to_offset(import_rva)
            for _ in range(256):
                if cursor + 20 > len(data) or data[cursor : cursor + 20] == b"\x00" * 20:
                    break
                name_rva = int.from_bytes(data[cursor + 12 : cursor + 16], "little")
                names.append(read_cstring(data, rva_to_offset(name_rva)))
                cursor += 20
    if directory_count > 13:
        delay_rva = int.from_bytes(data[directories + 13 * 8 : directories + 13 * 8 + 4], "little")
        if delay_rva:
            cursor = rva_to_offset(delay_rva)
            for _ in range(256):
                if cursor + 32 > len(data) or data[cursor : cursor + 32] == b"\x00" * 32:
                    break
                name_rva = int.from_bytes(data[cursor + 4 : cursor + 8], "little")
                if name_rva:
                    names.append(read_cstring(data, rva_to_offset(name_rva)))
                cursor += 32
    return names


def windows_is_system(name):
    lowered = name.lower()
    return (
        lowered in WINDOWS_SYSTEM_DLLS
        or lowered.startswith("api-ms-win-")
        or lowered.startswith("ext-ms-win-")
    )


def bundle_windows_imports(binaries, dest_dir, bazel_bin):
    pending = list(binaries)
    seen = set()
    imported = []
    while pending:
        binary = pending.pop()
        if binary in seen or not is_pe(binary):
            continue
        seen.add(binary)
        for name in pe_imports(binary):
            imported.append(name)
            if windows_is_system(name) or (dest_dir / name).exists():
                continue
            found = find_named(bazel_bin, {name}, ["zlmediakit", "external"])
            if found is None:
                raise SystemExit(
                    "{} imports {}, and that DLL is not a known system library or a build output".format(
                        binary.name, name
                    )
                )
            placed = copy_file(found.resolve(), dest_dir / name)
            pending.append(placed)
            log("packed imported {} from {}".format(name, found))
    if not any(name.lower() == "mk_api.dll" for name in imported):
        raise SystemExit("neither the gateway nor the C SDK imports mk_api.dll")


def short_repo_name(dirname):
    for repo in PINNED_REPOS:
        if dirname == repo or dirname.endswith("~" + repo) or dirname.endswith("+" + repo):
            return repo
    return ""


def find_pinned_repos(external):
    if not external.is_dir():
        raise SystemExit("external directory does not exist: " + str(external))
    found = {}
    for child in external.iterdir():
        if not child.is_dir():
            continue
        repo = short_repo_name(child.name)
        if not repo:
            continue
        if repo not in found or child.name == repo:
            found[repo] = child
    missing = [repo for repo in PINNED_REPOS if repo not in found]
    if missing:
        names = sorted(path.name for path in external.iterdir() if path.is_dir())
        raise SystemExit(
            "pinned sources not found: {}\nexternal directories: {}".format(
                ", ".join(missing), ", ".join(names[:100])
            )
        )
    return found


def iter_license_files(repo_dir):
    for dirpath, dirnames, filenames in os.walk(repo_dir, followlinks=False):
        relative = Path(dirpath).relative_to(repo_dir)
        depth = 0 if str(relative) == "." else len(relative.parts)
        dirnames[:] = [
            name
            for name in dirnames
            if name not in SKIP_SOURCE_DIRS and not name.startswith(".")
        ]
        if depth > 4:
            dirnames[:] = []
            continue
        for filename in filenames:
            if filename.lower() in LICENSE_NAMES:
                path = Path(dirpath) / filename
                if path.stat().st_size <= 256 * 1024:
                    yield path


def write_sqlite_notice(repo_dir, dest_dir):
    header = repo_dir / "sqlite3.h"
    if not header.is_file():
        raise SystemExit("sqlite3.h is missing from " + str(repo_dir))
    lines = header.read_text(encoding="utf-8", errors="replace").splitlines()
    banner = []
    for line in lines[:80]:
        banner.append(line)
        if "**********" in line and len(banner) > 5:
            break
    text = "\n".join(banner).strip() + "\n"
    lowered = text.lower()
    if "public domain" not in lowered and "disclaims copyright" not in lowered:
        raise SystemExit("sqlite3.h has no public-domain or copyright-disclaimer banner")
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / "NOTICE_FROM_SQLITE3_H.txt"
    dest.write_bytes(text.encode("utf-8"))
    return dest


def write_zlib_readme(repo_dir, dest_dir):
    readme = None
    for name in ("README", "README.md"):
        candidate = repo_dir / name
        if candidate.is_file():
            readme = candidate
            break
    if readme is None:
        raise SystemExit("zlib sources have no LICENSE or README: " + str(repo_dir))
    text = readme.read_text(encoding="utf-8", errors="replace")
    if "zlib" not in text.lower() or "copyright" not in text.lower():
        raise SystemExit("zlib README does not contain the license text")
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / readme.name
    shutil.copy2(readme, dest)
    return dest


def collect_licenses(output_base, workspace, dest_root):
    dest_root.mkdir(parents=True, exist_ok=True)
    repos = find_pinned_repos(output_base / "external")
    copied = []
    for repo, source in repos.items():
        repo_dest = dest_root / repo
        for license_path in iter_license_files(source):
            relative = license_path.relative_to(source)
            target = repo_dest / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(license_path, target)
            copied.append(target.relative_to(dest_root.parent).as_posix())
        if repo == "sqlite_src":
            notice = write_sqlite_notice(source, repo_dest)
            copied.append(notice.relative_to(dest_root.parent).as_posix())
        if repo == "zlib_src" and not any(path.startswith("third_party_licenses/zlib_src/") for path in copied):
            readme = write_zlib_readme(source, repo_dest)
            copied.append(readme.relative_to(dest_root.parent).as_posix())
    project = dest_root / "project"
    project.mkdir(parents=True, exist_ok=True)
    for relative, name in (
        ("third_party/README.txt", "third_party-README.txt"),
        ("third_party/sqlite/README.txt", "sqlite-README.txt"),
        ("third_party/versions.json", "versions.json"),
    ):
        source = workspace / relative
        if not source.is_file():
            raise SystemExit("missing " + relative)
        target = project / name
        shutil.copy2(source, target)
        copied.append(target.relative_to(dest_root.parent).as_posix())
    blob = "\n".join(copied)
    required = (
        "drogon_src/",
        "trantor/",
        "zlmediakit_src/",
        "ZLToolKit/",
        "resiprocate_src/",
        "tinyxml2_src/",
        "zlib_src/",
        "sqlite_src/",
        "jsoncpp",
    )
    missing = [needle for needle in required if needle not in blob]
    if missing:
        raise SystemExit(
            "third-party license texts are incomplete: {}\ncollected:\n{}".format(
                ", ".join(missing), blob
            )
        )
    index = dest_root / "INDEX.txt"
    index.write_bytes(
        (
            "License texts copied from the pinned sources of this build.\n"
            "versions.json records the upstream revisions.\n\n" + "\n".join(sorted(copied)) + "\n"
        ).encode("utf-8")
    )
    log("collected {} third-party license files".format(len(copied)))
    return copied


def package_readme(gateway, media, sdk):
    return (
        "GB28181 Gateway\n"
        "\n"
        "本目录是一次构建打出来的运行文件，用来集成到第三方系统、对接国标平台，"
        "并把实时拉流交给业务处理。\n"
        "\n"
        "- {gateway}：网关进程\n"
        "- {media}：同一次构建的 ZLMediaKit 动态库，必须和网关、C SDK 放在同一目录\n"
        "- {sdk}：C SDK 共享库\n"
        "- configs/server.example.json：配置示例。复制后再改 api_token 和地址\n"
        "- LICENSE：本仓库的 MIT 许可\n"
        "- third_party_licenses/：这次构建用到的第三方许可文本\n"
        "\n"
        "Linux 和 macOS 在本目录启动：\n"
        "\n"
        "    ./{gateway} ./configs/server.example.json\n"
        "\n"
        "Windows 在本目录启动：\n"
        "\n"
        "    .\\{gateway} .\\configs\\server.example.json\n"
        "\n"
        "示例配置里的令牌只适合本机试验。Linux 包在 Ubuntu 24.04 上链接该系统的 "
        "glibc 和 libuuid。Windows 包需要已安装的 Microsoft Visual C++ 2015–2022 "
        "运行库（x64）。这些二进制没有在麒麟、统信 UOS 或具体国产 CPU 上测试。\n"
    ).format(gateway=gateway, media=media, sdk=sdk)


def make_executable(path):
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_archive(root, archive, stem):
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        archive.unlink()
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as handle:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    arcname = (Path(stem) / path.relative_to(root)).as_posix()
                    handle.write(path, arcname=arcname)
        return
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "w:gz") as handle:
            handle.add(root, arcname=stem)
        return
    raise SystemExit("archive must end in .zip or .tar.gz: " + str(archive))


def archive_members(archive):
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as handle:
            return handle.namelist()
    with tarfile.open(archive, "r:gz") as handle:
        return handle.getnames()


def assert_members(archive, stem, required):
    members = archive_members(archive)
    missing = []
    for name in required:
        wanted = stem + "/" + name
        if wanted not in members and not any(member.startswith(wanted) for member in members):
            missing.append(wanted)
    if missing:
        raise SystemExit(
            "archive is missing {}\nmembers:\n{}".format(", ".join(missing), "\n".join(members))
        )
    log("archive " + str(archive))
    for member in members:
        log("  " + member)


def binary_names():
    if sys.platform.startswith("linux"):
        return "gateway", "libmk_api.so", "libgb28181_c_sdk.so", "linux"
    if sys.platform == "darwin":
        return "gateway", "libmk_api.dylib", "libgb28181_c_sdk.dylib", "darwin"
    if sys.platform == "win32":
        return "gateway.exe", "mk_api.dll", "gb28181_c_sdk.dll", "win32"
    raise SystemExit("unsupported platform " + sys.platform)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--bazel-bin", type=Path, default=Path("bazel-bin"))
    parser.add_argument("--output-base", type=Path)
    parser.add_argument("--workspace", type=Path, default=Path("."))
    parser.add_argument("--expect-arch", choices=("x86_64", "aarch64"))
    args = parser.parse_args()

    workspace = args.workspace.resolve()
    bazel_bin = args.bazel_bin
    if not bazel_bin.is_absolute():
        bazel_bin = workspace / bazel_bin
    if not bazel_bin.is_dir():
        raise SystemExit("bazel-bin does not exist: " + str(bazel_bin))
    gateway_name, media_name, sdk_name, platform = binary_names()

    gateway = find_named(bazel_bin, {gateway_name}, ["apps/server", "apps\\server"])
    sdk = find_named(bazel_bin, {sdk_name}, [])
    media_names = {media_name}
    if platform == "linux":
        media_names.update(
            path.name
            for path in find_with_prefix(bazel_bin, "libmk_api.so")
            if versioned_library_name(path.name)
        )
    media = find_named(bazel_bin, media_names, ["zlmediakit"])
    if gateway is None or sdk is None or media is None:
        raise SystemExit(
            "missing build outputs under {}\ngateway={} sdk={} media={}".format(
                bazel_bin, gateway, sdk, media
            )
        )
    log("gateway: " + str(gateway))
    log("sdk: " + str(sdk))
    log("media: " + str(media))

    output_base = args.output_base.resolve() if args.output_base else bazel_output_base()
    stem = args.archive.name[: -len(".tar.gz")] if args.archive.name.endswith(".tar.gz") else args.archive.stem
    temp_root = Path(tempfile.mkdtemp(prefix="gb28181-pkg-"))
    try:
        root = temp_root / stem
        root.mkdir()
        gateway_out = copy_file(gateway.resolve(), root / gateway_name)
        sdk_out = copy_file(sdk.resolve(), root / sdk_name)
        media_files = place_shared_library(media, root)
        binaries = [gateway_out, sdk_out] + media_files
        for binary in binaries:
            make_executable(binary)
        if args.expect_arch:
            for binary in (gateway_out, sdk_out, media_files[0]):
                actual = detect_arch(binary)
                if actual != args.expect_arch:
                    raise SystemExit(
                        "{} is {}, expected {}".format(binary.name, actual, args.expect_arch)
                    )
                log("{} architecture {}".format(binary.name, actual))
        if platform == "linux":
            fix_linux_rpath([path for path in binaries if is_elf(path)])
            verify_linux([gateway_out, sdk_out])
        elif platform == "darwin":
            library_names = {path.name for path in media_files}
            library_names.add(sdk_name)
            fix_macos([gateway_out, sdk_out] + media_files, library_names)
        else:
            bundle_windows_imports([gateway_out, sdk_out], root, bazel_bin)
            for binary in (gateway_out, sdk_out):
                log("{} imports: {}".format(binary.name, ", ".join(pe_imports(binary))))

        license_path = workspace / "LICENSE"
        config_path = workspace / "configs" / "server.example.json"
        if not license_path.is_file() or not config_path.is_file():
            raise SystemExit("LICENSE or configs/server.example.json is missing")
        copy_file(license_path, root / "LICENSE")
        copy_file(config_path, root / "configs" / "server.example.json")
        (root / "README.txt").write_bytes(
            package_readme(gateway_name, media_files[0].name, sdk_name).encode("utf-8")
        )
        collect_licenses(output_base, workspace, root / "third_party_licenses")
        archive = args.archive if args.archive.is_absolute() else workspace / args.archive
        write_archive(root, archive, stem)
        assert_members(
            archive,
            stem,
            [
                gateway_name,
                media_files[0].name,
                sdk_name,
                "configs/server.example.json",
                "LICENSE",
                "README.txt",
                "third_party_licenses/",
            ],
        )
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    main()
