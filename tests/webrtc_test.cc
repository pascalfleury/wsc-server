// WebRTC sessions over loopback: a libdatachannel client signals on /ws and
// talks WSC over data channels, like the reference JS client.
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

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"
#include "nlohmann/json.hpp"
#include "rtc/rtc.h"
#include "src/core/clock.h"
#include "src/memory/frame_pool.h"
#include "src/platform/asio.h"
#include "src/routing/router.h"
#include "src/sessions/ws_server.h"
#include "testing/fake_output_adapter.h"

namespace wsc_server {
namespace {

// Upper bound only; handshakes take ~30 ms (~40 ms under ThreadSanitizer,
// after the warm-up in SetUpTestSuite).
constexpr absl::Duration kWait = absl::Seconds(10);

// Stops libdatachannel's threads before static destructors run.
class RtcEnvironment final : public testing::Environment {
 public:
  void TearDown() override { WsServer::ShutdownLibrary(); }
};
const auto* const kRtcEnvironment = testing::AddGlobalTestEnvironment(
    new RtcEnvironment);  // NOLINT: gtest owns it
using Json = nlohmann::json;

// A WebRTC client: signalling over a WebSocket, then WSC over data channels.
class RtcClient {
 public:
  RtcClient(const std::string& url, std::vector<std::string> labels)
      : labels_(std::move(labels)), ws_(rtcCreateWebSocket(url.c_str())) {
    rtcSetUserPointer(ws_, this);
    rtcSetOpenCallback(
        ws_, [](int, void* p) { static_cast<RtcClient*>(p)->Offer(); });
    rtcSetMessageCallback(ws_, [](int, const char* data, int size, void* p) {
      if (size < 0) static_cast<RtcClient*>(p)->OnSignal(data);
    });
  }
  ~RtcClient() {
    for (const auto& [label, dc] : channels_) rtcDeleteDataChannel(dc);
    if (pc_ >= 0) rtcDeletePeerConnection(pc_);
    rtcDeleteWebSocket(ws_);
  }

  bool WaitOpen(size_t n, absl::Duration wait = kWait) {
    absl::MutexLock lock(mutex_);
    auto done = [this, n] { return open_ >= n; };
    return mutex_.AwaitWithTimeout(absl::Condition(&done), wait);
  }
  bool WaitReceived(size_t n) {
    absl::MutexLock lock(mutex_);
    auto done = [this, n] { return received_.size() >= n; };
    return mutex_.AwaitWithTimeout(absl::Condition(&done), kWait);
  }

  void Send(std::string_view label, std::span<const uint8_t> bytes) {
    rtcSendMessage(channels_.at(std::string(label)),
                   reinterpret_cast<const char*>(bytes.data()),
                   static_cast<int>(bytes.size()));
  }

  // The n-th received packet and the label of the channel it came on.
  std::pair<std::string, std::vector<uint8_t>> Received(size_t n) {
    absl::MutexLock lock(mutex_);
    return received_.at(n);
  }

 private:
  void Offer() {
    rtcConfiguration config{};
    pc_ = rtcCreatePeerConnection(&config);
    rtcSetUserPointer(pc_, this);
    rtcSetLocalDescriptionCallback(
        pc_, [](int, const char* sdp, const char* type, void* p) {
          auto* self = static_cast<RtcClient*>(p);
          const std::string text =
              Json{{"type", "offer"}, {"offer", {{"type", type}, {"sdp", sdp}}}}
                  .dump();
          rtcSendMessage(self->ws_, text.c_str(), -1);
        });
    rtcSetLocalCandidateCallback(
        pc_, [](int, const char* cand, const char* mid, void* p) {
          auto* self = static_cast<RtcClient*>(p);
          const std::string text =
              Json{{"type", "ice"},
                   {"candidate", {{"candidate", cand}, {"sdpMid", mid}}}}
                  .dump();
          rtcSendMessage(self->ws_, text.c_str(), -1);
        });
    for (const std::string& label : labels_) {
      const int dc = rtcCreateDataChannel(pc_, label.c_str());
      channels_.emplace(label, dc);
      labels_by_id_.emplace(dc, label);
      rtcSetUserPointer(dc, this);
      rtcSetOpenCallback(dc, [](int, void* p) {
        auto* self = static_cast<RtcClient*>(p);
        absl::MutexLock lock(self->mutex_);
        ++self->open_;
      });
      rtcSetMessageCallback(dc, [](int id, const char* data, int size,
                                   void* p) {
        if (size < 0) return;
        auto* self = static_cast<RtcClient*>(p);
        absl::MutexLock lock(self->mutex_);
        self->received_.emplace_back(self->labels_by_id_.at(id),
                                     std::vector<uint8_t>(data, data + size));
      });
    }
  }

