#include "src/application/application.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "src/application/catalog_accumulator.h"
#include "src/core/task_runner.h"

namespace gb28181 {
namespace {
using Clock = std::chrono::steady_clock;

int64_t WallTime() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

Error Failure(ErrorCode code, const std::string& trace = {}) {
  return {code, ErrorMessage(code), trace};
}

bool Digits(const std::string& value, size_t size) {
  return value.size() == size &&
         std::all_of(value.begin(), value.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

bool Identifier(const std::string& value) {
  return !value.empty() && value.size() <= 64 &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
         });
}

bool Ipv4(const std::string& address) {
  std::istringstream input(address);
  std::string part;
  int count = 0;
  while (std::getline(input, part, '.')) {
    uint32_t number = 0;
    const auto parsed =
        std::from_chars(part.data(), part.data() + part.size(), number);
    if (part.empty() || parsed.ec != std::errc() ||
        parsed.ptr != part.data() + part.size() || number > 255)
      return false;
    ++count;
  }
  return count == 4 && !address.empty() && address.back() != '.' &&
         address != "0.0.0.0";
}

bool Active(PlaybackState state) {
  return state != PlaybackState::kFailed && state != PlaybackState::kEnded;
}

template <typename T>
struct Request {
  std::string id;
  RequestOptions options;
  Completion<T> completion;
  bool done = false;
};

template <typename T>
using RequestPtr = std::shared_ptr<Request<T>>;

class State : public std::enable_shared_from_this<State> {
 public:
  State(std::shared_ptr<IRepository> repository,
        std::shared_ptr<IGb28181Service> sip,
        std::shared_ptr<IMediaService> media)
      : repository_(std::move(repository)),
        sip_(std::move(sip)),
        media_(std::move(media)) {
  }

  Result<void> Initialize(const Config& config) {
    config_ = config;
    if (config.database_path.empty() || !Digits(config.server_gb_id, 20) ||
        !Digits(config.realm, 10) || !config.sip_port ||
        !config.query_timeout_ms || !config.media_timeout_ms ||
        !config.lease_seconds || !config.max_playbacks ||
        config.rtp_port == 65535 || config.max_playbacks > 4096 ||
        config.lease_seconds > 86400)
      return Failure(ErrorCode::kInvalidArgument);
    if (!Ipv4(config.advertised_address))
      return Failure(ErrorCode::kInvalidArgument);
    const auto opened = repository_->Open(config.database_path);
    if (!opened)
      return opened.error();
    auto platforms = repository_->ListPlatforms();
    if (!platforms)
      return platforms.error();
    for (const auto& platform : platforms.value())
      platforms_[platform.id].snapshot.config = platform;
    run_id_ = std::to_string(WallTime()) + "_" +
              std::to_string(std::random_device{}());
    accepting_.store(true);
    std::weak_ptr<State> weak = shared_from_this();
    auto media_started = media_->Start(config, [weak](MediaEvent event) {
      if (auto self = weak.lock())
        self->Post([self, event = std::move(event)] { self->OnMedia(event); });
    });
    if (!media_started)
      return media_started.error();
    auto sip_started =
        sip_->Start(config, platforms.value(), [weak](SipEvent event) {
          if (auto self = weak.lock())
            self->Post(
                [self, event = std::move(event)] { self->OnSip(event); });
        });
    if (!sip_started)
      return sip_started.error();
    Post([self = shared_from_this()] { self->Tick(); });
    return {};
  }

  bool InCallback() const {
    return callbacks_.IsCurrent() || actor_.IsCurrent() || storage_.IsCurrent();
  }

  Result<void> Shutdown() {
    if (InCallback())
      return Failure(ErrorCode::kInvalidState);
    accepting_.store(false);
    auto settled = std::make_shared<std::promise<void>>();
    auto completed = settled->get_future();
    actor_.Post([this, settled] {
      for (auto& query : queries_)
        query.second.fail(Failure(ErrorCode::kCancelled));
      queries_.clear();
      for (auto& playback : playbacks_)
        EndPlayback(playback.second, Failure(ErrorCode::kCancelled));
      std::vector<std::function<void(Error)>> failures;
      for (const auto& request : pending_)
        failures.push_back(request.second.fail);
      for (auto& fail : failures)
        fail(Failure(ErrorCode::kCancelled));
      settled->set_value();
    });
    completed.wait();
    {
      std::lock_guard<std::mutex> lock(subscriptions_mutex_);
      for (auto& subscription : subscriptions_)
        subscription.second->active.store(false);
      subscriptions_.clear();
    }
    sip_->Stop();
    media_->Stop();
    storage_.Stop();
    actor_.Stop();
    callbacks_.Drain();
    callbacks_.Stop();
    repository_->Close();
    return {};
  }

  template <typename T, typename Operation>
  Result<void> Submit(Completion<T> completion,
                      RequestOptions options,
                      Operation operation) {
    if (!completion || options.timeout.count() <= 0)
      return Failure(ErrorCode::kInvalidArgument);
    if (!accepting_.load())
      return Failure(ErrorCode::kInvalidState);
    auto request = std::make_shared<Request<T>>();
    request->options = std::move(options);
    request->completion = std::move(completion);
    auto self = shared_from_this();
    const auto submitted = Clock::now();
    if (!actor_.Post([self, request, operation = std::move(operation),
                      submitted]() mutable {
          request->id = self->Id("request");
          if (request->options.trace_id.empty())
            request->options.trace_id = request->id;
          if (!self->accepting_.load()) {
            self->Finish<T>(request, Failure(ErrorCode::kCancelled));
            return;
          }
          if (self->pending_.size() >= 1024) {
            self->Finish<T>(request, Failure(ErrorCode::kResourceExhausted));
            return;
          }
          self->pending_.emplace(
              request->id, Pending{submitted + request->options.timeout,
                                   request->options.cancellation,
                                   [self, request](Error error) {
                                     self->Finish<T>(request, std::move(error));
                                   }});
          if (request->options.cancellation.cancelled()) {
            self->Finish<T>(request, Failure(ErrorCode::kCancelled));
            return;
          }
          if (Clock::now() >= submitted + request->options.timeout) {
            self->Finish<T>(request, Failure(ErrorCode::kTimeout));
            return;
          }
          try {
            operation(*self, request);
          } catch (...) {
            self->Finish<T>(request, Failure(ErrorCode::kInternalError));
          }
        }))
      return Failure(ErrorCode::kInvalidState);
    return {};
  }

