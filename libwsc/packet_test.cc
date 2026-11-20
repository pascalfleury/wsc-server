#include "libwsc/packet.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"

namespace wsc {
namespace {

using ::testing::ElementsAre;
using ::testing::Optional;

// The annotated example of spec 02: STREAM_CHANNELS over Art-Net. The spec
// gives transport_len 9; its descriptor is 10 bytes (WSC_UPSTREAM.org).
std::vector<uint8_t> SpecExample() {
  return {0x57, 0x53, 0x43, 0x21, 0x01, 0x00, 0x00, 0x10, 0x00, 0xC0, 0x00,
          0x00, 0x00, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x0A,
          // payload
          0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0xFF, 0x80, 0x40, 0x00,
          // transport descriptor
          0x02, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x64, 0x19, 0x36, 0x00};
}

std::optional<ErrorCode> DecodeError(const std::vector<uint8_t>& bytes) {
  const absl::StatusOr<Packet> packet = Decode(bytes);
  EXPECT_FALSE(packet.ok());
  return GetErrorCode(packet.status());
}

std::vector<uint8_t> EncodeOrDie(const Packet& packet) {
  std::array<uint8_t, 1024> buffer{};
  const absl::StatusOr<size_t> size = Encode(packet, buffer);
  EXPECT_TRUE(size.ok()) << size.status();
  return {buffer.begin(), buffer.begin() + size.value_or(0)};
}

TEST(PacketTest, DecodesSpecExample) {
  const std::vector<uint8_t> bytes = SpecExample();
  const absl::StatusOr<Packet> packet = Decode(bytes);
  ASSERT_TRUE(packet.ok()) << packet.status();
  EXPECT_THAT(packet->header.version, ElementsAre(1, 0, 0));
  EXPECT_EQ(packet->header.flags, flags::kTransport | flags::kGateway);
  EXPECT_EQ(packet->header.id, 1u);
  const auto& body = std::get<StreamChannels>(packet->body);
  EXPECT_EQ(body.universe, 0);
  EXPECT_EQ(body.start_channel, 1);
  EXPECT_THAT(body.values, ElementsAre(255, 128, 64, 0));
  const std::optional<TransportDescriptor>& transport = packet->transport;
  if (!transport) FAIL() << "no transport descriptor";
  const TransportDescriptor& td = *transport;
  EXPECT_EQ(td.protocol, Protocol::kArtNet);
  EXPECT_EQ(td.iface, Iface::kUdp);
  EXPECT_EQ(td.addr_type, AddrType::kIpv4);
  EXPECT_THAT(td.addr_data, ElementsAre(192, 168, 1, 100, 0x19, 0x36));
  EXPECT_TRUE(td.params.empty());

  EXPECT_EQ(EncodeOrDie(*packet), bytes);
}

TEST(PacketTest, DropsShortBuffersAndBadMagic) {
  std::vector<uint8_t> bytes = SpecExample();
  EXPECT_TRUE(IsDrop(Decode(std::span(bytes).first(18)).status()));
  bytes[3] = '?';
  EXPECT_TRUE(IsDrop(Decode(bytes).status()));
}

TEST(PacketTest, MapsHeaderErrorsToWscCodes) {
  std::vector<uint8_t> bytes = SpecExample();
  bytes[4] = 2;  // major version
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kProtocolVersion));

  bytes = SpecExample();
  bytes[7] = 0x30;  // type 0x3000
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kInvalidMessageType));

  bytes = SpecExample();
  bytes.push_back(0);  // one byte more than the length fields say
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMalformedPacket));

  bytes = SpecExample();
  bytes[14] = 0;  // id 0
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMalformedPacket));
}

TEST(PacketTest, EnforcesFlagInvariants) {
  std::vector<uint8_t> bytes = SpecExample();
  bytes[9] = 0x40;  // GW without TR (TD still present)
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMissingTransportFlag));

  bytes[9] = 0x00;  // TR = 0 with a TD
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMissingTransportFlag));

  bytes = SpecExample();
  bytes[9] = 0xC8;  // TS
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kValidationFailed));

  // TR = 1 without a TD: drop the TD bytes and zero transport_len.
  bytes = SpecExample();
  bytes.resize(bytes.size() - 10);
  bytes[18] = 0;
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMissingTransport));
}

