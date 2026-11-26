#include "src/sessions/ws_server.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
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
#include "src/sessions/session.h"

namespace wsc_server {
namespace {

constexpr std::string_view kPath = "/ws";
// Spec 06 send path: above this many queued bytes a reply is dropped rather
// than queued, so latency cannot build up behind a slow peer.
constexpr int kMaxBufferedBytes = 64 * 1024;
constexpr absl::Duration kTimeoutCheck = absl::Seconds(1);

void LogFromRtc(rtcLogLevel level, const char* message) {
  if (level <= RTC_LOG_ERROR) {
    LOG(ERROR) << "libdatachannel: " << message;
  } else {
    LOG(WARNING) << "libdatachannel: " << message;
  }
}

}  // namespace

// One WebSocket connection: the transport of its Session, which it runs on
// its own strand together with the timeout check.
class WsServer::Connection final
    : public SessionTransport,
      public std::enable_shared_from_this<Connection> {
 public:
  Connection(int ws, const asio::any_io_executor& executor,
             const Router& router, FramePool& pool, const Clock& clock)
      : ws_(ws),
        strand_(asio::make_strand(executor)),
        timer_(strand_),
        session_(*this, router, pool, clock) {}

  void Start() {
    asio::post(strand_, [self = shared_from_this()] { self->Arm(); });
  }

  void Receive(std::shared_ptr<const Frame> frame) {
    asio::post(strand_, [self = shared_from_this(), frame = std::move(frame)] {
      self->session_.OnFrame(frame);
    });
  }

  // Stops the timer; the last posted work then releases the connection.
  void Stop() {
    asio::post(strand_, [self = shared_from_this()] { self->timer_.cancel(); });
  }

  // SessionTransport, called on the strand.
  void Send(std::span<const uint8_t> packet) override {
    if (rtcGetBufferedAmount(ws_) > kMaxBufferedBytes) {
      LOG_EVERY_N_SEC(WARNING, 5)
          << "WebSocket " << ws_ << ": send buffer full, reply dropped";
      return;
    }
    rtcSendMessage(ws_, reinterpret_cast<const char*>(packet.data()),
                   static_cast<int>(packet.size()));
  }
  void Close() override { rtcClose(ws_); }

 private:
  void Arm() {
    timer_.expires_after(absl::ToChronoNanoseconds(kTimeoutCheck));
    timer_.async_wait([self = shared_from_this()](const asio::error_code& ec) {
      if (ec) return;  // stopped
      if (!self->session_.CloseIfTimedOut()) self->Arm();
    });
  }

  int ws_;
  asio::strand<asio::any_io_executor> strand_;
  asio::steady_timer timer_;
  Session session_;
};

absl::StatusOr<std::unique_ptr<WsServer>> WsServer::Create(
    const Options& options, const asio::any_io_executor& executor,
    const Router& router, FramePool& pool, const Clock& clock) {
  static absl::once_flag logger_once;
  absl::call_once(logger_once,
                  [] { rtcInitLogger(RTC_LOG_WARNING, LogFromRtc); });

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
  rtcDeleteWebSocketServer(server_);  // no new connections
  absl::flat_hash_map<int, std::shared_ptr<Connection>> connections;
  {
    absl::MutexLock lock(mutex_);
    connections.swap(connections_);
  }
  for (auto& [ws, connection] : connections) {
    // Blocks until the socket's running callbacks return; none run after.
    rtcDeleteWebSocket(ws);
    connection->Stop();
  }
}

uint16_t WsServer::port() const {
  return static_cast<uint16_t>(rtcGetWebSocketServerPort(server_));
}

size_t WsServer::connection_count() const {
  absl::MutexLock lock(mutex_);
  return connections_.size();
}

void WsServer::OnClient(int /*server*/, int ws, void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  auto connection = std::make_shared<Connection>(
      ws, self->executor_, self->router_, self->pool_, self->clock_);
  {
    absl::MutexLock lock(self->mutex_);
    self->connections_.emplace(ws, connection);
  }
  rtcSetUserPointer(ws, self);
  rtcSetOpenCallback(ws, &WsServer::OnOpen);
  rtcSetClosedCallback(ws, &WsServer::OnClosed);
  rtcSetMessageCallback(ws, &WsServer::OnMessage);
  connection->Start();
}

void WsServer::OnOpen(int ws, void* /*ptr*/) {
  std::array<char, 256> path{};
  if (rtcGetWebSocketPath(ws, path.data(), static_cast<int>(path.size())) < 0 ||
      std::string_view(path.data()) != kPath) {
    LOG(WARNING) << "WebSocket " << ws << ": path is not " << kPath;
    rtcClose(ws);
  }
}

void WsServer::OnClosed(int ws, void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  std::shared_ptr<Connection> connection;
  {
    absl::MutexLock lock(self->mutex_);
    const auto it = self->connections_.find(ws);
    // Not found: the destructor took it and is deleting the socket; it waits
    // for this callback to return.
    if (it == self->connections_.end()) return;
    connection = std::move(it->second);
    self->connections_.erase(it);
  }
  // From here on `self` may be gone: the destructor no longer knows `ws` and
  // does not wait for us. Deleting from within the socket's own callback is
  // allowed (libdatachannel DOC.md, rtcDeleteWebSocket).
  rtcDeleteWebSocket(ws);
  connection->Stop();
}

void WsServer::OnMessage(int ws, const char* data, int size, void* ptr) {
  auto* self = static_cast<WsServer*>(ptr);
  if (size < 0) {
    // shortcut: text messages are WebRTC signalling, handled from Phase 3
    // step 6C; until then they are ignored.
    LOG_EVERY_N_SEC(INFO, 5)
        << "WebSocket " << ws << ": WebRTC signalling is not supported yet";
    return;
  }
  const std::shared_ptr<Connection> connection = self->Find(ws);
  if (connection == nullptr) return;
  absl::StatusOr<std::shared_ptr<Frame>> frame =
      self->pool_.Acquire(static_cast<size_t>(size));
  if (!frame.ok()) {
    // shortcut: dropped and logged, as in Session::OnMessage.
    LOG_EVERY_N_SEC(WARNING, 5) << "WebSocket " << ws << ": " << frame.status();
    return;
  }
  const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(data),
                                       static_cast<size_t>(size));
  std::ranges::copy(bytes, (*frame)->mutable_buffer().begin());
  (*frame)->set_size(bytes.size());
  connection->Receive(*std::move(frame));
}

std::shared_ptr<WsServer::Connection> WsServer::Find(int ws) const {
  absl::MutexLock lock(mutex_);
  const auto it = connections_.find(ws);
  return it == connections_.end() ? nullptr : it->second;
}

}  // namespace wsc_server