  template <typename T>
  void Finish(const RequestPtr<T>& request, Result<T> result) {
    if (request->done)
      return;
    request->done = true;
    pending_.erase(request->id);
    if (!result.ok()) {
      auto error = result.error();
      error.trace_id = request->options.trace_id;
      result = std::move(error);
    }
    auto callback = std::move(request->completion);
    callbacks_.Post(
        [callback = std::move(callback), result = std::move(result)]() mutable {
          callback(std::move(result));
        });
  }

  template <typename T, typename Work, typename Continuation>
  void Database(Work work, Continuation continuation) {
    auto self = shared_from_this();
    storage_.Post([self, work = std::move(work),
                   continuation = std::move(continuation)]() mutable {
      Result<T> result = Failure(ErrorCode::kInternalError);
      try {
        result = work(*self->repository_);
      } catch (...) {
      }
      self->actor_.Post([self, result = std::move(result),
                         continuation = std::move(continuation)]() mutable {
        if (self->accepting_.load())
          continuation(std::move(result));
      });
    });
  }

  void SavePlatform(PlatformConfig platform,
                    bool create,
                    const RequestPtr<Platform>& request) {
    if (create && platform.id.empty())
      platform.id = Id("lower");
    if (!Identifier(platform.id) || !Digits(platform.gb_id, 20) ||
        (!platform.realm.empty() && platform.realm != config_.realm) ||
        !platform.heartbeat_timeout_seconds ||
        (platform.default_receive_mode != RtpReceiveMode::kUdp &&
         platform.default_receive_mode != RtpReceiveMode::kTcpPassive &&
         platform.default_receive_mode != RtpReceiveMode::kTcpActive)) {
      Finish<Platform>(request, Failure(ErrorCode::kInvalidArgument));
      return;
    }
    platform.realm = config_.realm;
    Database<void>(
        [platform, create](IRepository& repository) {
          return repository.SavePlatform(platform, create);
        },
        [self = shared_from_this(), platform, request](Result<void> result) {
          if (!result) {
            self->Finish<Platform>(request, result.error());
            return;
          }
          auto& live = self->platforms_[platform.id];
          const auto old = live.snapshot.config;
          if (!platform.enabled || old.gb_id != platform.gb_id ||
              old.password != platform.password ||
              old.username != platform.username) {
            self->LosePlatform(platform.id,
                               Failure(ErrorCode::kPlatformOffline));
            live.snapshot.state = OnlineState::kUnknown;
            live.registered = false;
          }
          live.snapshot.config = platform;
          self->PushPlatforms();
          self->Publish({EventType::kPlatformChanged,
                         platform.id,
                         request->options.trace_id,
                         {ErrorCode::kOk, {}, {}}});
          self->Finish<Platform>(request, live.snapshot);
        });
  }

  void RemovePlatform(const PlatformId& id, const RequestPtr<void>& request) {
    Database<void>(
        [id](IRepository& repository) { return repository.RemovePlatform(id); },
        [self = shared_from_this(), id, request](Result<void> result) {
          if (!result) {
            self->Finish<void>(request, result.error());
            return;
          }
          self->LosePlatform(id, Failure(ErrorCode::kPlatformOffline));
          self->platforms_.erase(id);
          self->PushPlatforms();
          self->Publish({EventType::kPlatformChanged,
                         id,
                         request->options.trace_id,
                         {ErrorCode::kOk, {}, {}}});
          self->Finish<void>(request, {});
        });
  }

  void GetPlatform(const PlatformId& id, const RequestPtr<Platform>& request) {
    auto found = platforms_.find(id);
    if (found == platforms_.end())
      Finish<Platform>(request, Failure(ErrorCode::kNotFound));
    else
      Finish<Platform>(request, found->second.snapshot);
  }

  void ListPlatforms(const RequestPtr<std::vector<Platform>>& request) {
    std::vector<Platform> result;
    for (const auto& item : platforms_)
      result.push_back(item.second.snapshot);
    Finish<std::vector<Platform>>(request, std::move(result));
  }

  void GetResource(const ResourceId& id, const RequestPtr<Resource>& request) {
    Database<Resource>(
        [id](IRepository& repository) { return repository.FindResource(id); },
        [self = shared_from_this(), request](Result<Resource> result) {
          self->Finish<Resource>(request, std::move(result));
        });
  }

  void ListResources(ResourceFilter filter,
                     const RequestPtr<std::vector<Resource>>& request) {
    Database<std::vector<Resource>>(
        [filter](IRepository& repository) {
          return repository.ListResources(filter);
        },
        [self = shared_from_this(),
         request](Result<std::vector<Resource>> result) {
          self->Finish<std::vector<Resource>>(request, std::move(result));
        });
  }

  void SyncCatalog(const PlatformId& id, const RequestPtr<Operation>& request) {
    if (!Online(id)) {
      Finish<Operation>(request, Failure(ErrorCode::kPlatformOffline));
      return;
    }
    for (const auto& query : queries_)
      if (query.second.platform_id == id && query.second.command == "Catalog") {
        Finish<Operation>(request, Failure(ErrorCode::kInvalidState));
        return;
      }
    Operation operation;
    operation.id = Id("catalog");
    operation.platform_id = id;
    operations_[operation.id] = operation;
    const uint32_t sn = NextSn();
    Query query;
    query.platform_id = id;
    query.target = platforms_.at(id).snapshot.config.gb_id;
    query.command = "Catalog";
    query.request_id = operation.id;
    query.deadline =
        Clock::now() + std::chrono::milliseconds(config_.query_timeout_ms);
    query.cancellation = request->options.cancellation;
    query.fail = [self = shared_from_this(), operation_id = operation.id,
                  trace = request->options.trace_id](Error error) {
      auto& operation = self->operations_.at(operation_id);
      operation.state = error.code == ErrorCode::kCancelled
                            ? OperationState::kCancelled
                            : OperationState::kFailed;
      error.trace_id = trace;
      operation.error = std::move(error);
      self->Publish({EventType::kOperationChanged, operation_id, trace,
                     operation.error, operation});
    };
    query.trace = request->options.trace_id;
    queries_[sn] = query;
    const auto sent =
        sip_->SendQuery(id, query.target, "Catalog", sn, operation.id);
    if (!sent) {
      FailQuery(sn, sent.error());
      Finish<Operation>(request, sent.error());
      return;
    }
    Publish({EventType::kOperationChanged,
             operation.id,
             request->options.trace_id,
             {ErrorCode::kOk, {}, {}},
             operation});
    Finish<Operation>(request, operation);
  }

