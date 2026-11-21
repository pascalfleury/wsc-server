#ifndef WSC_SERVER_SRC_OUTPUTS_UDP_SENDER_H_
#define WSC_SERVER_SRC_OUTPUTS_UDP_SENDER_H_

#include <cstdint>
#include <memory>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/outputs/datagram_sender.h"
#include "src/platform/asio.h"

namespace wsc_server {

// Sends IPv4 UDP datagrams, unicast or multicast, from one socket. Send() is
// synchronous and is called from one strand at a time.
class UdpSender final : public DatagramSender {
 public:
  static absl::StatusOr<std::unique_ptr<UdpSender>> Create(
      asio::io_context& io);

  absl::Status Send(const Ipv4Endpoint& to,
                    std::span<const uint8_t> datagram) override;

 private:
  explicit UdpSender(asio::ip::udp::socket socket)
      : socket_(std::move(socket)) {}

  asio::ip::udp::socket socket_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_OUTPUTS_UDP_SENDER_H_
