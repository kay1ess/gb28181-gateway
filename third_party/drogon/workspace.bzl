load("//third_party/bazel:archive.bzl", "archive_with_submodules")

_DROGON = [
    "https://codeload.github.com/drogonframework/drogon/tar.gz/a6d52c282be3b7c4ae005dfaaebaa38fcaf8afb4",
    "0b1a11b088a8963acb08eb13f708ca0e0e05dbbe4924c85181b67a69efba6c7a",
    "drogon-a6d52c282be3b7c4ae005dfaaebaa38fcaf8afb4",
]
_TRANTOR = [
    "https://codeload.github.com/an-tao/trantor/tar.gz/350875b76576466a50383d379178c90001179a76",
    "a379c884b0613287d93ffee589e1bbf7149fb9d14e73e1d4cf25d776ab22dc51",
    "trantor-350875b76576466a50383d379178c90001179a76",
]

def register_drogon():
    archive_with_submodules(
        name = "drogon_src",
        archive = _DROGON,
        submodules = {"trantor": _TRANTOR},
        build_file = Label(":drogon.BUILD"),
        patches = [Label(":trantor-sha1-endian.patch")],
    )
