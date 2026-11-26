// The WebSocket endpoint over loopback, with libdatachannel's client
// (CODING.org: real sockets only in tests/).
#include "src/sessions/ws_server.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"
#include "rtc/rtc.h"
#include "src/core/clock.h"
#include "src/memory/frame_pool.h"
#include "src/platform/asio.h"
#include "src/routing/router.h"
#include "testing/fake_output_adapter.h"

namespace wsc_server {
namespace {

constexpr absl::Duration kWait = absl::Seconds(5);

// A libdatachannel WebSocket client that records what it receives.
class Client {
 public:
  explicit Client(const std::string& url)
      : ws_(rtcCreateWebSocket(url.c_str())) {
    rtcSetUserPointer(ws_, this);
    rtcSetOpenCallback(ws_, [](int, void* p) {
      auto* self = static_cast<Client*>(p);
      absl::MutexLock lock(self->mutex_);
      self->open_ = true;
    });
    rtcSetClosedCallback(ws_, [](int, void* p) {
      auto* self = static_cast<Client*>(p);
      absl::MutexLock lock(self->mutex_);
      self->closed_ = true;
    });
    rtcSetMessageCallback(ws_, [](int, const char* data, int size, void* p) {
      if (size < 0) return;
      auto* self = static_cast<Client*>(p);
      absl::MutexLock lock(self->mutex_);
      self->received_.emplace_back(data, data + size);
    });
  }
  ~Client() { rtcDeleteWebSocket(ws_); }

  bool WaitOpen() {
    return Wait([this] { return open_; });
  }
  bool WaitClosed() {
    return Wait([this] { return closed_; });
  }
  bool WaitReceived(size_t n) {
    return Wait([this, n] { return received_.size() >= n; });
  }

  void Send(std::span<const uint8_t> bytes) {
    rtcSendMessage(ws_, reinterpret_cast<const char*>(bytes.data()),
                   static_cast<int>(bytes.size()));
  }

  // The n-th received packet, decoded; its views point into this client.
  wsc::Packet Received(size_t n) {
    absl::MutexLock lock(mutex_);
    return wsc::Decode(received_.at(n)).value();
  }

 private:
  template <typename F>
  bool Wait(F done) {
    absl::MutexLock lock(mutex_);
    return mutex_.AwaitWithTimeout(absl::Condition(&done), kWait);
  }

  int ws_;
  absl::Mutex mutex_;
  bool open_ ABSL_GUARDED_BY(mutex_) = false;
  bool closed_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::vector<uint8_t>> received_ ABSL_GUARDED_BY(mutex_);
};

std::vector<uint8_t> EncodeOrDie(const wsc::Packet& packet) {
  std::vector<uint8_t> out(1024);
  const absl::StatusOr<size_t> size = wsc::Encode(packet, out);
  EXPECT_TRUE(size.ok()) << size.status();
  out.resize(size.value_or(0));
  return out;
}

constexpr std::array<uint8_t, 6> kIpv4 = {10, 0, 0, 7, 0x15, 0xC0};
constexpr std::array<uint8_t, 4> kValues = {1, 2, 3, 4};

class WsServerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(router_.Register(sacn_).ok());
    for (int i = 0; i < 2; ++i) threads_.emplace_back([this] { io_.run(); });
    absl::StatusOr<std::unique_ptr<WsServer>> server =
        WsServer::Create({.port = 0, .bind_address = "127.0.0.1"},
                         io_.get_executor(), router_, pool_, clock_);
    ASSERT_TRUE(server.ok()) << server.status();
    server_ = *std::move(server);
  }
  void TearDown() override {
    server_.reset();
    work_.reset();
    io_.stop();
    for (std::thread& t : threads_) t.join();
  }

  std::string Url(std::string_view path = "/ws") const {
    return absl::StrCat("ws://127.0.0.1:", server_->port(), path);
  }

  SystemClock clock_;
  FakeOutputAdapter sacn_{wsc::Protocol::kSacn, {wsc::Iface::kUdp}};
  Router router_;
  FramePool pool_{clock_};
  asio::io_context io_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_{asio::make_work_guard(io_)};
  std::vector<std::thread> threads_;
  std::unique_ptr<WsServer> server_;
};

TEST_F(WsServerTest, AnswersAKeepaliveOverWebSocket) {
  Client client(Url());
  ASSERT_TRUE(client.WaitOpen());
  client.Send(EncodeOrDie(
      {.header = {.flags = 0, .id = 42},
       .transport = std::nullopt,
       .body = wsc::StateQuery{
           .type = wsc::QueryType::kKeepalive, .target_id = "", .extra = {}}}));
  ASSERT_TRUE(client.WaitReceived(1));
  const wsc::Packet reply = client.Received(0);
  const auto& answer = std::get<wsc::StateAnswer>(reply.body);
  EXPECT_EQ(answer.query_id, 42u);
  EXPECT_EQ(answer.status, wsc::AnswerStatus::kSuccess);
  EXPECT_EQ(server_->connection_count(), 1u);
}

TEST_F(WsServerTest, RoutesForwardedPacketsAndAcknowledges) {
  Client client(Url());
  ASSERT_TRUE(client.WaitOpen());
  client.Send(EncodeOrDie(
      {.header = {.flags = wsc::flags::kTransport | wsc::flags::kGateway |
                           wsc::flags::kAck,
                  .id = 5},
       .transport = wsc::TransportDescriptor{.protocol = wsc::Protocol::kSacn,
                                             .iface = wsc::Iface::kUdp,
                                             .addr_type = wsc::AddrType::kIpv4,
                                             .addr_data = kIpv4,
                                             .params = {}},
       .body = wsc::StreamChannels{
           .universe = 1, .start_channel = 1, .values = kValues}}));
  ASSERT_TRUE(client.WaitReceived(1));
  const wsc::Packet reply = client.Received(0);
  EXPECT_EQ(std::get<wsc::StateAnswer>(reply.body).query_id, 5u);
  // The session submitted before it answered, on the same strand.
  ASSERT_EQ(sacn_.submitted.size(), 1u);
  EXPECT_EQ(sacn_.submitted[0].packet.header.id, 5u);
}

TEST_F(WsServerTest, ClosesConnectionsToOtherPaths) {
  Client client(Url("/other"));
  EXPECT_TRUE(client.WaitClosed());
}

}  // namespace
}  // namespace wsc_server
