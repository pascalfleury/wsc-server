// Real sockets over loopback (CODING.org: only in tests/).
#include "src/outputs/udp_sender.h"

#include <array>
#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/outputs/datagram_sender.h"
#include "src/platform/asio.h"

namespace wsc_server {
namespace {

using ::testing::ElementsAre;

TEST(UdpSenderTest, DeliversADatagramOverLoopback) {
  asio::io_context io;
  asio::ip::udp::socket receiver(io);
  asio::error_code ec;
  ASSERT_FALSE(receiver.open(asio::ip::udp::v4(), ec));
  ASSERT_FALSE(receiver.bind({asio::ip::make_address_v4("127.0.0.1"), 0}, ec));
  const uint16_t port = receiver.local_endpoint().port();

  absl::StatusOr<std::unique_ptr<UdpSender>> sender = UdpSender::Create(io);
  ASSERT_TRUE(sender.ok()) << sender.status();
  constexpr std::array<uint8_t, 3> kDatagram = {1, 2, 3};
  ASSERT_TRUE((*sender)->Send({{127, 0, 0, 1}, port}, kDatagram).ok());

  std::array<uint8_t, 16> buffer{};
  asio::ip::udp::endpoint from;
  const size_t n = receiver.receive_from(asio::buffer(buffer), from, 0, ec);
  ASSERT_FALSE(ec) << ec.message();
  EXPECT_THAT(std::span(buffer).first(n), ElementsAre(1, 2, 3));
}

}  // namespace
}  // namespace wsc_server
