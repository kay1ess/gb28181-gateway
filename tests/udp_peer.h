#ifndef GB28181_TESTS_UDP_PEER_H_
#define GB28181_TESTS_UDP_PEER_H_
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace gb28181::test {
class UdpPeer {
 public:
  explicit UdpPeer(uint16_t preferred_port = 0) {
#ifdef _WIN32
    static const auto initialized = [] {
      WSADATA data{};
      return WSAStartup(MAKEWORD(2, 2), &data);
    }();
    if (initialized)
      throw std::runtime_error("WSAStartup failed");
#endif
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == kInvalid)
      throw std::runtime_error("socket failed");
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(preferred_port);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) !=
        0) {
      Close();
      throw std::runtime_error("bind failed");
    }
#ifdef _WIN32
    int length = sizeof(local);
    const DWORD timeout = 3000;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    socklen_t length = sizeof(local);
    const timeval timeout{3, 0};
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    getsockname(socket_, reinterpret_cast<sockaddr*>(&local), &length);
    port_ = ntohs(local.sin_port);
  }

  ~UdpPeer() {
    Close();
  }

  UdpPeer(const UdpPeer&) = delete;
  UdpPeer& operator=(const UdpPeer&) = delete;

  uint16_t port() const {
    return port_;
  }

  bool CanBindTcp() const {
    const auto probe = socket(AF_INET, SOCK_STREAM, 0);
    if (probe == kInvalid)
      return false;
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    endpoint.sin_port = htons(port_);
    const bool available = bind(probe, reinterpret_cast<sockaddr*>(&endpoint),
                                sizeof(endpoint)) == 0;
#ifdef _WIN32
    closesocket(probe);
#else
    close(probe);
#endif
    return available;
  }

  void Send(uint16_t port, const std::string& message) {
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    remote.sin_port = htons(port);
    const int sent =
        sendto(socket_, message.data(), static_cast<int>(message.size()), 0,
               reinterpret_cast<sockaddr*>(&remote), sizeof(remote));
    if (sent != static_cast<int>(message.size()))
      throw std::runtime_error("UDP send failed");
  }

  std::string Receive() {
    char buffer[65536];
    const int count = recv(socket_, buffer, sizeof(buffer), 0);
    if (count <= 0)
      throw std::runtime_error("UDP receive timed out or failed");
    return std::string(buffer, count);
  }

 private:
  void Close() {
    if (socket_ == kInvalid)
      return;
#ifdef _WIN32
    closesocket(socket_);
#else
    close(socket_);
#endif
    socket_ = kInvalid;
  }
#ifdef _WIN32
  using Socket = SOCKET;
  static constexpr Socket kInvalid = INVALID_SOCKET;
#else
  using Socket = int;
  static constexpr Socket kInvalid = -1;
#endif
  Socket socket_ = kInvalid;
  uint16_t port_ = 0;
};

inline uint16_t UnusedSipPort() {
  const auto seed = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  for (uint64_t attempt = 0; attempt < 512; ++attempt) {
    // Windows/Docker may reserve different, large contiguous blocks for UDP
    // and TCP. Probe dispersed candidates with both sockets, rather than
    // relying on either protocol's ephemeral allocator to find their overlap.
    const auto candidate =
        static_cast<uint16_t>(10240 + (seed + attempt * 137) % 45000);
    try {
      UdpPeer peer(candidate);
      if (peer.CanBindTcp())
        return peer.port();
    } catch (const std::runtime_error&) {
    }
  }
  throw std::runtime_error("no UDP/TCP SIP port available");
}

inline uint16_t UnusedRtpPort() {
  for (int attempt = 0; attempt < 512; ++attempt) {
    const auto port = UnusedSipPort();
    try {
      UdpPeer rtp(port);
      UdpPeer rtcp(static_cast<uint16_t>(port + 1));
      return port;
    } catch (const std::runtime_error&) {
    }
  }
  throw std::runtime_error("no RTP/RTCP pair available");
}

class TcpPortBlocker {
 public:
  explicit TcpPortBlocker(uint16_t port) {
    UdpPeer socket_runtime;
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == kInvalid)
      throw std::runtime_error("TCP blocker socket");
#ifdef _WIN32
    const BOOL exclusive = TRUE;
    setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(port);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) !=
            0 ||
        listen(socket_, 1) != 0) {
      Close();
      throw std::runtime_error("TCP blocker bind");
    }
  }

  ~TcpPortBlocker() {
    Close();
  }

  TcpPortBlocker(const TcpPortBlocker&) = delete;
  TcpPortBlocker& operator=(const TcpPortBlocker&) = delete;

 private:
  void Close() {
    if (socket_ == kInvalid)
      return;
#ifdef _WIN32
    closesocket(socket_);
#else
    close(socket_);
#endif
    socket_ = kInvalid;
  }
#ifdef _WIN32
  using Socket = SOCKET;
  static constexpr Socket kInvalid = INVALID_SOCKET;
#else
  using Socket = int;
  static constexpr Socket kInvalid = -1;
#endif
  Socket socket_ = kInvalid;
};
}  // namespace gb28181::test
#endif  // GB28181_TESTS_UDP_PEER_H_
