#include "src/routing/router.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/types.h"
#include "src/memory/frame.h"
#include "src/routing/message.h"
#include "src/routing/output_adapter.h"

namespace wsc_server {
namespace {

using ::testing::Optional;

class FakeAdapter final : public OutputAdapter {
 public:
  FakeAdapter(wsc::Protocol protocol, std::vector<wsc::Iface> ifaces)
      : protocol_(protocol), ifaces_(std::move(ifaces)) {}

  [[nodiscard]] wsc::Protocol protocol() const override { return protocol_; }
  [[nodiscard]] std::span<const wsc::Iface> ifaces() const override {
    return ifaces_;
  }
  [[nodiscard]] DeliveryModel model() const override {
    return DeliveryModel::kCyclic;
  }
  [[nodiscard]] bool enabled() const override { return enabled_; }
  void Submit(Message message) override {
    submitted.push_back(std::move(message));
  }

  bool enabled_ = true;
  std::vector<Message> submitted;

 private:
  wsc::Protocol protocol_;
  std::vector<wsc::Iface> ifaces_;
};

constexpr std::array<uint8_t, 6> kIpv4 = {239, 255, 0, 1, 0x15, 0xC0};

Message Forward(wsc::Protocol protocol, wsc::Iface iface) {
  Message message;
  message.frame = std::make_shared<Frame>(16);
  message.packet.header.flags = wsc::flags::kTransport | wsc::flags::kGateway;
  message.packet.header.id = 1;
  message.packet.body = wsc::StreamChannels{};
  message.packet.transport =
      wsc::TransportDescriptor{.protocol = protocol,
                               .iface = iface,
                               .addr_type = wsc::AddrType::kIpv4,
                               .addr_data = kIpv4,
                               .params = {}};
  return message;
}

std::optional<wsc::ErrorCode> RouteError(const Router& router, Message m) {
  return wsc::GetErrorCode(router.Route(std::move(m)));
}

TEST(RouterTest, SubmitsToTheAdapterOfTheProtocol) {
  FakeAdapter sacn(wsc::Protocol::kSacn, {wsc::Iface::kUdp});
  FakeAdapter artnet(wsc::Protocol::kArtNet, {wsc::Iface::kUdp});
  Router router;
  ASSERT_TRUE(router.Register(sacn).ok());
  ASSERT_TRUE(router.Register(artnet).ok());

  const Message message = Forward(wsc::Protocol::kSacn, wsc::Iface::kUdp);
  ASSERT_TRUE(router.Route(message).ok());
  ASSERT_EQ(sacn.submitted.size(), 1u);
  EXPECT_EQ(sacn.submitted[0].frame, message.frame);  // shared, not copied
  EXPECT_TRUE(artnet.submitted.empty());
}

TEST(RouterTest, RoutesByProtocolAndInterface) {
  // DMX512 over USB and over serial are different drivers.
  FakeAdapter usb(wsc::Protocol::kDmx512, {wsc::Iface::kUsb});
  FakeAdapter serial(wsc::Protocol::kDmx512, {wsc::Iface::kSerial});
  Router router;
  ASSERT_TRUE(router.Register(usb).ok());
  ASSERT_TRUE(router.Register(serial).ok());
  Message message = Forward(wsc::Protocol::kDmx512, wsc::Iface::kSerial);
  std::optional<wsc::TransportDescriptor>& td = message.packet.transport;
  if (!td) FAIL() << "no transport descriptor";
  td->addr_type = wsc::AddrType::kSerial;
  td->addr_data = std::span(kIpv4).first(1);
  ASSERT_TRUE(router.Route(message).ok());
  EXPECT_EQ(serial.submitted.size(), 1u);
  EXPECT_TRUE(usb.submitted.empty());
}

TEST(RouterTest, OneAdapterCanServeSeveralInterfaces) {
  FakeAdapter osc(wsc::Protocol::kOsc, {wsc::Iface::kUdp, wsc::Iface::kTcp});
  Router router;
  ASSERT_TRUE(router.Register(osc).ok());
  Message message = Forward(wsc::Protocol::kOsc, wsc::Iface::kTcp);
  message.packet.body = wsc::ControlCue{};
  static constexpr std::array<uint8_t, 2> kAddress = {0x01, 0x00};
  std::get<wsc::ControlCue>(message.packet.body).address = kAddress;
  ASSERT_TRUE(router.Route(message).ok());
  EXPECT_EQ(osc.submitted.size(), 1u);
}

TEST(RouterTest, RefusesASecondAdapterForAProtocolAndInterface) {
  FakeAdapter a(wsc::Protocol::kOsc, {wsc::Iface::kUdp});
  FakeAdapter b(wsc::Protocol::kOsc, {wsc::Iface::kTcp, wsc::Iface::kUdp});
  Router router;
  ASSERT_TRUE(router.Register(a).ok());
  EXPECT_EQ(router.Register(b).code(), absl::StatusCode::kAlreadyExists);
  // Nothing of b was registered: OSC over TCP still has no adapter.
  EXPECT_THAT(
      RouteError(router, Forward(wsc::Protocol::kOsc, wsc::Iface::kTcp)),
      Optional(wsc::ErrorCode::kNoRoute));
}

TEST(RouterTest, AnswersNoRouteWithoutAdapterOrGateway) {
  FakeAdapter sacn(wsc::Protocol::kSacn, {wsc::Iface::kUdp});
  Router router;
  ASSERT_TRUE(router.Register(sacn).ok());
  EXPECT_THAT(
      RouteError(router, Forward(wsc::Protocol::kArtNet, wsc::Iface::kUdp)),
      Optional(wsc::ErrorCode::kNoRoute));

  Message internal = Forward(wsc::Protocol::kSacn, wsc::Iface::kUdp);
  internal.packet.header.flags = wsc::flags::kTransport;  // GW = 0
  EXPECT_THAT(RouteError(router, internal), Optional(wsc::ErrorCode::kNoRoute));
  EXPECT_TRUE(sacn.submitted.empty());
}

TEST(RouterTest, AnswersConfigErrorForADisabledAdapter) {
  FakeAdapter sacn(wsc::Protocol::kSacn, {wsc::Iface::kUdp});
  sacn.enabled_ = false;
  Router router;
  ASSERT_TRUE(router.Register(sacn).ok());
  EXPECT_THAT(
      RouteError(router, Forward(wsc::Protocol::kSacn, wsc::Iface::kUdp)),
      Optional(wsc::ErrorCode::kConfigError));
  EXPECT_TRUE(sacn.submitted.empty());
}

TEST(RouterTest, ValidatesBeforeRouting) {
  FakeAdapter sacn(wsc::Protocol::kSacn, {wsc::Iface::kUdp});
  Router router;
  ASSERT_TRUE(router.Register(sacn).ok());
  EXPECT_THAT(
      RouteError(router, Forward(wsc::Protocol::kSacn, wsc::Iface::kSerial)),
      Optional(wsc::ErrorCode::kIncompatibleIface));
  EXPECT_TRUE(sacn.submitted.empty());
}

}  // namespace
}  // namespace wsc_server
