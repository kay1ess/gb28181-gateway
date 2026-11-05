load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_sqlite():
    http_archive(
        name = "sqlite_src",
        urls = ["https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"],
        sha256 = "77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784",
        strip_prefix = "sqlite-amalgamation-3460100",
        build_file = Label(":sqlite.BUILD"),
    )
