#ifndef WSC_SERVER_TESTING_FAKE_DATAGRAM_SENDER_H_
#define WSC_SERVER_TESTING_FAKE_DATAGRAM_SENDER_H_

#include <cstdint>
#include <span>
#include <vector>

#include "absl/status/status.h"
#include "src/outputs/datagram_sender.h"

namespace wsc_server {

// Records every datagram instead of sending it.
class FakeDatagramSender final : public DatagramSender {
 public:
  struct Datagram {
    Ipv4Endpoint to;
    std::vector<uint8_t> bytes;
  };

  absl::Status Send(const Ipv4Endpoint& to,
                    std::span<const uint8_t> datagram) override {
    sent.push_back({to, {datagram.begin(), datagram.end()}});
    return absl::OkStatus();
  }

  std::vector<Datagram> sent;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_TESTING_FAKE_DATAGRAM_SENDER_H_