  void GetOperation(const OperationId& id,
                    const RequestPtr<Operation>& request) {
    auto found = operations_.find(id);
    if (found == operations_.end())
      Finish<Operation>(request, Failure(ErrorCode::kNotFound));
    else
      Finish<Operation>(request, found->second);
  }

  template <typename T>
  void Refresh(const ResourceId& id,
               const std::string& command,
               const RequestPtr<T>& request) {
    Database<Resource>(
        [id](IRepository& repository) { return repository.FindResource(id); },
        [self = shared_from_this(), request, command](Result<Resource> result) {
          if (request->done)
            return;
          if (!result) {
            self->Finish<T>(request, result.error());
            return;
          }
          const auto resource = result.value();
          if (!self->Online(resource.platform_id)) {
            self->Finish<T>(request, Failure(ErrorCode::kPlatformOffline));
            return;
          }
          const uint32_t sn = self->NextSn();
          Query query;
          query.platform_id = resource.platform_id;
          query.target = resource.gb_id;
          query.command = command;
          query.request_id = request->id;
          query.trace = request->options.trace_id;
          query.deadline = Clock::now() + request->options.timeout;
          query.cancellation = request->options.cancellation;
          query.fail = [self, request](Error error) {
            self->Finish<T>(request, std::move(error));
          };
          query.deliver = [self, request, resource](SipEvent event) {
            if constexpr (std::is_same_v<T, DeviceInfo>) {
              auto value = event.info;
              value.resource_id = resource.id;
              value.queried_ms = WallTime();
              self->Database<void>(
                  [value](IRepository& repository) {
                    return repository.SaveDeviceInfo(value);
                  },
                  [self, request, value](Result<void> saved) {
                    if (!saved)
                      self->Finish<T>(request, saved.error());
                    else
                      self->Finish<T>(request, value);
                  });
            } else {
              auto value = event.status;
              value.resource_id = resource.id;
              value.queried_ms = WallTime();
              self->Database<void>(
                  [value](IRepository& repository) {
                    return repository.SaveDeviceStatus(value);
                  },
                  [self, request, value](Result<void> saved) {
                    if (!saved)
                      self->Finish<T>(request, saved.error());
                    else
                      self->Finish<T>(request, value);
                  });
            }
          };
          self->queries_[sn] = query;
          const auto sent = self->sip_->SendQuery(
              resource.platform_id, resource.gb_id, command, sn, request->id);
          if (!sent)
            self->FailQuery(sn, sent.error());
        });
  }

  void StartLive(StartLiveRequest live,
                 const RequestPtr<PlaybackLease>& request) {
    const bool by_channel = live.resource_id.empty();
    if (by_channel ? !Identifier(live.platform_id) || live.channel_gb_id.empty()
                   : !live.platform_id.empty() || !live.channel_gb_id.empty()) {
      Finish<PlaybackLease>(request, Failure(ErrorCode::kInvalidArgument));
      return;
    }
    if (live.mode && live.mode != RtpReceiveMode::kUdp &&
        live.mode != RtpReceiveMode::kTcpPassive &&
        live.mode != RtpReceiveMode::kTcpActive) {
      Finish<PlaybackLease>(request, Failure(ErrorCode::kInvalidArgument));
      return;
    }
    Database<Resource>(
        [live, by_channel](IRepository& repository) {
          if (by_channel)
            return repository.FindResourceByChannel(live.platform_id,
                                                    live.channel_gb_id);
          return repository.FindResource(live.resource_id);
        },
        [self = shared_from_this(), requested_mode = live.mode,
         request](Result<Resource> result) {
          if (request->done)
            return;
          if (!result) {
            self->Finish<PlaybackLease>(request, result.error());
            return;
          }
          const auto resource = result.value();
          if (!self->Online(resource.platform_id)) {
            self->Finish<PlaybackLease>(request,
                                        Failure(ErrorCode::kPlatformOffline));
            return;
          }
          if (resource.parental || resource.state == OnlineState::kOffline) {
            self->Finish<PlaybackLease>(
                request,
                Failure(resource.parental ? ErrorCode::kInvalidArgument
                                          : ErrorCode::kResourceOffline));
            return;
          }
          const auto mode = requested_mode.value_or(
              self->platforms_.at(resource.platform_id)
                  .snapshot.config.default_receive_mode);
          for (auto& item : self->playbacks_) {
            if (Active(item.second.snapshot.state) &&
                item.second.snapshot.resource_id == resource.id &&
                item.second.snapshot.mode == mode) {
              self->Finish<PlaybackLease>(request, self->NewLease(item.second));
              return;
            }
          }
          if (self->media_->ReceiverCount() >= self->config_.max_playbacks) {
            self->Finish<PlaybackLease>(request,
                                        Failure(ErrorCode::kResourceExhausted));
            return;
          }
          PlaybackEntry entry;
          entry.snapshot.id = self->Id("play");
          entry.snapshot.created_ms = WallTime();
          entry.snapshot.resource_id = resource.id;
          entry.snapshot.mode = mode;
          entry.snapshot.ssrc =
              1 + static_cast<uint32_t>(self->sequence_ % 999999999);
          entry.platform_id = resource.platform_id;
          entry.trace = request->options.trace_id;
          entry.deadline = Clock::now() + std::chrono::milliseconds(
                                              self->config_.media_timeout_ms);
          auto receiver = self->media_->CreateReceiver(
              {entry.snapshot.id, mode, 0, entry.snapshot.ssrc});
          if (!receiver) {
            self->Finish<PlaybackLease>(request, receiver.error());
            return;
          }
          const auto sent = self->sip_->InviteLive(
              {entry.snapshot.id, resource.platform_id, resource.gb_id,
               self->config_.advertised_address, receiver.value().port,
               entry.snapshot.ssrc, mode});
          if (!sent) {
            self->media_->CloseReceiver(entry.snapshot.id);
            self->Finish<PlaybackLease>(request, sent.error());
            return;
          }
          const auto id = entry.snapshot.id;
          self->playbacks_[id] = std::move(entry);
          self->Finish<PlaybackLease>(request,
                                      self->NewLease(self->playbacks_.at(id)));
          self->PublishPlayback(self->playbacks_.at(id));
        });
  }

