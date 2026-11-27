#include "src/sessions/session.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"
#include "src/memory/frame_pool.h"
#include "src/routing/router.h"
#include "testing/fake_clock.h"
#include "testing/fake_output_adapter.h"
#include "testing/fake_reply_sink.h"

namespace wsc_server {
namespace {

class FakeTransport final : public SessionTransport {
 public:
  void Send(std::span<const uint8_t> packet) override {
    sent.emplace_back(packet.begin(), packet.end());
  }
  void Close() override { closed = true; }

  std::vector<std::vector<uint8_t>> sent;
  bool closed = false;
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

std::vector<uint8_t> Keepalive(uint32_t id) {
  return EncodeOrDie(
      {.header = {.flags = 0, .id = id},
       .transport = std::nullopt,
       .body = wsc::StateQuery{
           .type = wsc::QueryType::kKeepalive, .target_id = "", .extra = {}}});
}

std::vector<uint8_t> Channels(uint32_t id, wsc::Protocol protocol,
                              uint16_t extra_flags = 0) {
  return EncodeOrDie(
      {.header = {.flags =
                      static_cast<uint16_t>(wsc::flags::kTransport |
                                            wsc::flags::kGateway | extra_flags),
                  .id = id},
       .transport = wsc::TransportDescriptor{.protocol = protocol,
                                             .iface = wsc::Iface::kUdp,
                                             .addr_type = wsc::AddrType::kIpv4,
                                             .addr_data = kIpv4,
                                             .params = {}},
       .body = wsc::StreamChannels{
           .universe = 1, .start_channel = 1, .values = kValues}});
}

class SessionTest : public testing::Test {
 protected:
  SessionTest() { EXPECT_TRUE(router_.Register(sacn_).ok()); }

  // The n-th packet the session sent, decoded.
  wsc::Packet Sent(size_t n) {
    EXPECT_LT(n, transport_.sent.size());
    return wsc::Decode(transport_.sent.at(n)).value();
  }

