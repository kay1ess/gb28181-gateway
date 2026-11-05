load("//third_party/bazel:archive.bzl", "archive_with_submodules")
load("//third_party/jsoncpp:workspace.bzl", "JSONCPP_ARCHIVE")

_ZLMEDIAKIT = [
    "https://codeload.github.com/ZLMediaKit/ZLMediaKit/tar.gz/fb3265725f0880da1f52a3cc6e6eb04bbc327fa3",
    "2edb167b5d9d8c23e57d48cdedf6c692dfcdbcb48287b3d7596e65d25228d3a2",
    "ZLMediaKit-fb3265725f0880da1f52a3cc6e6eb04bbc327fa3",
]
_ZLTOOLKIT = [
    "https://codeload.github.com/ZLMediaKit/ZLToolKit/tar.gz/d7d601200f65f3111dbf9d20ed7f14d2654bbd6e",
    "0650a9af5a8ad71cdf6fbc1383cb5335eaaa1e8aa5b6b66dd37a2f82f20d148d",
    "ZLToolKit-d7d601200f65f3111dbf9d20ed7f14d2654bbd6e",
]
_MEDIA_SERVER = [
    "https://codeload.github.com/ireader/media-server/tar.gz/21c4451ff2e4c4bb1c817e606c8b4e5deac1e719",
    "ac91fbd9f815e8869a889f809ff4a68a21fe1c5251b1bb100ffac6d793d26151",
    "media-server-21c4451ff2e4c4bb1c817e606c8b4e5deac1e719",
]

def register_zlmediakit():
    archive_with_submodules(
        name = "zlmediakit_src",
        archive = _ZLMEDIAKIT,
        submodules = {
            "3rdpart/ZLToolKit": _ZLTOOLKIT,
            "3rdpart/jsoncpp": JSONCPP_ARCHIVE,
            "3rdpart/media-server": _MEDIA_SERVER,
        },
        remove_build_files = [
            "3rdpart/jsoncpp/BUILD.bazel",
            "3rdpart/jsoncpp/example/BUILD.bazel",
        ],
        build_file = Label(":zlmediakit.BUILD"),
        patches = [
            Label(":zlmediakit-cmake.patch"),
            Label(":zlmediakit-lifecycle.patch"),
            Label(":zlmediakit-rtp-start-cleanup.patch"),
            Label(":zlmediakit-shared-rtp.patch"),
            Label(":zlmediakit-shared-rtp-audio.patch"),
            Label(":zltoolkit-shutdown.patch"),
        ],
    )