  void GetPlayback(const PlaybackId& id, const RequestPtr<Playback>& request) {
    auto found = playbacks_.find(id);
    if (found == playbacks_.end())
      Finish<Playback>(request, Failure(ErrorCode::kNotFound));
    else
      Finish<Playback>(request, found->second.snapshot);
  }

  void GetPlaybackAddress(const PlaybackId& id,
                          uint32_t idle_timeout_seconds,
                          const RequestPtr<Playback>& request) {
    if (!idle_timeout_seconds || idle_timeout_seconds > 86400) {
      Finish<Playback>(request, Failure(ErrorCode::kInvalidArgument));
      return;
    }
    auto found = playbacks_.find(id);
    if (found == playbacks_.end()) {
      Finish<Playback>(request, Failure(ErrorCode::kNotFound));
      return;
    }
    auto& entry = found->second;
    if (!Active(entry.snapshot.state)) {
      Finish<Playback>(request, entry.snapshot.state == PlaybackState::kFailed
                                    ? entry.snapshot.error
                                    : Failure(ErrorCode::kInvalidState));
      return;
    }
    entry.snapshot.idle_timeout_seconds = idle_timeout_seconds;
    Finish<Playback>(request, entry.snapshot);
  }

  void WaitPlayback(const PlaybackId& id, const RequestPtr<Playback>& request) {
    auto found = playbacks_.find(id);
    if (found == playbacks_.end()) {
      Finish<Playback>(request, Failure(ErrorCode::kNotFound));
      return;
    }
    auto& entry = found->second;
    if (entry.snapshot.state == PlaybackState::kPlaying)
      Finish<Playback>(request, entry.snapshot);
    else if (!Active(entry.snapshot.state))
      Finish<Playback>(request, entry.snapshot.state == PlaybackState::kFailed
                                    ? entry.snapshot.error
                                    : Failure(ErrorCode::kCancelled));
    else
      entry.waiters.push_back(request);
  }

  void Renew(const PlaybackLeaseId& id,
             const RequestPtr<PlaybackLease>& request) {
    auto found = leases_.find(id);
    if (found == leases_.end() || found->second.deadline <= Clock::now()) {
      Finish<PlaybackLease>(request, Failure(ErrorCode::kNotFound));
      return;
    }
    found->second.deadline =
        Clock::now() + std::chrono::seconds(config_.lease_seconds);
    found->second.snapshot.expires_ms =
        WallTime() + static_cast<int64_t>(config_.lease_seconds) * 1000;
    Finish<PlaybackLease>(request, found->second.snapshot);
  }

  void Release(const PlaybackLeaseId& id, const RequestPtr<void>& request) {
    ReleaseLease(id);
    Finish<void>(request, {});
  }

  void ForceStop(const PlaybackId& id, const RequestPtr<void>& request) {
    auto found = playbacks_.find(id);
    if (found == playbacks_.end()) {
      Finish<void>(request, Failure(ErrorCode::kNotFound));
      return;
    }
    EndPlayback(found->second, {});
    Finish<void>(request, {});
  }

  void StatisticsFor(const RequestPtr<Statistics>& request) {
    Statistics result;
    result.active_queries = static_cast<uint32_t>(queries_.size());
    result.active_receivers = media_->ReceiverCount();
    result.playback_leases = static_cast<uint32_t>(leases_.size());
    const auto sip_statistics = sip_->GetStatistics();
    result.sip_sessions = sip_statistics.sessions;
    result.sip_requests = sip_statistics.requests;
    for (const auto& item : platforms_)
      if (item.second.snapshot.state == OnlineState::kOnline)
        ++result.online_platforms;
    for (const auto& item : playbacks_)
      if (Active(item.second.snapshot.state))
        ++result.active_playbacks;
    Finish<Statistics>(request, result);
  }

  Result<uint64_t> Subscribe(EventHandler handler) {
    if (!handler)
      return Failure(ErrorCode::kInvalidArgument);
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    if (!accepting_.load())
      return Failure(ErrorCode::kInvalidState);
    auto subscription = std::make_shared<Subscription>();
    subscription->handler = std::move(handler);
    const auto id = ++subscription_sequence_;
    subscriptions_[id] = std::move(subscription);
    return id;
  }

  void Unsubscribe(uint64_t id) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    auto found = subscriptions_.find(id);
    if (found == subscriptions_.end())
      return;
    found->second->active.store(false);
    subscriptions_.erase(found);
  }

 private:
  struct Pending {
    Clock::time_point deadline;
    Cancellation cancellation;
    std::function<void(Error)> fail;
  };

  struct LivePlatform {
    Platform snapshot;
    bool registered = false;
    Clock::time_point registration_deadline;
    Clock::time_point heartbeat_deadline;
  };

  struct Query {
    PlatformId platform_id;
    std::string target;
    std::string command;
    std::string request_id;
    std::string trace;
    Clock::time_point deadline;
    Cancellation cancellation;
    CatalogAccumulator catalog;
    std::function<void(Error)> fail;
    std::function<void(SipEvent)> deliver;
  };

  struct PlaybackEntry {
    Playback snapshot;
    PlatformId platform_id;
    std::string trace;
    Clock::time_point deadline;
    bool answer_received = false;
    bool media_ready = false;
    std::optional<Clock::time_point> idle_since;
    std::set<PlaybackLeaseId> leases;
    std::vector<RequestPtr<Playback>> waiters;
  };

  struct Lease {
    PlaybackLease snapshot;
    Clock::time_point deadline;
  };

  struct Subscription {
    std::atomic<bool> active{true};
    EventHandler handler;
  };

