"""Register the third-party repositories exposed to the root module."""

load("//third_party/drogon:workspace.bzl", "register_drogon")
load("//third_party/jsoncpp:workspace.bzl", "register_jsoncpp")
load("//third_party/resiprocate:workspace.bzl", "register_resiprocate")
load("//third_party/sqlite:workspace.bzl", "register_sqlite")
load("//third_party/tinyxml2:workspace.bzl", "register_tinyxml2")
load("//third_party/zlib:workspace.bzl", "register_zlib")
load("//third_party/zlmediakit:workspace.bzl", "register_zlmediakit")

def _dependencies_impl(_ctx):
    if native.bazel_version.split(".")[0] != "9":
        fail("GB28181 Gateway requires Bazel 9 (>=9.0.0, <10.0.0); current version: " + native.bazel_version)
    register_drogon()
    register_jsoncpp()
    register_resiprocate()
    register_sqlite()
    register_tinyxml2()
    register_zlib()
    register_zlmediakit()

deps = module_extension(implementation = _dependencies_impl)
