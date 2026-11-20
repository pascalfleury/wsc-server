#include "libwsc/validate.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/types.h"

namespace wsc {
namespace {

using ::testing::Optional;

constexpr std::array<uint8_t, 6> kIpv4 = {10, 0, 0, 1, 0x1F, 0x40};
constexpr std::array<uint8_t, 1> kSerialPort = {0};

Packet Forward(Body body, Protocol protocol, Iface iface) {
  const bool serial = iface == Iface::kSerial;
  Packet packet;
  packet.header.flags = flags::kTransport | flags::kGateway;
  packet.header.id = 1;
  packet.body = body;
  packet.transport =
      TransportDescriptor{protocol,
                          iface,
                          serial ? AddrType::kSerial : AddrType::kIpv4,
                          serial ? std::span<const uint8_t>(kSerialPort)
                                 : std::span<const uint8_t>(kIpv4),
                          {}};
  return packet;
}

std::optional<ErrorCode> ValidateError(const Packet& packet) {
  return GetErrorCode(Validate(packet));
}

TEST(ValidateTest, AcceptsPermittedCombinations) {
  EXPECT_TRUE(
      Validate(Forward(StreamChannels{}, Protocol::kArtNet, Iface::kUdp)).ok());
  EXPECT_TRUE(
      Validate(Forward(StreamChannels{}, Protocol::kDmx512, Iface::kSerial))
          .ok());
  EXPECT_TRUE(Validate(Forward(TunnelRaw{}, Protocol::kRaw, Iface::kTcp)).ok());
  EXPECT_TRUE(
      Validate(Forward(StateQuery{}, Protocol::kMqtt, Iface::kTcp)).ok());
  Packet internal;
  internal.header.id = 1;
  internal.body = ControlCue{};
  EXPECT_TRUE(Validate(internal).ok());
}

TEST(ValidateTest, RejectsInterfaceNotInMatrix) {
  EXPECT_THAT(
      ValidateError(Forward(StreamChannels{}, Protocol::kDmx512, Iface::kUdp)),
      Optional(ErrorCode::kIncompatibleIface));
  // Interface 0, as the upstream SDK's raw() helper writes it.
  EXPECT_THAT(ValidateError(Forward(TunnelRaw{}, Protocol::kRaw, Iface{0})),
              Optional(ErrorCode::kIncompatibleIface));
}

TEST(ValidateTest, RejectsTypeNotPermittedForProtocolOnlyWithGateway) {
  Packet packet = Forward(StreamChannels{}, Protocol::kMidi, Iface::kUdp);
  EXPECT_THAT(ValidateError(packet),
              Optional(ErrorCode::kIncompatibleProtocol));
  packet.header.flags = flags::kTransport;
  EXPECT_TRUE(Validate(packet).ok());
}

TEST(ValidateTest, TunnelNeedsRawProtocol) {
  EXPECT_THAT(ValidateError(Forward(TunnelRaw{}, Protocol::kOsc, Iface::kUdp)),
              Optional(ErrorCode::kInvalidTunnel));
  Packet packet;
  packet.header.id = 1;
  packet.body = TunnelRaw{};
  EXPECT_THAT(ValidateError(packet), Optional(ErrorCode::kInvalidTunnel));
}

}  // namespace
}  // namespace wsc