  bool Post(std::function<void()> work) {
    if (!accepting_.load())
      return false;
    return actor_.Post([self = shared_from_this(), work = std::move(work)] {
      if (self->accepting_.load())
        work();
    });
  }

  std::string Id(const char* prefix) {
    return std::string(prefix) + "_" + run_id_ + "_" +
           std::to_string(++sequence_);
  }

  uint32_t NextSn() {
    do {
      ++sn_;
    } while (!sn_ || queries_.count(sn_));
    return sn_;
  }

  bool Online(const PlatformId& id) const {
    auto found = platforms_.find(id);
    return found != platforms_.end() && found->second.snapshot.config.enabled &&
           found->second.snapshot.state == OnlineState::kOnline;
  }

  void PushPlatforms() {
    std::vector<PlatformConfig> configs;
    for (const auto& item : platforms_)
      configs.push_back(item.second.snapshot.config);
    sip_->ReplacePlatforms(std::move(configs));
  }

  void Publish(Event event) {
    std::vector<std::shared_ptr<Subscription>> subscribers;
    {
      std::lock_guard<std::mutex> lock(subscriptions_mutex_);
      for (const auto& item : subscriptions_)
        subscribers.push_back(item.second);
    }
    for (const auto& subscriber : subscribers)
      callbacks_.Post([subscriber, event] {
        if (subscriber->active.load())
          subscriber->handler(event);
      });
  }

  void PublishPlayback(const PlaybackEntry& entry) {
    Publish({EventType::kPlaybackChanged, entry.snapshot.id, entry.trace,
             entry.snapshot.error});
  }

  void FailQuery(uint32_t sn, Error error) {
    auto found = queries_.find(sn);
    if (found == queries_.end())
      return;
    auto fail = std::move(found->second.fail);
    queries_.erase(found);
    fail(std::move(error));
  }

  void LosePlatform(const PlatformId& id, Error error) {
    auto found = platforms_.find(id);
    if (found != platforms_.end())
      found->second.snapshot.state = OnlineState::kOffline;
    std::vector<uint32_t> queries;
    for (const auto& item : queries_)
      if (item.second.platform_id == id)
        queries.push_back(item.first);
    for (auto sn : queries)
      FailQuery(sn, error);
    for (auto& item : playbacks_)
      if (item.second.platform_id == id)
        EndPlayback(item.second, error);
  }

  PlaybackLease NewLease(PlaybackEntry& entry) {
    Lease lease;
    lease.snapshot.id = Id("lease");
    lease.snapshot.playback_id = entry.snapshot.id;
    lease.snapshot.expires_ms =
        WallTime() + static_cast<int64_t>(config_.lease_seconds) * 1000;
    lease.deadline = Clock::now() + std::chrono::seconds(config_.lease_seconds);
    entry.leases.insert(lease.snapshot.id);
    entry.snapshot.consumers = static_cast<uint32_t>(entry.leases.size());
    leases_[lease.snapshot.id] = lease;
    return lease.snapshot;
  }

  void ReleaseLease(const PlaybackLeaseId& id) {
    auto found = leases_.find(id);
    if (found == leases_.end())
      return;
    auto playback = playbacks_.find(found->second.snapshot.playback_id);
    leases_.erase(found);
    if (playback == playbacks_.end())
      return;
    auto& entry = playback->second;
    entry.leases.erase(id);
    entry.snapshot.consumers = static_cast<uint32_t>(entry.leases.size());
    if (entry.leases.empty() && !entry.snapshot.idle_timeout_seconds)
      EndPlayback(entry, {});
    else
      PublishPlayback(entry);
  }

  void EndPlayback(PlaybackEntry& entry, std::optional<Error> error) {
    if (!Active(entry.snapshot.state))
      return;
    entry.snapshot.state = PlaybackState::kStopping;
    sip_->TerminateSession(entry.snapshot.id);
    media_->CloseReceiver(entry.snapshot.id);
    for (const auto& id : entry.leases)
      leases_.erase(id);
    entry.leases.clear();
    entry.snapshot.consumers = 0;
    entry.snapshot.endpoints.clear();
    entry.snapshot.state =
        error ? PlaybackState::kFailed : PlaybackState::kEnded;
    entry.snapshot.error = error.value_or(Error{ErrorCode::kOk, {}, {}});
    entry.snapshot.error.trace_id = entry.trace;
    for (const auto& waiter : entry.waiters)
      Finish<Playback>(waiter, error.value_or(Failure(ErrorCode::kCancelled)));
    entry.waiters.clear();
    PublishPlayback(entry);
  }

  void Ready(PlaybackEntry& entry) {
    if (!entry.answer_received || !entry.media_ready ||
        !Active(entry.snapshot.state))
      return;
    auto receiver = media_->GetReceiver(entry.snapshot.id);
    if (!receiver || !receiver.value().ready)
      return;
    entry.snapshot.state = PlaybackState::kPlaying;
    if (!entry.snapshot.media_ready_ms)
      entry.snapshot.media_ready_ms = WallTime();
    if (!entry.snapshot.ready_ms)
      entry.snapshot.ready_ms = WallTime();
    entry.snapshot.endpoints = receiver.value().endpoints;
    for (const auto& waiter : entry.waiters)
      Finish<Playback>(waiter, entry.snapshot);
    entry.waiters.clear();
    PublishPlayback(entry);
  }

  void OnMedia(const MediaEvent& event) {
    auto found = playbacks_.find(event.id);
    if (found == playbacks_.end() || !Active(found->second.snapshot.state))
      return;
    auto& entry = found->second;
    if (event.type == MediaEventType::kReady) {
      entry.media_ready = true;
      if (!entry.snapshot.media_ready_ms)
        entry.snapshot.media_ready_ms = WallTime();
      Ready(entry);
    } else if (event.type == MediaEventType::kClosed ||
               event.type == MediaEventType::kTimeout)
      EndPlayback(entry, Failure(ErrorCode::kMediaReceiveTimeout));
    else if (event.type == MediaEventType::kConnectionFailed)
      EndPlayback(entry, event.error);
  }

