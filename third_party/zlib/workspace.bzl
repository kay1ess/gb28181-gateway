load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_zlib():
    http_archive(
        name = "zlib_src",
        urls = ["https://zlib.net/fossils/zlib-1.3.1.tar.gz"],
        sha256 = "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23",
        strip_prefix = "zlib-1.3.1",
        build_file = Label(":zlib.BUILD"),
    )
