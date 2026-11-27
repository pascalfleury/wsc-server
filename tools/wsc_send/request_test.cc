#include "tools/wsc_send/request.h"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"

namespace wsc_server::wsc_send {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::SizeIs;

std::vector<uint8_t> EncodeOrDie(const wsc::Packet& packet) {
  std::vector<uint8_t> out(256);
  out.resize(wsc::Encode(packet, out).value());
  return out;
}

TEST(ParseValuesTest, ReadsListsAndRepeats) {
  EXPECT_THAT(*ParseValues("1,2,255"), ElementsAre(1, 2, 255));
  EXPECT_THAT(*ParseValues("7*3,0"), ElementsAre(7, 7, 7, 0));
  EXPECT_THAT(*ParseValues("255*512"), SizeIs(512));
}

TEST(ParseValuesTest, RejectsBadValues) {
  for (const char* bad : {"", "256", "1,,2", "x", "1*2*3", "1*513", "-1"}) {
    EXPECT_FALSE(ParseValues(bad).ok()) << bad;
  }
}

TEST(ParseIpv4EndpointTest, ReadsAddressAndOptionalPort) {
  EXPECT_THAT(*ParseIpv4Endpoint("10.0.0.7", 5568),
              ElementsAre(10, 0, 0, 7, 0x15, 0xC0));
  EXPECT_THAT(*ParseIpv4Endpoint("127.0.0.1:6000", 5568),
              ElementsAre(127, 0, 0, 1, 0x17, 0x70));
}

TEST(ParseIpv4EndpointTest, RejectsMalformedEndpoints) {
  for (const char* bad :
       {"", "10.0.0", "10.0.0.256", "10.0.0.1:", "10.0.0.1:70000", "a:b:c"}) {
    EXPECT_FALSE(ParseIpv4Endpoint(bad, 5568).ok()) << bad;
  }
}

TEST(EncodeChannelsTest, UnicastPacketDecodesForTheSacnAdapter) {
  const absl::StatusOr<std::vector<uint8_t>> bytes =
      EncodeChannels({.universe = 3,
                      .start_channel = 10,
                      .values = {1, 2},
                      .unicast = Ipv4AddrData{10, 0, 0, 7, 0x15, 0xC0},
                      .priority = 150},
                     42);
  ASSERT_TRUE(bytes.ok()) << bytes.status();
  const wsc::Packet packet = wsc::Decode(*bytes).value();

  EXPECT_EQ(packet.header.id, 42u);
  EXPECT_EQ(packet.header.flags,
            wsc::flags::kTransport | wsc::flags::kGateway | wsc::flags::kAck);
  const auto& body = std::get<wsc::StreamChannels>(packet.body);
  EXPECT_EQ(body.universe, 3);
  EXPECT_EQ(body.start_channel, 10);
  EXPECT_THAT(body.values, ElementsAre(1, 2));
  if (!packet.transport) FAIL() << "no transport descriptor";
  const wsc::TransportDescriptor& td = *packet.transport;
  EXPECT_EQ(td.protocol, wsc::Protocol::kSacn);
  EXPECT_EQ(td.iface, wsc::Iface::kUdp);
  EXPECT_EQ(td.addr_type, wsc::AddrType::kIpv4);
  EXPECT_THAT(td.addr_data, ElementsAre(10, 0, 0, 7, 0x15, 0xC0));
  ASSERT_THAT(td.params, SizeIs(1));
  EXPECT_EQ(td.params[0].key, wsc::ParamKey::kPriority);
  EXPECT_THAT(td.params[0].value, ElementsAre(150));
}

TEST(EncodeChannelsTest, WithoutUnicastSetsTheMulticastFlag) {
  const wsc::Packet packet =
      wsc::Decode(EncodeChannels({.universe = 1,
                                  .start_channel = 1,
                                  .values = {9},
                                  .unicast = std::nullopt,
                                  .priority = 100},
                                 1)
                      .value())
          .value();
  EXPECT_NE(packet.header.flags & wsc::flags::kMulticast, 0);
  if (!packet.transport) FAIL() << "no transport descriptor";
  EXPECT_EQ(packet.transport->addr_type, wsc::AddrType::kNone);
}

TEST(EncodeKeepaliveTest, IsAKeepaliveQuery) {
  const std::vector<uint8_t> bytes = EncodeKeepalive(5);
  const wsc::Packet packet = wsc::Decode(bytes).value();
  EXPECT_EQ(packet.header.id, 5u);
  EXPECT_EQ(std::get<wsc::StateQuery>(packet.body).type,
            wsc::QueryType::kKeepalive);
}

TEST(CheckReplyTest, AcceptsOnlyASuccessAnswerToOurId) {
  auto answer = [](uint32_t query_id, wsc::AnswerStatus status) {
    return EncodeOrDie({.header = {.flags = 0, .id = 100},
                        .transport = std::nullopt,
                        .body = wsc::StateAnswer{.query_id = query_id,
                                                 .status = status,
                                                 .format = {},
                                                 .data = {}}});
  };
  EXPECT_TRUE(CheckReply(answer(7, wsc::AnswerStatus::kSuccess), 7).ok());
  EXPECT_FALSE(CheckReply(answer(8, wsc::AnswerStatus::kSuccess), 7).ok());
  EXPECT_FALSE(CheckReply(answer(7, wsc::AnswerStatus::kBusy), 7).ok());
}

TEST(CheckReplyTest, ReportsStateErrorCodeAndMessage) {
  const std::vector<uint8_t> error =
      EncodeOrDie({.header = {.flags = 0, .id = 7},
                   .transport = std::nullopt,
                   .body = wsc::StateError{.code = wsc::ErrorCode::kNoRoute,
                                           .message = "no adapter",
                                           .context = "{}"}});
  const absl::Status status = CheckReply(error, 7);
  EXPECT_THAT(status.message(), HasSubstr("0x0009"));
  EXPECT_THAT(status.message(), HasSubstr("no adapter"));
}

TEST(CheckReplyTest, RejectsGarbage) {
  const std::vector<uint8_t> garbage(30, 0xAB);
  EXPECT_FALSE(CheckReply(garbage, 1).ok());
}

}  // namespace
}  // namespace wsc_server::wsc_send
