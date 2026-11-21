#include "src/outputs/udp_sender.h"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "src/platform/asio.h"

namespace wsc_server {

absl::StatusOr<std::unique_ptr<UdpSender>> UdpSender::Create(
    asio::io_context& io) {
  asio::ip::udp::socket socket(io);
  asio::error_code ec;
  if (socket.open(asio::ip::udp::v4(), ec)) {
    return absl::UnavailableError(
        absl::StrCat("cannot open UDP socket: ", ec.message()));
  }
  return std::unique_ptr<UdpSender>(new UdpSender(std::move(socket)));
}

absl::Status UdpSender::Send(const Ipv4Endpoint& to,
                             std::span<const uint8_t> datagram) {
  const asio::ip::udp::endpoint endpoint(asio::ip::address_v4(to.address),
                                         to.port);
  asio::error_code ec;
  socket_.send_to(asio::buffer(datagram.data(), datagram.size()), endpoint, 0,
                  ec);
  if (ec) {
    return absl::UnavailableError(
        absl::StrCat("UDP send to ", endpoint.address().to_string(), ":",
                     to.port, " failed: ", ec.message()));
  }
  return absl::OkStatus();
}

}  // namespace wsc_server
