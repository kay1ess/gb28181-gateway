#include "src/adapters/sip/resip_service.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <resip/dum/AppDialogSet.hxx>
#include <resip/dum/ClientInviteSession.hxx>
#include <resip/dum/ClientOutOfDialogReq.hxx>
#include <resip/dum/ClientPagerMessage.hxx>
#include <resip/dum/DialogUsageManager.hxx>
#include <resip/dum/InMemoryRegistrationDatabase.hxx>
#include <resip/dum/InviteSessionHandler.hxx>
#include <resip/dum/MasterProfile.hxx>
#include <resip/dum/OutOfDialogHandler.hxx>
#include <resip/dum/PagerMessageHandler.hxx>
#include <resip/dum/RegistrationHandler.hxx>
#include <resip/dum/ServerAuthManager.hxx>
#include <resip/dum/ServerInviteSession.hxx>
#include <resip/dum/ServerOutOfDialogReq.hxx>
#include <resip/dum/ServerPagerMessage.hxx>
#include <resip/dum/ServerRegistration.hxx>
#include <resip/dum/ServerSubscription.hxx>
#include <resip/dum/UserAuthInfo.hxx>
#include <resip/stack/PlainContents.hxx>
#include <resip/stack/SipStack.hxx>
#include <rutil/DigestStream.hxx>
#include <rutil/Log.hxx>

#include "src/adapters/sip/sdp_codec.h"
#include "src/adapters/sip/xml_codec.h"

namespace gb28181::sip {
namespace {
std::string String(const resip::Data& data) {
  return std::string(data.data(), data.size());
}

std::string Body(const resip::SipMessage& message) {
  const auto& raw = message.getRawBody();
  return raw.getBuffer() ? std::string(raw.getBuffer(), raw.getLength())
                         : std::string();
}

Error Failed(ErrorCode code) {
  return {code, ErrorMessage(code), {}};
}

// Opt-in transport diagnostics exclude authentication headers and raw bodies.
// This is useful for real devices which silently drop requests.
class SipTrace final : public resip::Transport::SipMessageLoggingHandler {
 public:
  void outboundMessage(const resip::Tuple& source,
                       const resip::Tuple& destination,
                       const resip::SipMessage& message) override {
    Write("send", source, destination, message);
  }

  void inboundMessage(const resip::Tuple& source,
                      const resip::Tuple& destination,
                      const resip::SipMessage& message) override {
    Write("receive", source, destination, message);
  }

 private:
  static void Write(const char* direction,
                    const resip::Tuple& source,
                    const resip::Tuple& destination,
                    const resip::SipMessage& message) {
    try {
      std::ostringstream text;
      text << "SIP trace " << direction << " " << source << " -> "
           << destination << " " << message.brief()
           << " from=" << message.header(resip::h_From).uri()
           << " to=" << message.header(resip::h_To).uri();
      if (!message.header(resip::h_Vias).empty())
        text << " via=" << message.header(resip::h_Vias).front();
      const auto body = Body(message);
      if (!body.empty() && message.method() == resip::MESSAGE) {
        const auto xml = ParseXml(body);
        if (xml)
          text << " xml=" << xml.value().root << "/" << xml.value().command
               << " sn=" << xml.value().sn
               << " device=" << xml.value().device_id;
        else
          text << " xml_error=" << xml.error().message;
      }
      std::clog << text.str() << '\n';
    } catch (...) {
    }
  }
};
}  // namespace

class ResipService::Impl : public resip::ServerRegistrationHandler,
                           public resip::OutOfDialogHandler,
                           public resip::ServerPagerMessageHandler,
                           public resip::ClientPagerMessageHandler,
                           public resip::InviteSessionHandler {
 public:
  Impl() : resip::InviteSessionHandler(true) {
  }

  struct Peer {
    resip::Tuple source;
    std::chrono::steady_clock::time_point deadline;
  };

  struct Session {
    std::string id;
    PlatformId platform_id;
    resip::AppDialogSet* dialog = nullptr;
    resip::InviteSessionHandle handle;
    RtpReceiveMode mode = RtpReceiveMode::kUdp;
  };

  class ClientDialog final : public resip::AppDialogSet {
   public:
    ClientDialog(Impl& owner, resip::DialogUsageManager& dum)
        : AppDialogSet(dum), owner_(owner) {
    }

    ~ClientDialog() override {
      owner_.ForgetDialog(this);
    }

   private:
    Impl& owner_;
  };

  // DUM owns AppDialogSet. Some failure paths destroy it without delivering
  // onTerminated; remove borrowed pointers before they can be used by Stop.
  void ForgetDialog(resip::AppDialogSet* dialog) {
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
      if (it->second.dialog != dialog)
        continue;
      std::clog << "SIP dialog destroyed before termination notification: "
                << it->second.id << '\n';
      SipEvent event;
      event.type = SipEventType::kSessionEnded;
      event.platform_id = it->second.platform_id;
      event.request_id = it->second.id;
      sessions_.erase(it);
      Emit(std::move(event));
      break;
    }
  }

