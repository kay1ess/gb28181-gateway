#ifndef GB28181_INCLUDE_GB28181_TYPES_H_
#define GB28181_INCLUDE_GB28181_TYPES_H_
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gb28181/error.h"

namespace gb28181 {
// Opaque identifiers: internal resource IDs must include sharing provenance,
// rather than treating the GB device code as a globally unique resource ID.
using PlatformId = std::string;
using ResourceId = std::string;
using OperationId = std::string;
using PlaybackId = std::string;
using PlaybackLeaseId = std::string;
using SipSessionId = std::string;
using ReceiverId = std::string;

enum class OnlineState {
  kUnknown,
  kOnline,
  kOffline
};

enum class RtpReceiveMode {
  kUdp = 0,
  kTcpPassive = 1,
  kTcpActive = 2
};

enum class PlaybackState {
  kNegotiating,
  kWaitingMedia,
  kPlaying,
  kStopping,
  kEnded,
  kFailed
};

enum class OperationState {
  kRunning,
  kSucceeded,
  kFailed,
  kCancelled
};

struct PlatformConfig {
  PlatformId id;
  std::string gb_id;
  std::string name;
  std::string realm;
  std::string username;
  std::string password;
  bool enabled = true;
  uint32_t heartbeat_timeout_seconds = 180;
  RtpReceiveMode default_receive_mode = RtpReceiveMode::kUdp;
};

struct Platform {
  PlatformConfig config;
  OnlineState state = OnlineState::kUnknown;
  int64_t last_seen_ms = 0;
  std::string remote_address;
  uint16_t remote_port = 0;
  std::string sip_transport;
};

struct Resource {
  ResourceId id;
  PlatformId platform_id;
  std::string gb_id;
  std::string parent_gb_id;
  std::string name;
  std::string manufacturer;
  std::string model;
  std::string address;
  std::string civil_code;
  bool parental = false;
  OnlineState state = OnlineState::kUnknown;
  int64_t updated_ms = 0;
};

struct ResourceFilter {
  PlatformId platform_id;
  std::string keyword;
  uint32_t offset = 0;
  uint32_t limit = 100;
  std::optional<OnlineState> state;
};

struct DeviceInfo {
  ResourceId resource_id;
  std::string name;
  std::string manufacturer;
  std::string model;
  std::string firmware;
  int64_t queried_ms = 0;
};

struct DeviceStatus {
  ResourceId resource_id;
  OnlineState state = OnlineState::kUnknown;
  std::string status;
  int64_t queried_ms = 0;
};

struct Operation {
  OperationId id;
  PlatformId platform_id;
  OperationState state = OperationState::kRunning;
  uint32_t received = 0;
  uint32_t expected = 0;
  Error error{ErrorCode::kOk, {}, {}};
};

struct PlaybackEndpoint {
  std::string protocol;
  std::string url;
};

struct Playback {
  PlaybackId id;
  ResourceId resource_id;
  RtpReceiveMode mode = RtpReceiveMode::kUdp;
  PlaybackState state = PlaybackState::kNegotiating;
  uint32_t ssrc = 0;
  uint32_t consumers = 0;
  Error error{ErrorCode::kOk, {}, {}};
  std::vector<PlaybackEndpoint> endpoints;
  // Unix milliseconds for diagnostics. media_ready_ms means valid tracks,
  // not just the first RTP packet; ready_ms also requires SIP negotiation.
  int64_t created_ms = 0;
  int64_t negotiated_ms = 0;
  int64_t media_ready_ms = 0;
  int64_t ready_ms = 0;
};

struct PlaybackLease {
  PlaybackLeaseId id;
  PlaybackId playback_id;
  int64_t expires_ms = 0;
};

struct StartLiveRequest {
  ResourceId resource_id;
  // Omit to resolve the platform default when the request is handled.
  std::optional<RtpReceiveMode> mode;
};

struct Statistics {
  uint32_t online_platforms = 0;
  uint32_t active_queries = 0;
  uint32_t active_playbacks = 0;
  uint32_t active_receivers = 0;
  uint32_t playback_leases = 0;
  // Last SIP-loop snapshot; excludes completed transaction retransmit timers.
  uint32_t sip_sessions = 0;
  uint32_t sip_requests = 0;
};
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_TYPES_H_
