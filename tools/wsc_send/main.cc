// wsc-send: sends one WSC packet to a server over WebSocket and checks the
// reply. Drives the server by hand and from test scripts; exits 0 when the
// server accepted the packet.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "rtc/rtc.h"
#include "tools/wsc_send/request.h"

ABSL_FLAG(std::string, url, "ws://127.0.0.1:4515/ws", "Server endpoint.");
ABSL_FLAG(bool, keepalive, false,
          "Send STATE_QUERY(KEEPALIVE) instead of STREAM_CHANNELS.");
ABSL_FLAG(uint16_t, universe, 1, "sACN universe (1-63999).");
ABSL_FLAG(uint16_t, start, 1, "First channel (1-512).");
ABSL_FLAG(std::string, values, "255",
          "Channel values: comma-separated 0-255; v*n repeats v n times.");
ABSL_FLAG(std::string, dest, "",
          "Unicast sACN receiver a.b.c.d[:port]. Default: multicast.");
ABSL_FLAG(uint32_t, priority, 100, "sACN priority (0-200).");
ABSL_FLAG(absl::Duration, timeout, absl::Seconds(3),
          "How long to wait for the connection and for the reply.");

namespace wsc_server::wsc_send {
namespace {

constexpr uint32_t kPacketId = 1;
constexpr uint16_t kSacnPort = 5568;

// One libdatachannel WebSocket client that keeps the first binary reply.
class Client {
 public:
  explicit Client(const std::string& url)
      : ws_(rtcCreateWebSocket(url.c_str())) {
    rtcSetUserPointer(ws_, this);
    rtcSetOpenCallback(ws_, [](int, void* p) {
      static_cast<Client*>(p)->Set([](Client& c) { c.open_ = true; });
    });
    rtcSetClosedCallback(ws_, [](int, void* p) {
      static_cast<Client*>(p)->Set([](Client& c) { c.closed_ = true; });
    });
    rtcSetErrorCallback(ws_, [](int, const char* error, void* p) {
      const std::string text(error);
      static_cast<Client*>(p)->Set([&text](Client& c) { c.error_ = text; });
    });
    rtcSetMessageCallback(ws_, [](int, const char* data, int size, void* p) {
      if (size < 0) return;  // text: WebRTC signalling, not for us
      static_cast<Client*>(p)->Set([data, size](Client& c) {
        if (!c.reply_) c.reply_.emplace(data, data + size);
      });
    });
  }
  ~Client() { rtcDeleteWebSocket(ws_); }

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  absl::Status Connect(absl::Duration timeout) {
    absl::MutexLock lock(mutex_);
    mutex_.AwaitWithTimeout(
        absl::Condition(
            +[](Client* c) { return c->open_ || c->Failed(); }, this),
        timeout);
    if (open_) return absl::OkStatus();
    return absl::UnavailableError(
        absl::StrCat("cannot connect", error_.empty() ? "" : ": ", error_));
  }

  void Send(const std::vector<uint8_t>& packet) {
    rtcSendMessage(ws_, reinterpret_cast<const char*>(packet.data()),
                   static_cast<int>(packet.size()));
  }

  absl::StatusOr<std::vector<uint8_t>> Reply(absl::Duration timeout) {
    absl::MutexLock lock(mutex_);
    mutex_.AwaitWithTimeout(
        absl::Condition(
            +[](Client* c) { return c->reply_.has_value() || c->Failed(); },
            this),
        timeout);
    if (reply_) return *reply_;
    return absl::DeadlineExceededError(
        closed_ ? "connection closed without a reply" : "no reply");
  }

 private:
  template <typename F>
  void Set(F update) {
    absl::MutexLock lock(mutex_);
    update(*this);
  }
  [[nodiscard]] bool Failed() const ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    return closed_ || !error_.empty();
  }

  int ws_;
  absl::Mutex mutex_;
  bool open_ ABSL_GUARDED_BY(mutex_) = false;
  bool closed_ ABSL_GUARDED_BY(mutex_) = false;
  std::string error_ ABSL_GUARDED_BY(mutex_);
  std::optional<std::vector<uint8_t>> reply_ ABSL_GUARDED_BY(mutex_);
};

absl::StatusOr<std::vector<uint8_t>> PacketFromFlags() {
  if (absl::GetFlag(FLAGS_keepalive)) return EncodeKeepalive(kPacketId);
  const uint32_t priority = absl::GetFlag(FLAGS_priority);
  if (priority > 0xFF) {
    return absl::InvalidArgumentError("--priority must fit in one byte");
  }
  ChannelsRequest request{.universe = absl::GetFlag(FLAGS_universe),
                          .start_channel = absl::GetFlag(FLAGS_start),
                          .values = {},
                          .unicast = std::nullopt,
                          .priority = static_cast<uint8_t>(priority)};
  absl::StatusOr<std::vector<uint8_t>> values =
      ParseValues(absl::GetFlag(FLAGS_values));
  if (!values.ok()) return values.status();
  request.values = *std::move(values);
  if (const std::string dest = absl::GetFlag(FLAGS_dest); !dest.empty()) {
    absl::StatusOr<Ipv4AddrData> unicast = ParseIpv4Endpoint(dest, kSacnPort);
    if (!unicast.ok()) return unicast.status();
    request.unicast = *unicast;
  }
  return EncodeChannels(request, kPacketId);
}

absl::Status Run() {
  const absl::StatusOr<std::vector<uint8_t>> packet = PacketFromFlags();
  if (!packet.ok()) return packet.status();
  const absl::Duration timeout = absl::GetFlag(FLAGS_timeout);

  Client client(absl::GetFlag(FLAGS_url));
  if (absl::Status status = client.Connect(timeout); !status.ok()) {
    return status;
  }
  client.Send(*packet);
  const absl::StatusOr<std::vector<uint8_t>> reply = client.Reply(timeout);
  if (!reply.ok()) return reply.status();
  return CheckReply(*reply, kPacketId);
}

}  // namespace
}  // namespace wsc_server::wsc_send

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Sends one WSC packet over WebSocket and checks the server's reply.\n"
      "  wsc-send --universe=1 --values=255*512 --dest=127.0.0.1\n"
      "  wsc-send --keepalive");
  absl::ParseCommandLine(argc, argv);
  const absl::Status status = wsc_server::wsc_send::Run();
  rtcCleanup();  // joins libdatachannel's threads before static destructors
  if (status.ok()) {
    std::cout << "ok\n";
    return EXIT_SUCCESS;
  }
  std::cerr << "wsc-send: " << status << "\n";
  return EXIT_FAILURE;
}
