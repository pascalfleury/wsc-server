#ifndef WSC_SERVER_TOOLS_WSC_SEND_REQUEST_H_
#define WSC_SERVER_TOOLS_WSC_SEND_REQUEST_H_

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// What wsc-send puts on the wire, and how it reads the reply. Pure: no I/O.
namespace wsc_server::wsc_send {

// IPv4 Transport Descriptor addr_data: four address bytes, big-endian port.
using Ipv4AddrData = std::array<uint8_t, 6>;

// A STREAM_CHANNELS packet for the sACN adapter.
struct ChannelsRequest {
  uint16_t universe = 1;
  uint16_t start_channel = 1;
  std::vector<uint8_t> values;
  std::optional<Ipv4AddrData> unicast;  // nullopt: the MC flag (multicast)
  uint8_t priority = 100;
};

// "255,0,128": DMX values 0-255. "v*n" repeats v n times ("255*512").
absl::StatusOr<std::vector<uint8_t>> ParseValues(std::string_view text);

// "a.b.c.d" or "a.b.c.d:port" (default_port when omitted).
absl::StatusOr<Ipv4AddrData> ParseIpv4Endpoint(std::string_view text,
                                               uint16_t default_port);

// The encoded packet, with TR, GW and ACK set, so the server always replies.
absl::StatusOr<std::vector<uint8_t>> EncodeChannels(
    const ChannelsRequest& request, uint32_t id);

// STATE_QUERY(KEEPALIVE).
std::vector<uint8_t> EncodeKeepalive(uint32_t id);

// The server's reply to packet `id`: OK for STATE_ANSWER(SUCCESS); otherwise
// a status naming the answer status or the STATE_ERROR code and message.
absl::Status CheckReply(std::span<const uint8_t> reply, uint32_t id);

}  // namespace wsc_server::wsc_send

#endif  // WSC_SERVER_TOOLS_WSC_SEND_REQUEST_H_
