load("//third_party/bazel:archive.bzl", "github_archive")

JSONCPP_ARCHIVE = [
    "https://codeload.github.com/open-source-parsers/jsoncpp/tar.gz/ca98c98457b1163cca1f7d8db62827c115fec6d1",
    "ed9259c30e450a1e86087f351eb84d85b5393e21cd5bfa9313b1dceed71ec7c8",
    "jsoncpp-ca98c98457b1163cca1f7d8db62827c115fec6d1",
]

def register_jsoncpp():
    github_archive("jsoncpp", JSONCPP_ARCHIVE, [Label(":jsoncpp-cxx-standard.patch")])
