#include "src/outputs/sacn/sacn_streams.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"
#include "src/core/uuid.h"
#include "src/outputs/datagram_sender.h"
#include "testing/fake_datagram_sender.h"

namespace wsc_server {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Optional;

// Field accessors for a recorded E1.31 data packet (offsets: e131_test.cc).
struct E131View {
  const std::vector<uint8_t>& bytes;

  [[nodiscard]] uint8_t priority() const { return bytes[108]; }
  [[nodiscard]] uint8_t sequence() const { return bytes[111]; }
  [[nodiscard]] uint16_t universe() const {
    return static_cast<uint16_t>((bytes[113] << 8) | bytes[114]);
  }
  [[nodiscard]] std::span<const uint8_t> slots() const {
    return std::span(bytes).subspan(126);
  }
};

E131View View(const FakeDatagramSender::Datagram& d) { return {d.bytes}; }

const Uuid kServerId =
    Uuid::Parse("35797cde-3def-48f8-bb5a-19713477591e").value();
const absl::Time kStart = absl::UnixEpoch();
constexpr std::array<uint8_t, 6> kUnicast = {10, 0, 0, 7, 0x15, 0xC0};
constexpr std::array<uint8_t, 1> kPriority120 = {120};

// A STREAM_CHANNELS packet; the views point into `values`.
wsc::Packet Channels(uint16_t universe, uint16_t start,
                     std::span<const uint8_t> values, bool multicast = false,
                     bool priority = false) {
  wsc::Packet packet;
  packet.header.id = 1;
  packet.header.flags = wsc::flags::kTransport | wsc::flags::kGateway |
                        (multicast ? wsc::flags::kMulticast : 0);
  packet.body = wsc::StreamChannels{
      .universe = universe, .start_channel = start, .values = values};
  wsc::TransportDescriptor td{.protocol = wsc::Protocol::kSacn,
                              .iface = wsc::Iface::kUdp,
                              .addr_type = wsc::AddrType::kIpv4,
                              .addr_data = kUnicast,
                              .params = {}};
  if (priority) td.params.push_back({wsc::ParamKey::kPriority, kPriority120});
  packet.transport = td;
  return packet;
}

class SacnStreamsTest : public testing::Test {
 protected:
  FakeDatagramSender sender_;
  SacnStreams streams_{{.cid = SacnCid(kServerId, "main")}, sender_};
};

TEST_F(SacnStreamsTest, SendsTheFirstUpdateAtOnceAsAFullUniverse) {
  const std::vector<uint8_t> values(75, 9);
  ASSERT_TRUE(streams_.Apply(Channels(1, 50, values)).ok());
  streams_.Tick(kStart);
  ASSERT_EQ(sender_.sent.size(), 1u);
  const E131View d = View(sender_.sent[0]);
  EXPECT_EQ(sender_.sent[0].to, (Ipv4Endpoint{{10, 0, 0, 7}, 5568}));
  EXPECT_EQ(d.universe(), 1);
  EXPECT_EQ(d.priority(), 100);
  ASSERT_EQ(d.slots().size(), 512u);
  EXPECT_THAT(d.slots().first(49), Each(0));
  EXPECT_THAT(d.slots().subspan(49, 75), Each(9));
}

TEST_F(SacnStreamsTest, MulticastFlagSendsToTheUniverseGroup) {
  const std::vector<uint8_t> values(4, 1);
  ASSERT_TRUE(streams_.Apply(Channels(0x0102, 1, values, true)).ok());
  streams_.Tick(kStart);
  ASSERT_EQ(sender_.sent.size(), 1u);
  EXPECT_EQ(sender_.sent[0].to, (Ipv4Endpoint{{239, 255, 1, 2}, 5568}));
}

TEST_F(SacnStreamsTest, UsesThePriorityParam) {
  const std::vector<uint8_t> values(4, 1);
  ASSERT_TRUE(streams_.Apply(Channels(1, 1, values, false, true)).ok());
  streams_.Tick(kStart);
  ASSERT_EQ(sender_.sent.size(), 1u);
  EXPECT_EQ(View(sender_.sent[0]).priority(), 120);
}

TEST_F(SacnStreamsTest, CoalescesAt44HzAndRepeatsEverySecond) {
  const std::vector<uint8_t> a(4, 1);
  const std::vector<uint8_t> b(4, 2);
  const std::vector<uint8_t> c(4, 3);
  ASSERT_TRUE(streams_.Apply(Channels(1, 1, a)).ok());
  const absl::Time next = streams_.Tick(kStart);
  EXPECT_EQ(next, kStart + absl::Seconds(1));  // keepalive
  ASSERT_TRUE(streams_.Apply(Channels(1, 1, b)).ok());
  ASSERT_TRUE(streams_.Apply(Channels(1, 1, c)).ok());
  EXPECT_EQ(streams_.Tick(kStart + absl::Milliseconds(10)),
            kStart + absl::Seconds(1) / 44);
  streams_.Tick(kStart + absl::Seconds(1) / 44);
  streams_.Tick(kStart + absl::Seconds(1) / 44 + absl::Seconds(1));
  ASSERT_EQ(sender_.sent.size(), 3u);  // a, then c (b coalesced), then c again
  EXPECT_EQ(View(sender_.sent[0]).slots()[0], 1);
  EXPECT_EQ(View(sender_.sent[1]).slots()[0], 3);
  EXPECT_EQ(View(sender_.sent[2]).slots()[0], 3);
  EXPECT_THAT((std::vector<uint8_t>{View(sender_.sent[0]).sequence(),
                                    View(sender_.sent[1]).sequence(),
                                    View(sender_.sent[2]).sequence()}),
              ElementsAre(0, 1, 2));
}

TEST_F(SacnStreamsTest, KeepsUniversesAndDestinationsApart) {
  const std::vector<uint8_t> values(4, 1);
  ASSERT_TRUE(streams_.Apply(Channels(1, 1, values)).ok());
  ASSERT_TRUE(streams_.Apply(Channels(2, 1, values)).ok());
  ASSERT_TRUE(streams_.Apply(Channels(1, 1, values, true)).ok());
  EXPECT_EQ(streams_.stream_count(), 3u);
  streams_.Tick(kStart);
  EXPECT_EQ(sender_.sent.size(), 3u);
}

TEST_F(SacnStreamsTest, PartialDmxSendsUpToTheHighestChannel) {
  SacnStreams partial({.cid = SacnCid(kServerId, "main"), .partial_dmx = true},
                      sender_);
  const std::vector<uint8_t> values(75, 9);
  ASSERT_TRUE(partial.Apply(Channels(1, 50, values)).ok());
  partial.Tick(kStart);
  ASSERT_EQ(sender_.sent.size(), 1u);
  EXPECT_EQ(View(sender_.sent[0]).slots().size(), 124u);
}

TEST_F(SacnStreamsTest, RejectsBadUniversesSlicesAndPriorities) {
  const std::vector<uint8_t> values(4, 1);
  const std::vector<uint8_t> too_long(600, 1);
  EXPECT_THAT(wsc::GetErrorCode(streams_.Apply(Channels(0, 1, values))),
              Optional(wsc::ErrorCode::kValidationFailed));
  EXPECT_THAT(wsc::GetErrorCode(streams_.Apply(Channels(1, 1, too_long))),
              Optional(wsc::ErrorCode::kValidationFailed));
  EXPECT_EQ(streams_.stream_count(), 0u);

  constexpr std::array<uint8_t, 1> kPriority201 = {201};
  wsc::Packet packet = Channels(1, 1, values);
  std::optional<wsc::TransportDescriptor>& td = packet.transport;
  if (!td) FAIL() << "no transport descriptor";
  td->params.push_back({wsc::ParamKey::kPriority, kPriority201});
  EXPECT_THAT(wsc::GetErrorCode(streams_.Apply(packet)),
              Optional(wsc::ErrorCode::kValidationFailed));
}

TEST(SacnCidTest, IsStablePerServerAndOutput) {
  EXPECT_EQ(SacnCid(kServerId, "main"), SacnCid(kServerId, "main"));
  EXPECT_NE(SacnCid(kServerId, "main"), SacnCid(kServerId, "backup"));
  EXPECT_EQ(SacnCid(kServerId, "main"),
            Uuid::FromName(kServerId, "sacn-output/main"));
}

}  // namespace
}  // namespace wsc_server