  FakeClock clock_;
  FakeTransport transport_;
  FakeOutputAdapter sacn_{wsc::Protocol::kSacn, {wsc::Iface::kUdp}};
  Router router_;
  FramePool pool_{clock_};
  Session session_{transport_, router_, pool_, clock_};
};

TEST_F(SessionTest, AnswersKeepaliveWithSuccess) {
  session_.OnMessage(Keepalive(7));
  ASSERT_EQ(transport_.sent.size(), 1u);
  const wsc::Packet reply = Sent(0);
  const auto& answer = std::get<wsc::StateAnswer>(reply.body);
  EXPECT_EQ(answer.query_id, 7u);
  EXPECT_EQ(answer.status, wsc::AnswerStatus::kSuccess);
  EXPECT_EQ(reply.header.id, 1u);  // the session's own sequence
}

TEST_F(SessionTest, RoutesForwardedPacketsIntoPooledFrames) {
  session_.OnMessage(Channels(3, wsc::Protocol::kSacn));
  ASSERT_EQ(sacn_.submitted.size(), 1u);
  const Message& message = sacn_.submitted[0];
  EXPECT_EQ(message.packet.header.id, 3u);
  // The views point into the pooled frame, not into the transport's buffer.
  const auto& channels = std::get<wsc::StreamChannels>(message.packet.body);
  const std::span<const uint8_t> frame = message.frame->bytes();
  EXPECT_GE(channels.values.data(), frame.data());
  EXPECT_LE(channels.values.data() + channels.values.size(),
            frame.data() + frame.size());
  EXPECT_TRUE(transport_.sent.empty());  // no ACK requested
}

TEST_F(SessionTest, PassesItsReplySinkWithEachMessage) {
  const auto sink = std::make_shared<FakeReplySink>();
  session_.OnMessage(Channels(4, wsc::Protocol::kSacn), sink);
  ASSERT_EQ(sacn_.submitted.size(), 1u);
  EXPECT_EQ(sacn_.submitted[0].reply.lock(), sink);
}

TEST_F(SessionTest, AnswersAckOnlyOnceTheAdapterAcceptedThePacket) {
  session_.OnMessage(Channels(9, wsc::Protocol::kSacn, wsc::flags::kAck));
  EXPECT_TRUE(transport_.sent.empty());  // routed, adapter not done yet
  session_.OnResult(sacn_.submitted.at(0).packet.header, absl::OkStatus());
  ASSERT_EQ(transport_.sent.size(), 1u);
  const wsc::Packet reply = Sent(0);
  const auto& answer = std::get<wsc::StateAnswer>(reply.body);
  EXPECT_EQ(answer.query_id, 9u);
  EXPECT_EQ(answer.status, wsc::AnswerStatus::kSuccess);
}

TEST_F(SessionTest, AcceptedPacketsWithoutAckGetNoAnswer) {
  session_.OnMessage(Channels(10, wsc::Protocol::kSacn));
  session_.OnResult(sacn_.submitted.at(0).packet.header, absl::OkStatus());
  EXPECT_TRUE(transport_.sent.empty());
}

TEST_F(SessionTest, RepliesStateErrorWhenTheAdapterRejectsThePacket) {
  for (const uint16_t ack : {uint16_t{0}, wsc::flags::kAck}) {
    transport_.sent.clear();
    sacn_.submitted.clear();
    session_.OnMessage(Channels(12, wsc::Protocol::kSacn, ack));
    session_.OnResult(
        sacn_.submitted.at(0).packet.header,
        wsc::Error(wsc::ErrorCode::kValidationFailed, "priority 250"));
    ASSERT_EQ(transport_.sent.size(), 1u) << "ack " << ack;
    const wsc::Packet reply = Sent(0);
    EXPECT_EQ(reply.header.id, 12u);
    const auto& error = std::get<wsc::StateError>(reply.body);
    EXPECT_EQ(error.code, wsc::ErrorCode::kValidationFailed);
    EXPECT_EQ(error.message, "priority 250");
  }
}

TEST_F(SessionTest, IgnoresResultsAfterClosing) {
  session_.OnMessage(Channels(13, wsc::Protocol::kSacn, wsc::flags::kAck));
  clock_.Advance(absl::Seconds(6));
  ASSERT_TRUE(session_.CloseIfTimedOut());
  session_.OnResult(sacn_.submitted.at(0).packet.header, absl::OkStatus());
  EXPECT_TRUE(transport_.sent.empty());
}

TEST_F(SessionTest, RepliesStateErrorWithTheOffendingId) {
  session_.OnMessage(Channels(11, wsc::Protocol::kArtNet, wsc::flags::kAck));
  ASSERT_EQ(transport_.sent.size(), 1u);  // STATE_ERROR only, no ACK answer
  const wsc::Packet reply = Sent(0);
  EXPECT_EQ(reply.header.id, 11u);
  EXPECT_EQ(std::get<wsc::StateError>(reply.body).code,
            wsc::ErrorCode::kNoRoute);
}

TEST_F(SessionTest, RepliesStateErrorForUndecodablePackets) {
  std::vector<uint8_t> bytes = Keepalive(21);
  bytes[4] = 2;  // major version 2
  session_.OnMessage(bytes);
  ASSERT_EQ(transport_.sent.size(), 1u);
  const wsc::Packet reply = Sent(0);
  EXPECT_EQ(reply.header.id, 21u);
  EXPECT_EQ(std::get<wsc::StateError>(reply.body).code,
            wsc::ErrorCode::kProtocolVersion);
}

TEST_F(SessionTest, DropsGarbageSilently) {
  const std::array<uint8_t, 5> garbage = {'h', 'e', 'l', 'l', 'o'};
  session_.OnMessage(garbage);
  EXPECT_TRUE(transport_.sent.empty());
}

TEST_F(SessionTest, ClosesAfterFiveSecondsOfSilence) {
  clock_.Advance(absl::Seconds(4));
  session_.OnMessage(Keepalive(1));
  clock_.Advance(absl::Seconds(5));
  EXPECT_FALSE(session_.CloseIfTimedOut());
  clock_.Advance(absl::Milliseconds(1));
  EXPECT_TRUE(session_.CloseIfTimedOut());
  EXPECT_TRUE(transport_.closed);
  EXPECT_FALSE(session_.CloseIfTimedOut());  // only once
  session_.OnMessage(Keepalive(2));          // ignored after close
  EXPECT_EQ(transport_.sent.size(), 1u);
}

TEST_F(SessionTest, ResetTimeoutCountsTheFiveSecondsFromNow) {
  clock_.Advance(absl::Seconds(8));  // e.g. a slow WebRTC handshake
  session_.ResetTimeout();
  clock_.Advance(absl::Seconds(5));
  EXPECT_FALSE(session_.CloseIfTimedOut());
  clock_.Advance(absl::Milliseconds(1));
  EXPECT_TRUE(session_.CloseIfTimedOut());
}

}  // namespace
}  // namespace wsc_server