  void OnSip(SipEvent event) {
    auto platform = platforms_.find(event.platform_id);
    if (platform == platforms_.end())
      return;
    auto& live = platform->second;
    if (event.type == SipEventType::kRegistered) {
      live.registered = event.expires_seconds != 0;
      live.registration_deadline =
          Clock::now() + std::chrono::seconds(event.expires_seconds);
      live.heartbeat_deadline =
          Clock::now() +
          std::chrono::seconds(live.snapshot.config.heartbeat_timeout_seconds);
      live.snapshot.state =
          live.registered ? OnlineState::kOnline : OnlineState::kOffline;
      live.snapshot.last_seen_ms = WallTime();
      live.snapshot.remote_address = event.remote_address;
      live.snapshot.remote_port = event.remote_port;
      live.snapshot.sip_transport = event.transport;
      Publish({EventType::kPlatformChanged,
               event.platform_id,
               {},
               {ErrorCode::kOk, {}, {}}});
      return;
    }
    if (event.type == SipEventType::kUnregistered) {
      live.registered = false;
      LosePlatform(event.platform_id, Failure(ErrorCode::kPlatformOffline));
      Publish({EventType::kPlatformChanged,
               event.platform_id,
               {},
               Failure(ErrorCode::kPlatformOffline)});
      return;
    }
    if (event.type == SipEventType::kHeartbeat) {
      if (!live.registered || Clock::now() >= live.registration_deadline)
        return;
      const bool changed = live.snapshot.state != OnlineState::kOnline;
      live.heartbeat_deadline =
          Clock::now() +
          std::chrono::seconds(live.snapshot.config.heartbeat_timeout_seconds);
      live.snapshot.last_seen_ms = WallTime();
      live.snapshot.state = OnlineState::kOnline;
      if (changed)
        Publish({EventType::kPlatformChanged,
                 event.platform_id,
                 {},
                 {ErrorCode::kOk, {}, {}}});
      return;
    }
    if (event.type == SipEventType::kRequestFailed) {
      std::vector<uint32_t> failed;
      for (const auto& query : queries_)
        if (query.second.request_id == event.request_id &&
            query.second.platform_id == event.platform_id)
          failed.push_back(query.first);
      for (auto sn : failed)
        FailQuery(sn, event.error);
      auto playback = playbacks_.find(event.request_id);
      if (playback != playbacks_.end())
        EndPlayback(playback->second, event.error);
      return;
    }
    if (event.type == SipEventType::kInviteAnswer) {
      auto found = playbacks_.find(event.request_id);
      if (found == playbacks_.end() || !Active(found->second.snapshot.state))
        return;
      auto& entry = found->second;
      if (event.ssrc != entry.snapshot.ssrc) {
        EndPlayback(entry, Failure(ErrorCode::kSdpNegotiationFailed));
        return;
      }
      entry.answer_received = true;
      entry.snapshot.negotiated_ms = WallTime();
      entry.snapshot.state = PlaybackState::kWaitingMedia;
      if (entry.snapshot.mode == RtpReceiveMode::kTcpActive) {
        auto connected = media_->ConnectReceiver(
            entry.snapshot.id, event.remote_address, event.remote_port);
        if (!connected) {
          EndPlayback(entry, connected.error());
          return;
        }
      }
      auto receiver = media_->GetReceiver(entry.snapshot.id);
      if (receiver && receiver.value().ready)
        entry.media_ready = true;
      Ready(entry);
      PublishPlayback(entry);
      return;
    }
    if (event.type == SipEventType::kSessionEnded) {
      auto found = playbacks_.find(event.request_id);
      if (found != playbacks_.end() && Active(found->second.snapshot.state))
        EndPlayback(found->second, Failure(ErrorCode::kSipTransportFailed));
      return;
    }
    if (event.type != SipEventType::kQueryResponse)
      return;
    auto found = queries_.find(event.sn);
    if (found == queries_.end() ||
        found->second.platform_id != event.platform_id ||
        found->second.command != event.command ||
        found->second.target != event.device_gb_id)
      return;
    auto& query = found->second;
    if (query.command != "Catalog") {
      auto deliver = std::move(query.deliver);
      queries_.erase(found);
      deliver(std::move(event));
      return;
    }
    auto complete = query.catalog.Add(event.catalog_total, event.resources);
    if (!complete) {
      FailQuery(event.sn, complete.error());
      return;
    }
    auto& operation = operations_.at(query.request_id);
    operation.received = query.catalog.received();
    operation.expected = query.catalog.expected();
    Publish({EventType::kOperationChanged, operation.id, query.trace,
             operation.error, operation,
             std::make_shared<const std::vector<Resource>>(
                 std::move(event.resources))});
    if (!complete.value())
      return;
    auto resources = query.catalog.Snapshot();
    for (auto& resource : resources) {
      resource.platform_id = event.platform_id;
      resource.id = event.platform_id + ":" + resource.gb_id;
      resource.updated_ms = WallTime();
    }
    const auto id = query.request_id;
    const auto trace = query.trace;
    const auto platform_id = event.platform_id;
    queries_.erase(found);
    Database<void>(
        [platform_id, resources](IRepository& repository) {
          return repository.ReplaceCatalog(platform_id, resources);
        },
        [self = shared_from_this(), id, trace,
         platform_id](Result<void> result) {
          auto found = self->operations_.find(id);
          if (found == self->operations_.end())
            return;
          auto& operation = found->second;
          operation.state =
              result ? OperationState::kSucceeded : OperationState::kFailed;
          if (!result)
            operation.error = result.error();
          operation.error.trace_id = trace;
          self->Publish({EventType::kOperationChanged, id, trace,
                         operation.error, operation});
          if (result)
            self->Publish({EventType::kCatalogChanged,
                           platform_id,
                           trace,
                           {ErrorCode::kOk, {}, {}}});
        });
  }

