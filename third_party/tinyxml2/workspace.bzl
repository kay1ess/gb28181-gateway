load("//third_party/bazel:archive.bzl", "github_archive")

_TINYXML2 = [
    "https://codeload.github.com/leethomason/tinyxml2/tar.gz/321ea883b7190d4e85cae5512a12e5eaa8f8731f",
    "d5ae097578717b42b4f05f2c6b2d09f473a1c30d1552ca4e2dcb4eab5b34de84",
    "tinyxml2-321ea883b7190d4e85cae5512a12e5eaa8f8731f",
]

def register_tinyxml2():
    github_archive("tinyxml2", _TINYXML2)