  class Authentication final : public resip::ServerAuthManager {
   public:
    Authentication(Impl& owner, resip::DialogUsageManager& dum)
        : ServerAuthManager(dum,
                            dum.dumIncomingTarget(),
                            true,
                            resip::Data(owner.config_.realm)),
          owner_(owner) {
    }

   protected:
    bool proxyAuthenticationMode() const override {
      return false;
    }

    bool useAuthInt() const override {
      return false;
    }

    resip::AsyncBool requiresChallenge(
        const resip::SipMessage& message) override {
      return message.method() == resip::REGISTER ? resip::True : resip::False;
    }

    bool authorizedForThisIdentity(const resip::Data& user,
                                   const resip::Data& realm,
                                   resip::Uri& from) override {
      auto* platform = owner_.FindByGb(String(from.user()));
      return platform && platform->enabled &&
             realm == resip::Data(owner_.config_.realm) &&
             String(user) == (platform->username.empty() ? platform->gb_id
                                                         : platform->username);
    }

    void requestCredential(const resip::Data& user,
                           const resip::Data& realm,
                           const resip::SipMessage& message,
                           const resip::Auth&,
                           const resip::Data& transaction) override {
      auto* platform =
          owner_.FindByGb(String(message.header(resip::h_From).uri().user()));
      if (!platform || !platform->enabled ||
          String(user) != (platform->username.empty() ? platform->gb_id
                                                      : platform->username) ||
          String(realm) != owner_.config_.realm) {
        owner_.dum_->post(new resip::UserAuthInfo(
            user, realm, resip::UserAuthInfo::UserUnknown, transaction));
        return;
      }
      resip::DigestStream a1;
      a1 << user << ":" << realm << ":" << platform->password;
      a1.flush();
      owner_.dum_->post(
          new resip::UserAuthInfo(user, realm, a1.getHex(), transaction));
    }

   private:
    Impl& owner_;
  };