  void Tick() {
    if (!accepting_.load())
      return;
    const auto now = Clock::now();
    const bool check_readers = now >= next_reader_check_;
    if (check_readers)
      next_reader_check_ = now + std::chrono::milliseconds(250);
    std::vector<uint32_t> expired_queries;
    for (const auto& query : queries_)
      if (query.second.deadline <= now || query.second.cancellation.cancelled())
        expired_queries.push_back(query.first);
    for (auto sn : expired_queries) {
      auto found = queries_.find(sn);
      if (found != queries_.end())
        FailQuery(sn, Failure(found->second.cancellation.cancelled()
                                  ? ErrorCode::kCancelled
                                  : ErrorCode::kTimeout));
    }
    std::vector<std::function<void()>> expired_requests;
    for (const auto& request : pending_)
      if (request.second.deadline <= now ||
          request.second.cancellation.cancelled()) {
        const auto code = request.second.cancellation.cancelled()
                              ? ErrorCode::kCancelled
                              : ErrorCode::kTimeout;
        expired_requests.push_back(
            [fail = request.second.fail, code] { fail(Failure(code)); });
      }
    for (auto& fail : expired_requests)
      fail();
    for (auto& item : platforms_) {
      auto& live = item.second;
      if (live.snapshot.state == OnlineState::kOnline &&
          (live.registration_deadline <= now ||
           live.heartbeat_deadline <= now)) {
        if (live.registration_deadline <= now)
          live.registered = false;
        LosePlatform(item.first, Failure(ErrorCode::kPlatformOffline));
        Publish({EventType::kPlatformChanged,
                 item.first,
                 {},
                 Failure(ErrorCode::kPlatformOffline)});
      }
    }
    std::vector<std::string> expired_leases;
    for (const auto& lease : leases_)
      if (lease.second.deadline <= now)
        expired_leases.push_back(lease.first);
    for (const auto& id : expired_leases)
      ReleaseLease(id);
    for (auto& item : playbacks_) {
      auto& entry = item.second;
      entry.waiters.erase(
          std::remove_if(entry.waiters.begin(), entry.waiters.end(),
                         [](const auto& request) { return request->done; }),
          entry.waiters.end());
      if (Active(entry.snapshot.state) &&
          entry.snapshot.state != PlaybackState::kPlaying &&
          entry.deadline <= now)
        EndPlayback(entry, Failure(ErrorCode::kMediaReceiveTimeout));
      if (check_readers && entry.snapshot.state == PlaybackState::kPlaying &&
          entry.snapshot.idle_timeout_seconds) {
        const auto readers = media_->GetReaderCount(entry.snapshot.id);
        if ((readers && readers.value() > 0) ||
            (!readers && readers.error().code != ErrorCode::kNotFound)) {
          entry.idle_since.reset();
        } else {
          if (!entry.idle_since)
            entry.idle_since = now;
          if (now - *entry.idle_since >=
              std::chrono::seconds(entry.snapshot.idle_timeout_seconds)) {
            EndPlayback(entry, {});
          }
        }
      }
    }
    for (auto it = operations_.begin();
         operations_.size() > 256 && it != operations_.end();) {
      if (it->second.state != OperationState::kRunning)
        it = operations_.erase(it);
      else
        ++it;
    }
    for (auto it = playbacks_.begin();
         playbacks_.size() > 256 && it != playbacks_.end();) {
      if (!Active(it->second.snapshot.state))
        it = playbacks_.erase(it);
      else
        ++it;
    }
    std::weak_ptr<State> weak = shared_from_this();
    actor_.PostAfter(std::chrono::milliseconds(25), [weak] {
      if (auto self = weak.lock())
        self->Tick();
    });
  }

  Config config_;
  std::shared_ptr<IRepository> repository_;
  std::shared_ptr<IGb28181Service> sip_;
  std::shared_ptr<IMediaService> media_;
  TaskRunner actor_;
  TaskRunner storage_;
  TaskRunner callbacks_;
  std::atomic<bool> accepting_{false};
  std::string run_id_;
  uint64_t sequence_ = 0;
  uint32_t sn_ = 0;
  std::map<std::string, Pending> pending_;
  std::map<PlatformId, LivePlatform> platforms_;
  std::map<uint32_t, Query> queries_;
  std::map<OperationId, Operation> operations_;
  std::map<PlaybackId, PlaybackEntry> playbacks_;
  std::map<PlaybackLeaseId, Lease> leases_;
  Clock::time_point next_reader_check_{};
  std::mutex subscriptions_mutex_;
  uint64_t subscription_sequence_ = 0;
  std::map<uint64_t, std::shared_ptr<Subscription>> subscriptions_;
};

class Application final : public Client {
 public:
  Application(std::unique_ptr<IRepository> repository,
              std::unique_ptr<IGb28181Service> sip,
              std::unique_ptr<IMediaService> media)
      : repository_(std::move(repository)),
        sip_(std::move(sip)),
        media_(std::move(media)) {
  }

  ~Application() override {
    Stop();
  }

