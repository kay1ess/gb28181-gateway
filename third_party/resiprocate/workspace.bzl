load("//third_party/bazel:archive.bzl", "github_archive")

_RESIPROCATE = [
    "https://codeload.github.com/resiprocate/resiprocate/tar.gz/44eb4c86d3109acb7db819f43566b4eeb29deb04",
    "3def6541cf4930c90d42fec9036ccf2630846355201035b4670056f3d92f2c58",
    "resiprocate-44eb4c86d3109acb7db819f43566b4eeb29deb04",
]

def register_resiprocate():
    github_archive("resiprocate", _RESIPROCATE)
