GB28181 Gateway third-party dependencies

Layout and build
  Builds require Bazel 9 (>=9.0.0, <10.0.0).
  workspace.bzl registers the dependency list. MODULE.bazel imports its
  Bzlmod extension.
  <library>/workspace.bzl declares the download URLs, SHA-256 checksums,
  submodules, and patch order for that dependency.
  <library>/<library>.BUILD is the BUILD.bazel overlay for that archive.
  <library>/*.patch contains patches for that dependency.
  BUILD.bazel exports public labels, including //third_party:sqlite,
  //third_party:tinyxml2, //third_party:drogon, //third_party:dum,
  //third_party:resiprocate, and //third_party:zlmediakit.
  //third_party:all_deps groups Drogon, reSIProcate, and ZLMediaKit.

  bazel build //apps/server:gateway
  bazel build //third_party:all_deps
  bazel run //third_party/bazel:smoke

Bazel verifies SHA-256 before extracting each archive into its external
repository directory. Patches apply there before compilation. The workspace
does not need third-party source checkouts or git submodule initialization.
The first build requires access to the Bazel registry and the archive hosts.

  SQLite 3.46.1 builds with SQLITE_THREADSAFE=1. TinyXML2 is pinned to 10.0.0.
  rules_foreign_cc runs the original CMake builds for reSIProcate, Drogon,
  JsonCpp, zlib, and ZLMediaKit. Windows uses MSVC, CMake, Ninja and Git Bash.
  Linux also needs the libuuid development files.
  Drogon uses Trantor, zlib and the same pinned JsonCpp revision as ZLMediaKit.
  The ZLMediaKit archive includes the separately downloaded ZLToolKit,
  media-server, and JsonCpp sources. Disabled Python bindings and the
  standalone web assistant do not require their source archives.

Patches
  JsonCpp's CMake patch honors a caller-specified C++ standard.
  Trantor's SHA-1 endian probe recognizes arm64 and i386, not x86_64.
  The Drogon patch marks __x86_64__ and __amd64__ as little endian so
  macOS x64 can compile trantor.
  ZLMediaKit's CMake patch redirects build outputs to the binary directory
  and skips standalone-server assets. Lifecycle and ZLToolKit patches handle
  RTP startup rollback and worker shutdown on Windows. The shared RTP
  patches add fixed-port SSRC admission and allow incoming audio tracks.
  When updating a dependency, update its URL and checksum in its own
  workspace.bzl and verify the patches and build against the new archive.

Runtime
  The ZLMediaKit target builds the shared mk_api C SDK with RTP proxy enabled.
  The adapter defaults to UDP/TCP RTP on port 10000 and RTCP on 10001.
  TCP active uses an outbound socket with an ephemeral local source port.
  Service Stop closes receivers and listeners and permits restart. Only one
  active media adapter per process is supported. Keep the media library with
  the server executable and do not unload it while SDK objects are active.