  void OnSignal(const char* text) {
    const Json json = Json::parse(text, nullptr, false);
    if (!json.is_object() || !json.contains("type")) return;
    if (json["type"] == "answer") {
      const std::string sdp = json["answer"]["sdp"].get<std::string>();
      rtcSetRemoteDescription(pc_, sdp.c_str(), "answer");
    } else if (json["type"] == "ice") {
      const std::string cand =
          json["candidate"]["candidate"].get<std::string>();
      const std::string mid = json["candidate"]["sdpMid"].get<std::string>();
      rtcAddRemoteCandidate(pc_, cand.c_str(), mid.c_str());
    }
  }

  // Written before any data channel callback can run, then read-only.
  std::vector<std::string> labels_;
  absl::flat_hash_map<std::string, int> channels_;
  absl::flat_hash_map<int, std::string> labels_by_id_;
  int ws_;
  int pc_ = -1;

  absl::Mutex mutex_;
  size_t open_ ABSL_GUARDED_BY(mutex_) = 0;
  std::vector<std::pair<std::string, std::vector<uint8_t>>> received_
      ABSL_GUARDED_BY(mutex_);
};

std::vector<uint8_t> EncodeOrDie(const wsc::Packet& packet) {
  std::vector<uint8_t> out(1024);
  const absl::StatusOr<size_t> size = wsc::Encode(packet, out);
  EXPECT_TRUE(size.ok()) << size.status();
  out.resize(size.value_or(0));
  return out;
}

std::vector<uint8_t> Keepalive(uint32_t id) {
  return EncodeOrDie(
      {.header = {.flags = 0, .id = id},
       .transport = std::nullopt,
       .body = wsc::StateQuery{
           .type = wsc::QueryType::kKeepalive, .target_id = "", .extra = {}}});
}

constexpr std::array<uint8_t, 6> kIpv4 = {10, 0, 0, 7, 0x15, 0xC0};
constexpr std::array<uint8_t, 4> kValues = {1, 2, 3, 4};

// A WsServer on a free loopback port, with its io_context threads and a fake
// sACN adapter behind the router.
class Harness {
 public:
  Harness() {
    EXPECT_TRUE(router_.Register(sacn_).ok());
    for (int i = 0; i < 2; ++i) threads_.emplace_back([this] { io_.run(); });
    absl::StatusOr<std::unique_ptr<WsServer>> server =
        WsServer::Create({.port = 0, .bind_address = "127.0.0.1"},
                         io_.get_executor(), router_, pool_, clock_);
    EXPECT_TRUE(server.ok()) << server.status();
    if (server.ok()) server_ = *std::move(server);
  }
  ~Harness() {
    server_.reset();
    work_.reset();
    io_.stop();
    for (std::thread& t : threads_) t.join();
  }

  [[nodiscard]] std::string Url() const {
    return absl::StrCat("ws://127.0.0.1:", server_->port(), "/ws");
  }
  WsServer& server() { return *server_; }
  FakeOutputAdapter& sacn() { return sacn_; }