  Result<void> Start(const Config& config,
                     const std::vector<PlatformConfig>& platforms,
                     std::function<void(SipEvent)> handler) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (thread_.joinable())
      return Failed(ErrorCode::kInvalidState);
    try {
      config_ = config;
      handler_ = std::move(handler);
      platforms_.clear();
      peers_.clear();
      for (const auto& platform : platforms)
        platforms_[platform.id] = platform;
      // Upstream informational auth logs include HA1 material. Keep those
      // disabled by default; diagnostics must be explicitly redacted.
      resip::Log::setLevel(resip::Log::Warning);
      stack_ = std::make_unique<resip::SipStack>();
      const char* trace = std::getenv("GB_SIP_TRACE");
      if (trace && std::string(trace) == "1")
        stack_->setTransportSipMessageLoggingHandler(
            std::make_shared<SipTrace>());
      stack_->addTransport(resip::UDP, config.sip_port, resip::V4,
                           resip::StunDisabled,
                           resip::Data(config.bind_address));
      stack_->addTransport(resip::TCP, config.sip_port, resip::V4,
                           resip::StunDisabled,
                           resip::Data(config.bind_address));
      dum_ = std::make_unique<resip::DialogUsageManager>(*stack_);
      registration_database_ =
          std::make_unique<resip::InMemoryRegistrationDatabase>(true);
      dum_->setRegistrationPersistenceManager(registration_database_.get());
      auto profile = std::make_shared<resip::MasterProfile>();
      profile->addSupportedMethod(resip::REGISTER);
      profile->addSupportedMethod(resip::MESSAGE);
      profile->addSupportedMethod(resip::INVITE);
      profile->addSupportedMethod(resip::ACK);
      profile->addSupportedMethod(resip::BYE);
      profile->addSupportedMethod(resip::CANCEL);
      profile->addSupportedMethod(resip::OPTIONS);
      profile->addSupportedMimeType(resip::MESSAGE,
                                    resip::Mime("Application", "MANSCDP+xml"));
      profile->addSupportedMimeType(resip::INVITE,
                                    resip::Mime("application", "sdp"));
      profile->validateContentEnabled() = false;
      profile->validateAcceptEnabled() = false;
      profile->setDefaultFrom(resip::NameAddr(
          resip::Data("sip:" + config.server_gb_id + "@" + config.realm)));
      profile->setOverrideHostAndPort(
          resip::Uri(resip::Data("sip:" + config.advertised_address + ":" +
                                 std::to_string(config.sip_port))));
      profile->setRportEnabled(true);
      dum_->setMasterProfile(profile);
      dum_->setServerRegistrationHandler(this);
      dum_->addOutOfDialogHandler(resip::MESSAGE, this);
      dum_->setServerPagerMessageHandler(this);
      dum_->setClientPagerMessageHandler(this);
      dum_->addOutOfDialogHandler(resip::OPTIONS, this);
      dum_->setInviteSessionHandler(this);
      dum_->setServerAuthManager(
          std::make_shared<Authentication>(*this, *dum_));
      stop_requested_.store(false);
      accepting_ = true;
      thread_ = std::thread([this] { Run(); });
      return {};
    } catch (...) {
      dum_.reset();
      stack_.reset();
      registration_database_.reset();
      handler_ = {};
      return Failed(ErrorCode::kSipTransportFailed);
    }
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      accepting_ = false;
      stop_requested_.store(true);
    }
    if (thread_.joinable())
      thread_.join();
    dum_.reset();
    stack_.reset();
    registration_database_.reset();
    sessions_.clear();
    requests_.clear();
    session_count_.store(0);
    request_count_.store(0);
    peers_.clear();
    handler_ = {};
  }

  bool Post(std::function<void()> command) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!accepting_)
      return false;
    commands_.push_back(std::move(command));
    return true;
  }

  void Run() {
    while (!stop_requested_.load()) {
      std::vector<std::function<void()>> commands;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        commands.swap(commands_);
      }
      for (auto& command : commands) {
        try {
          command();
        } catch (...) {
        }
      }
      try {
        stack_->process(10);
        while (dum_->process()) {
        }
      } catch (...) {
      }
      session_count_.store(static_cast<uint32_t>(sessions_.size()));
      request_count_.store(static_cast<uint32_t>(requests_.size()));
    }
    // Drive the SIP loop briefly after ending dialogs so ACK/BYE/CANCEL can be
    // emitted. Application deadlines have already settled public operations.
    std::vector<resip::AppDialogSet*> dialogs;
    for (const auto& item : sessions_)
      if (item.second.dialog)
        dialogs.push_back(item.second.dialog);
    for (auto* dialog : dialogs) {
      try {
        dialog->end();
      } catch (...) {
      }
    }
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < until) {
      try {
        stack_->process(10);
        while (dum_->process()) {
        }
      } catch (...) {
      }
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    commands_.clear();
  }

  PlatformConfig* FindByGb(const std::string& id) {
    for (auto& platform : platforms_)
      if (platform.second.gb_id == id)
        return &platform.second;
    return nullptr;
  }

  void Emit(SipEvent event) {
    if (handler_) {
      try {
        handler_(std::move(event));
      } catch (...) {
      }
    }
  }

  void Register(resip::ServerRegistrationHandle handle,
                const resip::SipMessage& message,
                bool remove) {
    auto* platform =
        FindByGb(String(message.header(resip::h_From).uri().user()));
    if (!platform || !platform->enabled) {
      handle->reject(403);
      return;
    }
    SipEvent event;
    event.type =
        remove ? SipEventType::kUnregistered : SipEventType::kRegistered;
    event.platform_id = platform->id;
    event.remote_address = String(resip::Tuple::inet_ntop(message.getSource()));
    event.remote_port = static_cast<uint16_t>(message.getSource().getPort());
    event.transport =
        message.getSource().getType() == resip::TCP ? "tcp" : "udp";
    event.expires_seconds = message.exists(resip::h_Expires)
                                ? message.header(resip::h_Expires).value()
                                : 3600;
    if (!message.header(resip::h_Contacts).empty() &&
        message.header(resip::h_Contacts).front().exists(resip::p_expires))
      event.expires_seconds =
          message.header(resip::h_Contacts).front().param(resip::p_expires);
    if (remove || !event.expires_seconds)
      peers_.erase(platform->id);
    else
      peers_[platform->id] = {message.getSource(),
                              std::chrono::steady_clock::now() +
                                  std::chrono::seconds(event.expires_seconds)};
    handle->accept(200);
    Emit(std::move(event));
  }

  void onAdd(resip::ServerRegistrationHandle h,
             const resip::SipMessage& m) override {
    Register(h, m, false);
  }

  void onRefresh(resip::ServerRegistrationHandle h,
                 const resip::SipMessage& m) override {
    Register(h, m, false);
  }

  void onRemove(resip::ServerRegistrationHandle h,
                const resip::SipMessage& m) override {
    Register(h, m, true);
  }

  void onRemoveAll(resip::ServerRegistrationHandle h,
                   const resip::SipMessage& m) override {
    Register(h, m, true);
  }

  void onQuery(resip::ServerRegistrationHandle h,
               const resip::SipMessage& m) override {
    Register(h, m, false);
  }

  template <typename Handle>
  void ReceiveMessage(Handle handle, const resip::SipMessage& message) {
    if (message.method() == resip::OPTIONS) {
      handle->send(handle->accept());
      return;
    }
    auto* platform =
        FindByGb(String(message.header(resip::h_From).uri().user()));
    auto peer = platform ? peers_.find(platform->id) : peers_.end();
    if (!platform || !platform->enabled || peer == peers_.end() ||
        peer->second.deadline <= std::chrono::steady_clock::now() ||
        !(peer->second.source == message.getSource())) {
      handle->send(handle->reject(403));
      return;
    }
    auto parsed = ParseXml(Body(message));
    if (!parsed) {
      handle->send(handle->reject(400));
      return;
    }
    const auto& xml = parsed.value();
    if (xml.root != "Response" &&
        !(xml.root == "Notify" && xml.command == "Keepalive")) {
      handle->send(handle->reject(405));
      return;
    }
    handle->send(handle->accept());
    SipEvent event;
    event.type = xml.command == "Keepalive" ? SipEventType::kHeartbeat
                                            : SipEventType::kQueryResponse;
    event.platform_id = platform->id;
    event.command = xml.command;
    event.sn = xml.sn;
    event.device_gb_id = xml.device_id;
    event.catalog_total = xml.total;
    event.resources = xml.resources;
    event.info = xml.info;
    event.status = xml.status;
    Emit(std::move(event));
  }

  void onReceivedRequest(resip::ServerOutOfDialogReqHandle handle,
                         const resip::SipMessage& message) override {
    ReceiveMessage(handle, message);
  }

  void onMessageArrived(resip::ServerPagerMessageHandle handle,
                        const resip::SipMessage& message) override {
    ReceiveMessage(handle, message);
  }

  void onSuccess(resip::ClientOutOfDialogReqHandle,
                 const resip::SipMessage& message) override {
    requests_.erase(String(message.header(resip::h_CallId).value()));
  }

  void onSuccess(resip::ClientPagerMessageHandle handle,
                 const resip::SipMessage& message) override {
    requests_.erase(String(message.header(resip::h_CallId).value()));
    handle->end();
  }

  void onFailure(resip::ClientPagerMessageHandle handle,
                 const resip::SipMessage& message,
                 std::unique_ptr<resip::Contents>) override {
    auto found =
        requests_.find(String(message.header(resip::h_CallId).value()));
    if (found != requests_.end()) {
      auto event = found->second;
      requests_.erase(found);
      event.type = SipEventType::kRequestFailed;
      event.error = Failed(ErrorCode::kSipRequestRejected);
      Emit(std::move(event));
    }
    handle->end();
  }

  void onFailure(resip::ClientOutOfDialogReqHandle,
                 const resip::SipMessage& message) override {
    auto found =
        requests_.find(String(message.header(resip::h_CallId).value()));
    if (found == requests_.end())
      return;
    auto event = found->second;
    requests_.erase(found);
    event.type = SipEventType::kRequestFailed;
    event.error = Failed(ErrorCode::kSipRequestRejected);
    Emit(std::move(event));
  }

  resip::NameAddr Target(const PlatformId& id, const std::string& gb_id) {
    const auto& source = peers_.at(id).source;
    return resip::NameAddr(resip::Data(
        "sip:" + gb_id + "@" + String(resip::Tuple::inet_ntop(source)) + ":" +
        std::to_string(source.getPort()) +
        (source.getType() == resip::TCP ? ";transport=tcp"
                                        : ";transport=udp")));
  }

  std::shared_ptr<resip::UserProfile> Profile(const PlatformId& id) {
    auto profile =
        std::make_shared<resip::UserProfile>(dum_->getMasterUserProfile());
    // UserProfile's identity is not inherited through its Profile base chain.
    // Without this, MESSAGE/INVITE are sent with an empty From URI (sip:).
    profile->setDefaultFrom(dum_->getMasterUserProfile()->getDefaultFrom());
    profile->setClientOutboundFlowTuple(peers_.at(id).source);
    return profile;
  }

  void Query(const PlatformId& id,
             const std::string& target,
             const std::string& command,
             uint32_t sn,
             const std::string& request_id) {
    SipEvent error;
    error.platform_id = id;
    error.request_id = request_id;
    error.command = command;
    error.sn = sn;
    auto body = BuildQuery(command, sn, target);
    if (!peers_.count(id) || !body) {
      error.error = !body ? body.error() : Failed(ErrorCode::kPlatformOffline);
      Emit(error);
      return;
    }
    try {
      auto pager = dum_->makePagerMessage(Target(id, target), Profile(id));
      auto& message = pager->getMessageRequest();
      message.setDestination(peers_.at(id).source);
      requests_[String(message.header(resip::h_CallId).value())] = error;
      pager->page(std::make_unique<resip::PlainContents>(
          resip::Data(body.value()),
          resip::Mime("Application", "MANSCDP+xml")));
    } catch (...) {
      error.error = Failed(ErrorCode::kSipTransportFailed);
      Emit(error);
    }
  }

  void Invite(LiveInvite request) {
    SipEvent error;
    error.platform_id = request.platform_id;
    error.request_id = request.session_id;
    if (!peers_.count(request.platform_id)) {
      error.error = Failed(ErrorCode::kPlatformOffline);
      Emit(error);
      return;
    }
    try {
      auto sdp = BuildLiveSdp(config_.server_gb_id, request.receive_address,
                              request.receive_port, request.ssrc, request.mode);
      if (!sdp) {
        error.error = sdp.error();
        Emit(error);
        return;
      }
      resip::PlainContents content(resip::Data(sdp.value()),
                                   resip::Mime("application", "sdp"));
      auto* dialog = new ClientDialog(*this, *dum_);
      auto message = dum_->makeInviteSession(
          Target(request.platform_id, request.device_gb_id),
          Profile(request.platform_id), &content, dialog);
      message->setDestination(peers_.at(request.platform_id).source);
      sessions_[String(message->header(resip::h_CallId).value())] = {
          request.session_id, request.platform_id, dialog, {}, request.mode};
      dum_->send(message);
    } catch (...) {
      error.error = Failed(ErrorCode::kSipTransportFailed);
      Emit(error);
    }
  }

  void onNewSession(resip::ClientInviteSessionHandle handle,
                    resip::InviteSession::OfferAnswerType,
                    const resip::SipMessage& message) override {
    auto found =
        sessions_.find(String(message.header(resip::h_CallId).value()));
    if (found != sessions_.end())
      found->second.handle = handle->getSessionHandle();
  }

  void onNewSession(resip::ServerInviteSessionHandle h,
                    resip::InviteSession::OfferAnswerType,
                    const resip::SipMessage&) override {
    h->reject(405);
  }

  void SessionEvent(const resip::SipMessage& message,
                    SipEventType type,
                    gb28181::Error error = {ErrorCode::kOk, {}, {}}) {
    auto found =
        sessions_.find(String(message.header(resip::h_CallId).value()));
    if (found == sessions_.end())
      return;
    SipEvent event;
    event.type = type;
    event.platform_id = found->second.platform_id;
    event.request_id = found->second.id;
    if (type == SipEventType::kInviteAnswer) {
      auto answer = ParseLiveAnswer(Body(message), found->second.mode);
      if (!answer) {
        event.type = SipEventType::kRequestFailed;
        error = answer.error();
      } else {
        event.remote_address = answer.value().address;
        event.remote_port = answer.value().port;
        event.ssrc = answer.value().ssrc;
      }
    }
    event.error = std::move(error);
    Emit(std::move(event));
  }

  void onFailure(resip::ClientInviteSessionHandle,
                 const resip::SipMessage& message) override {
    SessionEvent(message, SipEventType::kRequestFailed,
                 Failed(ErrorCode::kSipRequestRejected));
    sessions_.erase(String(message.header(resip::h_CallId).value()));
  }

  void onConnected(resip::ClientInviteSessionHandle,
                   const resip::SipMessage& message) override {
    SessionEvent(message, SipEventType::kInviteAnswer);
  }

  void onConnected(resip::InviteSessionHandle,
                   const resip::SipMessage&) override {
  }

  void onTerminated(resip::InviteSessionHandle handle,
                    resip::InviteSessionHandler::TerminatedReason reason,
                    const resip::SipMessage* related) override {
    if (reason == resip::InviteSessionHandler::Error) {
      std::clog << "SIP session terminated: reason=" << static_cast<int>(reason)
                << " status="
                << (related && related->isResponse()
                        ? related->header(resip::h_StatusLine).statusCode()
                        : 0)
                << '\n';
    }
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
      if (it->second.handle == handle) {
        SipEvent event;
        event.type = SipEventType::kSessionEnded;
        event.platform_id = it->second.platform_id;
        event.request_id = it->second.id;
        sessions_.erase(it);
        Emit(std::move(event));
        break;
      }
    }
  }

  void onEarlyMedia(resip::ClientInviteSessionHandle,
                    const resip::SipMessage&,
                    const resip::SdpContents&) override {
  }

  void onProvisional(resip::ClientInviteSessionHandle,
                     const resip::SipMessage&) override {
  }

  void onForkDestroyed(resip::ClientInviteSessionHandle) override {
  }

  void onRedirected(resip::ClientInviteSessionHandle,
                    const resip::SipMessage& message) override {
    SessionEvent(message, SipEventType::kRequestFailed,
                 Failed(ErrorCode::kSipRequestRejected));
  }

  void onAnswer(resip::InviteSessionHandle,
                const resip::SipMessage&,
                const resip::SdpContents&) override {
  }

  void onAnswer(resip::InviteSessionHandle,
                const resip::SipMessage&,
                const resip::Contents&) override {
  }

  void onOffer(resip::InviteSessionHandle handle,
               const resip::SipMessage&,
               const resip::SdpContents&) override {
    handle->reject(488);
  }

  void onOffer(resip::InviteSessionHandle handle,
               const resip::SipMessage&,
               const resip::Contents&) override {
    handle->reject(488);
  }

  void onOfferRequired(resip::InviteSessionHandle handle,
                       const resip::SipMessage&) override {
    handle->reject(488);
  }

  void onOfferRejected(resip::InviteSessionHandle,
                       const resip::SipMessage*) override {
  }

  void onInfo(resip::InviteSessionHandle handle,
              const resip::SipMessage&) override {
    handle->rejectNIT(405);
  }

  void onInfoSuccess(resip::InviteSessionHandle,
                     const resip::SipMessage&) override {
  }

  void onInfoFailure(resip::InviteSessionHandle,
                     const resip::SipMessage&) override {
  }

  void onMessage(resip::InviteSessionHandle handle,
                 const resip::SipMessage&) override {
    handle->rejectNIT(405);
  }

  void onMessageSuccess(resip::InviteSessionHandle,
                        const resip::SipMessage&) override {
  }

  void onMessageFailure(resip::InviteSessionHandle,
                        const resip::SipMessage&) override {
  }

  void onRefer(resip::InviteSessionHandle,
               resip::ServerSubscriptionHandle handle,
               const resip::SipMessage&) override {
    handle->reject(405);
  }

  void onReferNoSub(resip::InviteSessionHandle handle,
                    const resip::SipMessage&) override {
    handle->rejectNIT(405);
  }

  void onReferRejected(resip::InviteSessionHandle,
                       const resip::SipMessage&) override {
  }

  void onReferAccepted(resip::InviteSessionHandle,
                       resip::ClientSubscriptionHandle,
                       const resip::SipMessage&) override {
  }

  Config config_;
  std::function<void(SipEvent)> handler_;
  std::map<PlatformId, PlatformConfig> platforms_;
  std::map<PlatformId, Peer> peers_;
  std::map<std::string, Session> sessions_;
  std::map<std::string, SipEvent> requests_;
  std::atomic<uint32_t> session_count_{0};
  std::atomic<uint32_t> request_count_{0};
  std::unique_ptr<resip::SipStack> stack_;
  std::unique_ptr<resip::DialogUsageManager> dum_;
  std::unique_ptr<resip::InMemoryRegistrationDatabase> registration_database_;
  std::mutex queue_mutex_;
  std::vector<std::function<void()>> commands_;
  bool accepting_ = false;
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
};

