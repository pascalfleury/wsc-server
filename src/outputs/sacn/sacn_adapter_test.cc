#include "src/outputs/sacn/sacn_adapter.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"
#include "src/core/uuid.h"
#include "src/memory/frame.h"
#include "src/platform/asio.h"
#include "src/routing/message.h"
#include "testing/fake_clock.h"
#include "testing/fake_datagram_sender.h"

namespace wsc_server {
namespace {

constexpr std::array<uint8_t, 6> kUnicast = {10, 0, 0, 7, 0x15, 0xC0};

Message ChannelsMessage(std::span<const uint8_t> values) {
  Message message;
  message.frame = std::make_shared<Frame>(1);
  message.packet.header.id = 1;
  message.packet.header.flags = wsc::flags::kTransport | wsc::flags::kGateway;
  message.packet.body =
      wsc::StreamChannels{.universe = 1, .start_channel = 1, .values = values};
  message.packet.transport =
      wsc::TransportDescriptor{.protocol = wsc::Protocol::kSacn,
                               .iface = wsc::Iface::kUdp,
                               .addr_type = wsc::AddrType::kIpv4,
                               .addr_data = kUnicast,
                               .params = {}};
  return message;
}

TEST(SacnAdapterTest, SendsOnTheStrandAndCoalescesBurstUpdates) {
  asio::io_context io;
  FakeClock clock;
  FakeDatagramSender sender;
  const Uuid server_id =
      Uuid::Parse("35797cde-3def-48f8-bb5a-19713477591e").value();
  SacnAdapter adapter(io.get_executor(), clock,
                      {.cid = SacnCid(server_id, "main")}, sender);
  EXPECT_EQ(adapter.protocol(), wsc::Protocol::kSacn);

  const std::vector<uint8_t> a(4, 1);
  const std::vector<uint8_t> b(4, 2);
  adapter.Submit(ChannelsMessage(a));
  EXPECT_TRUE(sender.sent.empty());  // posted, not run inline
  io.poll();
  ASSERT_EQ(sender.sent.size(), 1u);

  // Same instant on the clock: within the 44 Hz interval, so b waits for the
  // timer instead of going out at once.
  adapter.Submit(ChannelsMessage(b));
  io.poll();
  EXPECT_EQ(sender.sent.size(), 1u);
}

TEST(SacnAdapterTest, RejectedPacketsSendNothing) {
  asio::io_context io;
  FakeClock clock;
  FakeDatagramSender sender;
  const Uuid cid = Uuid::Parse("35797cde-3def-48f8-bb5a-19713477591e").value();
  SacnAdapter adapter(io.get_executor(), clock, {.cid = cid}, sender);
  const std::vector<uint8_t> values(4, 1);
  Message message = ChannelsMessage(values);
  std::get<wsc::StreamChannels>(message.packet.body).universe = 0;
  adapter.Submit(message);
  io.poll();
  EXPECT_TRUE(sender.sent.empty());
}

}  // namespace
}  // namespace wsc_server
