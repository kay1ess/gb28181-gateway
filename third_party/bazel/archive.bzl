"""Helpers for downloading and patching pinned source archives."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _archive_with_submodules_impl(ctx):
    for directory, archive in [("", ctx.attr.archive)] + ctx.attr.submodules.items():
        url, sha256, prefix = archive
        ctx.download_and_extract(
            url = url,
            sha256 = sha256,
            stripPrefix = prefix,
            output = directory,
            type = "tar.gz",
        )

    # The nested JsonCpp sources belong to ZLM's CMake source tree.
    for path in ctx.attr.remove_build_files:
        ctx.delete(path)
    for patch in ctx.attr.patches:
        ctx.patch(patch, strip = 1)
    ctx.file("BUILD.bazel", ctx.read(ctx.attr.build_file), executable = False)

archive_with_submodules = repository_rule(
    implementation = _archive_with_submodules_impl,
    attrs = {
        "archive": attr.string_list(mandatory = True),
        "build_file": attr.label(mandatory = True, allow_single_file = True),
        "patches": attr.label_list(allow_files = [".patch"]),
        "remove_build_files": attr.string_list(),
        "submodules": attr.string_list_dict(mandatory = True),
    },
)

def github_archive(name, archive, patches = []):
    url, sha256, prefix = archive
    http_archive(
        name = name + "_src",
        urls = [url],
        sha256 = sha256,
        strip_prefix = prefix,
        type = "tar.gz",
        build_file = Label("//third_party/{}:{}.BUILD".format(name, name)),
        patches = patches,
        patch_args = ["-p1"],
    )