ResipService::ResipService() : impl_(std::make_unique<Impl>()) {
}

ResipService::~ResipService() {
  Stop();
}

SipStatistics ResipService::GetStatistics() const {
  return {impl_->session_count_.load(), impl_->request_count_.load()};
}

Result<void> ResipService::Start(const Config& config,
                                 const std::vector<PlatformConfig>& platforms,
                                 std::function<void(SipEvent)> handler) {
  return impl_->Start(config, platforms, std::move(handler));
}

void ResipService::Stop() {
  impl_->Stop();
}

Result<void> ResipService::ReplacePlatforms(
    std::vector<PlatformConfig> platforms) {
  if (!impl_->Post([this, platforms = std::move(platforms)] {
        const auto previous = std::move(impl_->platforms_);
        impl_->platforms_.clear();
        for (const auto& platform : platforms)
          impl_->platforms_[platform.id] = platform;
        for (auto it = impl_->peers_.begin(); it != impl_->peers_.end();) {
          auto found = impl_->platforms_.find(it->first);
          auto old = previous.find(it->first);
          if (found == impl_->platforms_.end() || !found->second.enabled ||
              old == previous.end() ||
              old->second.gb_id != found->second.gb_id ||
              old->second.username != found->second.username ||
              old->second.password != found->second.password ||
              old->second.realm != found->second.realm)
            it = impl_->peers_.erase(it);
          else
            ++it;
        }
      }))
    return Failed(ErrorCode::kInvalidState);
  return {};
}

Result<void> ResipService::SendQuery(const PlatformId& id,
                                     const std::string& target,
                                     const std::string& command,
                                     uint32_t sn,
                                     const std::string& request) {
  if (!impl_->Post([this, id, target, command, sn, request] {
        impl_->Query(id, target, command, sn, request);
      }))
    return Failed(ErrorCode::kInvalidState);
  return {};
}

Result<void> ResipService::InviteLive(LiveInvite request) {
  if (!impl_->Post([this, request = std::move(request)]() mutable {
        impl_->Invite(std::move(request));
      }))
    return Failed(ErrorCode::kInvalidState);
  return {};
}

Result<void> ResipService::TerminateSession(const SipSessionId& id) {
  if (!impl_->Post([this, id] {
        for (auto& item : impl_->sessions_)
          if (item.second.id == id && item.second.dialog) {
            item.second.dialog->end();
            return;
          }
      }))
    return Failed(ErrorCode::kInvalidState);
  return {};
}
}  // namespace gb28181::sip