TEST(PacketTest, RejectsMalformedPayloadAndTransport) {
  std::vector<uint8_t> bytes = SpecExample();
  bytes[24] = 5;  // count 5, but 4 values follow
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMalformedPacket));

  bytes = SpecExample();
  bytes[31] = 0x09;  // addr_type 9 is undefined
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMissingTransport));
}

TEST(PacketTest, EveryTruncationFails) {
  const std::vector<uint8_t> bytes = SpecExample();
  for (size_t n = 0; n < bytes.size(); ++n) {
    EXPECT_FALSE(Decode(std::span(bytes).first(n)).ok()) << n;
  }
}

TEST(PacketTest, RejectsUnknownValueType) {
  // CONTROL_PARAM, address [01 00], value type 0x0D.
  const std::vector<uint8_t> bytes = {
      0x57, 0x53, 0x43, 0x21, 1, 1, 0, 0x20, 0x01, 0,    0,    0,   0,
      0,    1,    0,    6,    0, 0, 0, 2,    0x01, 0x00, 0x0D, 0x00};
  EXPECT_THAT(DecodeError(bytes), Optional(ErrorCode::kMalformedPacket));
}

TEST(PacketTest, RoundTripsI64AndHostname) {
  // I64 has no golden packet: the upstream SDK does not implement it.
  const std::array<uint8_t, 3> address = {0x01, 0xD4, 0x00};
  const std::array<uint8_t, 8> value = {0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFB};
  const std::array<uint8_t, 7> host = {4, 'h', 'o', 's', 't', 0x1F, 0x40};
  Packet packet;
  packet.header.flags = flags::kTransport | flags::kGateway;
  packet.header.id = 7;
  packet.body = ControlParam{address, ValueType::kI64, value};
  packet.transport = TransportDescriptor{
      Protocol::kOsc, Iface::kTcp, AddrType::kHostname, host, {}};

  const std::vector<uint8_t> bytes = EncodeOrDie(packet);
  const absl::StatusOr<Packet> decoded = Decode(bytes);
  ASSERT_TRUE(decoded.ok()) << decoded.status();
  EXPECT_THAT(decoded->header.version, ElementsAre(1, 1, 0));
  const auto& param = std::get<ControlParam>(decoded->body);
  EXPECT_EQ(param.type, ValueType::kI64);
  EXPECT_THAT(param.value, testing::ElementsAreArray(value));
  const std::optional<TransportDescriptor>& transport = decoded->transport;
  if (!transport) FAIL() << "no transport descriptor";
  EXPECT_THAT(transport->addr_data, testing::ElementsAreArray(host));
  EXPECT_EQ(EncodeOrDie(*decoded), bytes);
}

TEST(PacketTest, EncodeRejectsInvalidPackets) {
  std::array<uint8_t, 256> buffer{};
  const std::array<uint8_t, 2> address = {0x01, 0x00};
  const std::array<uint8_t, 2> two_bytes = {1, 2};
  Packet packet;
  packet.header.id = 1;
  packet.body = ControlParam{address, ValueType::kU32, two_bytes};
  EXPECT_EQ(Encode(packet, buffer).status().code(),
            absl::StatusCode::kInvalidArgument);

  packet.body = ControlCue{std::span(address).first(1), CueAction::kStart};
  EXPECT_FALSE(Encode(packet, buffer).ok());  // no terminator

  packet.body = ControlCue{address, CueAction::kStart};
  packet.header.id = 0;
  EXPECT_FALSE(Encode(packet, buffer).ok());

  packet.header.id = 1;
  packet.header.flags = flags::kGateway;
  EXPECT_THAT(GetErrorCode(Encode(packet, buffer).status()),
              Optional(ErrorCode::kMissingTransportFlag));

  packet.header.flags = 0;
  EXPECT_EQ(Encode(packet, std::span(buffer).first(20)).status().code(),
            absl::StatusCode::kResourceExhausted);

  const std::array<uint8_t, 4> bad_ipv4 = {10, 0, 0, 1};  // port missing
  packet.header.flags = flags::kTransport;
  packet.transport = TransportDescriptor{
      Protocol::kOsc, Iface::kUdp, AddrType::kIpv4, bad_ipv4, {}};
  EXPECT_EQ(Encode(packet, buffer).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace wsc