 private:
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

// A client whose data channels are open. Retries once: with client and
// server in one process, usrsctp occasionally fails to bind a new connection's
// socket while an earlier one is still closing ("Could not bind usrsctp
// socket, errno=98", about 1 in 100 under ThreadSanitizer); the connection
// then fails and a real client reconnects.
std::unique_ptr<RtcClient> Connect(const std::string& url,
                                   const std::vector<std::string>& labels) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    auto client = std::make_unique<RtcClient>(url, labels);
    if (client->WaitOpen(labels.size())) return client;
  }
  ADD_FAILURE() << "data channels did not open";
  return nullptr;
}

class WebRtcTest : public testing::Test {
 protected:
  // The first WebRTC handshake in a process pays a one-time setup in the
  // crypto library: ~10 s under ThreadSanitizer, where it occasionally times
  // out. One throwaway connection here keeps that out of the tests.
  static void SetUpTestSuite() {
    Harness warm_up;
    RtcClient client(warm_up.Url(), {"WSC!DC:STATE_QUERY"});
    (void)client.WaitOpen(1, absl::Seconds(60));  // the result does not matter
  }

  Harness harness_;
};

TEST_F(WebRtcTest, AnswersOnTheChannelOfTheReplyType) {
  const auto client_ptr =
      Connect(harness_.Url(), {"WSC!DC:STATE_QUERY", "WSC!DC:STATE_ANSWER"});
  ASSERT_NE(client_ptr, nullptr);
  RtcClient& client = *client_ptr;
  client.Send("WSC!DC:STATE_QUERY", Keepalive(42));
  ASSERT_TRUE(client.WaitReceived(1));
  const auto [label, bytes] = client.Received(0);
  EXPECT_EQ(label, "WSC!DC:STATE_ANSWER");
  const wsc::Packet reply = wsc::Decode(bytes).value();
  EXPECT_EQ(std::get<wsc::StateAnswer>(reply.body).query_id, 42u);
  EXPECT_EQ(harness_.server().session_count(), 1u);  // one peer: one session
}

TEST_F(WebRtcTest, ChannelsOfOnePeerShareOneSession) {
  const auto client_ptr = Connect(
      harness_.Url(), {"WSC!DC:STREAM_CHANNELS", "WSC!DC:STATE_ANSWER"});
  ASSERT_NE(client_ptr, nullptr);
  RtcClient& client = *client_ptr;
  client.Send(
      "WSC!DC:STREAM_CHANNELS",
      EncodeOrDie(
          {.header = {.flags = wsc::flags::kTransport | wsc::flags::kGateway |
                               wsc::flags::kAck,
                      .id = 5},
           .transport =
               wsc::TransportDescriptor{.protocol = wsc::Protocol::kSacn,
                                        .iface = wsc::Iface::kUdp,
                                        .addr_type = wsc::AddrType::kIpv4,
                                        .addr_data = kIpv4,
                                        .params = {}},
           .body = wsc::StreamChannels{
               .universe = 1, .start_channel = 1, .values = kValues}}));
  ASSERT_TRUE(client.WaitReceived(1));
  const auto [label, bytes] = client.Received(0);
  EXPECT_EQ(label, "WSC!DC:STATE_ANSWER");
  EXPECT_EQ(
      std::get<wsc::StateAnswer>(wsc::Decode(bytes).value().body).query_id, 5u);
  // The session submitted before it answered, on the same strand.
  ASSERT_EQ(harness_.sacn().submitted.size(), 1u);
}

TEST_F(WebRtcTest, SingleSpecStyleChannelGetsTheAnswersToo) {
  const auto client_ptr = Connect(harness_.Url(), {"WSC!DC!1732600000000"});
  ASSERT_NE(client_ptr, nullptr);
  RtcClient& client = *client_ptr;
  client.Send("WSC!DC!1732600000000", Keepalive(7));
  ASSERT_TRUE(client.WaitReceived(1));
  EXPECT_EQ(client.Received(0).first, "WSC!DC!1732600000000");
}

}  // namespace
}  // namespace wsc_server
