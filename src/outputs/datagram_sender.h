#ifndef WSC_SERVER_SRC_OUTPUTS_DATAGRAM_SENDER_H_
#define WSC_SERVER_SRC_OUTPUTS_DATAGRAM_SENDER_H_

#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <utility>

#include "absl/status/status.h"

namespace wsc_server {

struct Ipv4Endpoint {
  std::array<uint8_t, 4> address;
  uint16_t port;

  friend auto operator<=>(const Ipv4Endpoint&, const Ipv4Endpoint&) = default;
  template <typename H>
  friend H AbslHashValue(H h, const Ipv4Endpoint& e) {
    return H::combine(std::move(h), e.address, e.port);
  }
};

// Sends datagrams (UDP for the network adapters). An interface so adapters can
// be tested without sockets.
class DatagramSender {
 public:
  virtual ~DatagramSender() = default;
  virtual absl::Status Send(const Ipv4Endpoint& to,
                            std::span<const uint8_t> datagram) = 0;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_OUTPUTS_DATAGRAM_SENDER_H_
