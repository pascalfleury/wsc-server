#include "src/sessions/ws_server.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "rtc/rtc.h"
#include "src/memory/frame.h"
#include "src/platform/asio.h"
#include "src/sessions/rtc_transports.h"
#include "src/sessions/session_host.h"
#include "src/sessions/signalling.h"

namespace wsc_server {
namespace {

constexpr std::string_view kPath = "/ws";

void LogFromRtc(rtcLogLevel level, const char* message) {
  if (level <= RTC_LOG_ERROR) {
    LOG(ERROR) << "libdatachannel: " << message;
  } else {
    LOG(WARNING) << "libdatachannel: " << message;
  }
}

void SendText(int ws, const std::string& text) {
  rtcSendMessage(ws, text.c_str(), -1);  // size < 0: null-terminated text
}

}  // namespace

absl::StatusOr<std::unique_ptr<WsServer>> WsServer::Create(
    const Options& options, const asio::any_io_executor& executor,
    const Router& router, FramePool& pool, const Clock& clock) {
  static absl::once_flag logger_once;
  absl::call_once(logger_once, [] {
    rtcInitLogger(RTC_LOG_WARNING, LogFromRtc);
    // Loads the global resources (e.g. the DTLS certificate) now, so the
    // first client's handshake does not wait for them.
    rtcPreload();
  });

  std::unique_ptr<WsServer> server(new WsServer(executor, router, pool, clock));
  rtcWsServerConfiguration config{};
  config.port = options.port;
  config.bindAddress =
      options.bind_address.empty() ? nullptr : options.bind_address.c_str();
  server->server_ = rtcCreateWebSocketServer(&config, &WsServer::OnClient);
  if (server->server_ < 0) {
    return absl::UnavailableError(
        absl::StrCat("cannot listen for WebSockets on port ", options.port));
  }
  rtcSetUserPointer(server->server_, server.get());
  return server;
}

WsServer::~WsServer() {
  rtcDeleteWebSocketServer(server_);  // no new clients
  absl::flat_hash_map<int, Entry> entries;
  {
    absl::MutexLock lock(mutex_);
    entries.swap(entries_);
  }
  // Each delete blocks until the object's running callbacks have returned;
  // none run afterwards. Channels before their peer connections.
  for (const Entry::Kind kind :
       {Entry::Kind::kDataChannel, Entry::Kind::kPeerConnection,
        Entry::Kind::kWebSocket}) {
    for (auto& [id, entry] : entries) {
      if (entry.kind != kind) continue;
      switch (kind) {
        case Entry::Kind::kDataChannel:
          rtcDeleteDataChannel(id);
          break;
        case Entry::Kind::kPeerConnection:
          rtcDeletePeerConnection(id);
          break;
        case Entry::Kind::kWebSocket:
          rtcDeleteWebSocket(id);
          break;
      }
      if (kind != Entry::Kind::kDataChannel && entry.host) entry.host->Stop();
    }
  }
  // Reaps already posted find nothing left, or finish what they took.
  absl::MutexLock lock(mutex_);
  mutex_.Await(absl::Condition(
      +[](int* pending) { return *pending == 0; }, &pending_reaps_));
}

void WsServer::ShutdownLibrary() { rtcCleanup(); }

uint16_t WsServer::port() const {
  return static_cast<uint16_t>(rtcGetWebSocketServerPort(server_));
}

size_t WsServer::session_count() const {
  absl::MutexLock lock(mutex_);
  size_t count = 0;
  for (const auto& [id, entry] : entries_) {
    if (entry.kind != Entry::Kind::kDataChannel && entry.host) ++count;
  }
  return count;
}

void WsServer::OnClient(int /*server*/, int ws, void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  {
    absl::MutexLock lock(self->mutex_);
    self->entries_.emplace(ws, Entry{.kind = Entry::Kind::kWebSocket});
  }
  rtcSetUserPointer(ws, self);
  rtcSetOpenCallback(ws, &WsServer::OnOpen);
  rtcSetClosedCallback(ws, &WsServer::OnClosed);
  rtcSetMessageCallback(ws, &WsServer::OnWebSocketMessage);
}

void WsServer::OnOpen(int ws, void* /*ptr*/) {
  std::array<char, 256> path{};
  if (rtcGetWebSocketPath(ws, path.data(), static_cast<int>(path.size())) < 0 ||
      std::string_view(path.data()) != kPath) {
    LOG(WARNING) << "WebSocket " << ws << ": path is not " << kPath;
    rtcClose(ws);
  }
}

void WsServer::OnClosed(int id, void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  absl::MutexLock lock(self->mutex_);
  // Not found: the destructor took it and is deleting the object.
  if (const auto it = self->entries_.find(id); it != self->entries_.end()) {
    self->ScheduleReap(id, it->second);
  }
}

void WsServer::ScheduleReap(int id, Entry& entry) {
  if (entry.closing) return;
  entry.closing = true;
  ++pending_reaps_;
  asio::post(executor_, [this, id] { Reap(id); });
}

void WsServer::Reap(int id) {
  Entry entry;
  bool found = false;
  {
    absl::MutexLock lock(mutex_);
    if (const auto it = entries_.find(id); it != entries_.end()) {
      entry = std::move(it->second);
      entries_.erase(it);
      found = true;
      if (entry.kind == Entry::Kind::kPeerConnection) {
        if (const auto ws = entries_.find(entry.link);
            ws != entries_.end() && ws->second.link == id) {
          ws->second.link = -1;
        }
      }
    }
  }
  if (found) {
    switch (entry.kind) {
      case Entry::Kind::kDataChannel:
        if (entry.peer != nullptr) entry.peer->RemoveChannel(id);
        rtcDeleteDataChannel(id);
        break;
      case Entry::Kind::kPeerConnection:
        rtcDeletePeerConnection(id);  // its channels are reaped on their own
        entry.host->Stop();
        break;
      case Entry::Kind::kWebSocket:
        rtcDeleteWebSocket(id);  // its peer connection lives on
        if (entry.host) entry.host->Stop();
        break;
    }
  }
  absl::MutexLock lock(mutex_);
  --pending_reaps_;
}

void WsServer::OnWebSocketMessage(int ws, const char* data, int size,
                                  void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  if (size < 0) {
    self->HandleSignalling(ws, data);
  } else {
    self->HandleBinary(ws, data, size);
  }
}

void WsServer::HandleBinary(int ws, const char* data, int size) {
  std::shared_ptr<SessionHost> host;
  bool created = false;
  {
    absl::MutexLock lock(mutex_);
    const auto it = entries_.find(ws);
    if (it == entries_.end()) return;
    if (!it->second.host) {
      it->second.host = std::make_shared<SessionHost>(
          std::make_unique<WebSocketTransport>(ws), executor_, router_, pool_,
          clock_);
      created = true;
    }
    host = it->second.host;
  }
  if (created) host->Start();
  Deliver(*host, data, size);
}

void WsServer::HandleSignalling(int ws, std::string_view text) {
  const absl::StatusOr<std::optional<signalling::Message>> message =
      signalling::Parse(text);
  if (!message.ok()) {
    LOG_EVERY_N_SEC(WARNING, 5)
        << "WebSocket " << ws << ": " << message.status();
    return;
  }
  const std::optional<signalling::Message>& signal = *message;
  if (!signal) return;  // nothing for the server to do
  if (const auto* offer = std::get_if<signalling::Offer>(&*signal)) {
    StartPeer(ws, offer->sdp);
    return;
  }
  const auto& candidate = std::get<signalling::Candidate>(*signal);
  int pc = -1;
  {
    absl::MutexLock lock(mutex_);
    const auto it = entries_.find(ws);
    if (it != entries_.end()) pc = it->second.link;
  }
  if (pc >= 0) {
    rtcAddRemoteCandidate(pc, candidate.candidate.c_str(),
                          candidate.mid.c_str());
  }
}

void WsServer::StartPeer(int ws, const std::string& sdp) {
  {
    absl::MutexLock lock(mutex_);
    const auto it = entries_.find(ws);
    if (it == entries_.end() || it->second.link >= 0) {
      LOG(WARNING) << "WebSocket " << ws << ": offer ignored";
      return;
    }
  }
  rtcConfiguration config{};  // no ICE servers: host candidates only (spec 06)
  const int pc = rtcCreatePeerConnection(&config);
  if (pc < 0) {
    LOG(ERROR) << "WebSocket " << ws << ": cannot create a peer connection";
    return;
  }
  auto transport = std::make_unique<PeerTransport>(pc);
  PeerTransport* peer = transport.get();
  auto host = std::make_shared<SessionHost>(std::move(transport), executor_,
                                            router_, pool_, clock_);
  {
    absl::MutexLock lock(mutex_);
    entries_.emplace(pc, Entry{.kind = Entry::Kind::kPeerConnection,
                               .host = host,
                               .peer = peer,
                               .link = ws});
    if (const auto it = entries_.find(ws); it != entries_.end()) {
      it->second.link = pc;
    }
  }
  rtcSetUserPointer(pc, this);
  rtcSetLocalDescriptionCallback(pc, &WsServer::OnLocalDescription);
  rtcSetLocalCandidateCallback(pc, &WsServer::OnLocalCandidate);
  rtcSetStateChangeCallback(pc, &WsServer::OnStateChange);
  rtcSetDataChannelCallback(pc, &WsServer::OnDataChannel);
  // The session's timeout starts with its first WSC data channel
  // (OnDataChannel); a peer that never connects fails or closes by itself.
  // libdatachannel answers by itself and reports it to OnLocalDescription.
  if (rtcSetRemoteDescription(pc, sdp.c_str(), "offer") < 0) {
    LOG(WARNING) << "WebSocket " << ws << ": offer rejected";
    rtcClosePeerConnection(pc);
  }
}

int WsServer::SignallingSocketOf(int pc) const {
  absl::MutexLock lock(mutex_);
  const auto it = entries_.find(pc);
  return it != entries_.end() ? it->second.link : -1;
}

void WsServer::OnLocalDescription(int pc, const char* sdp, const char* type,
                                  void* ptr) {
  const int ws = static_cast<WsServer*>(ptr)->SignallingSocketOf(pc);
  if (ws >= 0) SendText(ws, signalling::AnswerJson(type, sdp));
}

void WsServer::OnLocalCandidate(int pc, const char* candidate, const char* mid,
                                void* ptr) {
  const int ws = static_cast<WsServer*>(ptr)->SignallingSocketOf(pc);
  if (ws >= 0) SendText(ws, signalling::CandidateJson(candidate, mid));
}

void WsServer::OnStateChange(int pc, rtcState state, void* ptr) {
  if (state != RTC_FAILED && state != RTC_CLOSED) return;
  auto* self = static_cast<WsServer*>(ptr);
  absl::MutexLock lock(self->mutex_);
  if (const auto it = self->entries_.find(pc); it != self->entries_.end()) {
    self->ScheduleReap(pc, it->second);
  }
}

void WsServer::OnDataChannel(int pc, int dc, void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  std::array<char, 256> label{};
  const bool wsc = rtcGetDataChannelLabel(
                       dc, label.data(), static_cast<int>(label.size())) >= 0 &&
                   IsWscChannelLabel(label.data());
  Entry entry{.kind = Entry::Kind::kDataChannel};
  {
    absl::MutexLock lock(self->mutex_);
    if (const auto it = self->entries_.find(pc);
        it != self->entries_.end() && wsc) {
      entry.host = it->second.host;
      entry.peer = it->second.peer;
    }
    self->entries_.emplace(dc, entry);
  }
  rtcSetUserPointer(dc, self);
  rtcSetClosedCallback(dc, &WsServer::OnClosed);
  if (entry.peer == nullptr) {
    LOG(WARNING) << "peer " << pc << ": data channel '" << label.data()
                 << "' is not a WSC channel";
    rtcClose(dc);
    return;
  }
  entry.peer->AddChannel(label.data(), dc);
  rtcSetMessageCallback(dc, &WsServer::OnChannelMessage);
  entry.host->Start();  // the first channel starts the 5 s timeout
}

void WsServer::OnChannelMessage(int dc, const char* data, int size, void* ptr) {
  if (size < 0) return;  // WSC is binary
  auto* self = static_cast<WsServer*>(ptr);
  std::shared_ptr<SessionHost> host;
  PeerTransport* peer = nullptr;
  {
    absl::MutexLock lock(self->mutex_);
    const auto it = self->entries_.find(dc);
    if (it == self->entries_.end() || !it->second.host) return;
    host = it->second.host;
    peer = it->second.peer;
  }
  peer->NoteReceived(dc);
  self->Deliver(*host, data, size);
}

void WsServer::Deliver(SessionHost& host, const char* data, int size) {
  absl::StatusOr<std::shared_ptr<Frame>> frame =
      pool_.Acquire(static_cast<size_t>(size));
  if (!frame.ok()) {
    // shortcut: dropped and logged, as in Session::OnMessage.
    LOG_EVERY_N_SEC(WARNING, 5) << "session: " << frame.status();
    return;
  }
  const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(data),
                                       static_cast<size_t>(size));
  std::ranges::copy(bytes, (*frame)->mutable_buffer().begin());
  (*frame)->set_size(bytes.size());
  host.Receive(*std::move(frame));
}

}  // namespace wsc_server