  Result<void> Start(const Config& config) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (transitioning_ || state_)
        return Failure(ErrorCode::kInvalidState);
      transitioning_ = true;
    }
    auto state = std::make_shared<State>(repository_, sip_, media_);
    Result<void> result = Failure(ErrorCode::kInternalError);
    try {
      result = state->Initialize(config);
    } catch (...) {
    }
    if (!result)
      state->Shutdown();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (result)
        state_ = std::move(state);
      transitioning_ = false;
    }
    return result;
  }

  Result<void> Stop() override {
    std::shared_ptr<State> state;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (transitioning_)
        return Failure(ErrorCode::kInvalidState);
      if (!state_)
        return {};
      if (state_->InCallback())
        return Failure(ErrorCode::kInvalidState);
      transitioning_ = true;
      state = std::move(state_);
    }
    auto result = state->Shutdown();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      transitioning_ = false;
    }
    return result;
  }

  template <typename T, typename Work>
  Result<void> Submit(Completion<T> completion,
                      RequestOptions options,
                      Work work) {
    std::shared_ptr<State> state;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state = state_;
    }
    if (!state)
      return Failure(ErrorCode::kInvalidState);
    return state->Submit<T>(std::move(completion), std::move(options),
                            std::move(work));
  }

  Result<void> AddPlatform(PlatformConfig value,
                           Completion<Platform> done,
                           RequestOptions options) override {
    return Submit<Platform>(std::move(done), std::move(options),
                            [value](State& state, auto request) {
                              state.SavePlatform(value, true, request);
                            });
  }

  Result<void> UpdatePlatform(PlatformConfig value,
                              Completion<Platform> done,
                              RequestOptions options) override {
    return Submit<Platform>(std::move(done), std::move(options),
                            [value](State& state, auto request) {
                              state.SavePlatform(value, false, request);
                            });
  }

  Result<void> RemovePlatform(PlatformId id,
                              Completion<void> done,
                              RequestOptions options) override {
    return Submit<void>(std::move(done), std::move(options),
                        [id](State& state, auto request) {
                          state.RemovePlatform(id, request);
                        });
  }

  Result<void> GetPlatform(PlatformId id,
                           Completion<Platform> done,
                           RequestOptions options) override {
    return Submit<Platform>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.GetPlatform(id, request); });
  }

  Result<void> ListPlatforms(Completion<std::vector<Platform>> done,
                             RequestOptions options) override {
    return Submit<std::vector<Platform>>(
        std::move(done), std::move(options),
        [](State& state, auto request) { state.ListPlatforms(request); });
  }

  Result<void> GetResource(ResourceId id,
                           Completion<Resource> done,
                           RequestOptions options) override {
    return Submit<Resource>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.GetResource(id, request); });
  }

  Result<void> ListResources(ResourceFilter filter,
                             Completion<std::vector<Resource>> done,
                             RequestOptions options) override {
    return Submit<std::vector<Resource>>(std::move(done), std::move(options),
                                         [filter](State& state, auto request) {
                                           state.ListResources(filter, request);
                                         });
  }

  Result<void> SyncCatalog(PlatformId id,
                           Completion<Operation> done,
                           RequestOptions options) override {
    return Submit<Operation>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.SyncCatalog(id, request); });
  }

  Result<void> GetOperation(OperationId id,
                            Completion<Operation> done,
                            RequestOptions options) override {
    return Submit<Operation>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.GetOperation(id, request); });
  }

  Result<void> GetDeviceInfo(ResourceId id,
                             Completion<DeviceInfo> done,
                             RequestOptions options) override {
    return Submit<DeviceInfo>(
        std::move(done), std::move(options), [id](State& state, auto request) {
          state.Database<DeviceInfo>(
              [id](IRepository& repository) {
                return repository.FindDeviceInfo(id);
              },
              [self = state.shared_from_this(),
               request](Result<DeviceInfo> result) {
                self->Finish<DeviceInfo>(request, std::move(result));
              });
        });
  }

  Result<void> GetDeviceStatus(ResourceId id,
                               Completion<DeviceStatus> done,
                               RequestOptions options) override {
    return Submit<DeviceStatus>(
        std::move(done), std::move(options), [id](State& state, auto request) {
          state.Database<DeviceStatus>(
              [id](IRepository& repository) {
                return repository.FindDeviceStatus(id);
              },
              [self = state.shared_from_this(),
               request](Result<DeviceStatus> result) {
                self->Finish<DeviceStatus>(request, std::move(result));
              });
        });
  }

  Result<void> RefreshDeviceInfo(ResourceId id,
                                 Completion<DeviceInfo> done,
                                 RequestOptions options) override {
    return Submit<DeviceInfo>(std::move(done), std::move(options),
                              [id](State& state, auto request) {
                                state.Refresh(id, "DeviceInfo", request);
                              });
  }

  Result<void> RefreshDeviceStatus(ResourceId id,
                                   Completion<DeviceStatus> done,
                                   RequestOptions options) override {
    return Submit<DeviceStatus>(std::move(done), std::move(options),
                                [id](State& state, auto request) {
                                  state.Refresh(id, "DeviceStatus", request);
                                });
  }

  Result<void> StartLive(StartLiveRequest value,
                         Completion<PlaybackLease> done,
                         RequestOptions options) override {
    return Submit<PlaybackLease>(std::move(done), std::move(options),
                                 [value](State& state, auto request) {
                                   state.StartLive(value, request);
                                 });
  }

  Result<void> GetPlayback(PlaybackId id,
                           Completion<Playback> done,
                           RequestOptions options) override {
    return Submit<Playback>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.GetPlayback(id, request); });
  }

  Result<void> WaitPlaybackReady(PlaybackId id,
                                 Completion<Playback> done,
                                 RequestOptions options) override {
    return Submit<Playback>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.WaitPlayback(id, request); });
  }

  Result<void> GetPlaybackAddress(PlaybackId id,
                                  std::optional<uint32_t> idle_timeout_seconds,
                                  Completion<Playback> done,
                                  RequestOptions options) override {
    return Submit<Playback>(std::move(done), std::move(options),
                            [id, timeout = idle_timeout_seconds.value_or(60)](
                                State& state, auto request) {
                              state.GetPlaybackAddress(id, timeout, request);
                            });
  }

  Result<void> RenewPlayback(PlaybackLeaseId id,
                             Completion<PlaybackLease> done,
                             RequestOptions options) override {
    return Submit<PlaybackLease>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.Renew(id, request); });
  }

  Result<void> ReleasePlayback(PlaybackLeaseId id,
                               Completion<void> done,
                               RequestOptions options) override {
    return Submit<void>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.Release(id, request); });
  }

  Result<void> ForceStopPlayback(PlaybackId id,
                                 Completion<void> done,
                                 RequestOptions options) override {
    return Submit<void>(
        std::move(done), std::move(options),
        [id](State& state, auto request) { state.ForceStop(id, request); });
  }

  Result<void> GetStatistics(Completion<Statistics> done,
                             RequestOptions options) override {
    return Submit<Statistics>(
        std::move(done), std::move(options),
        [](State& state, auto request) { state.StatisticsFor(request); });
  }

  Result<uint64_t> SubscribeEvents(EventHandler handler) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_)
      return Failure(ErrorCode::kInvalidState);
    return state_->Subscribe(std::move(handler));
  }

  void UnsubscribeEvents(uint64_t id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_)
      state_->Unsubscribe(id);
  }

 private:
  std::mutex mutex_;
  bool transitioning_ = false;
  std::shared_ptr<State> state_;
  std::shared_ptr<IRepository> repository_;
  std::shared_ptr<IGb28181Service> sip_;
  std::shared_ptr<IMediaService> media_;
};
}  // namespace

std::unique_ptr<Client> CreateApplication(
    std::unique_ptr<IRepository> repository,
    std::unique_ptr<IGb28181Service> sip,
    std::unique_ptr<IMediaService> media) {
  return std::make_unique<Application>(std::move(repository), std::move(sip),
                                       std::move(media));
}
}  // namespace gb28181
