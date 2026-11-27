#include "tools/wsc_send/request.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"

namespace wsc_server::wsc_send {
namespace {

// A decimal number in [0, max].
absl::StatusOr<uint32_t> ParseNumber(std::string_view text, uint32_t max,
                                     std::string_view what) {
  uint32_t value = 0;
  if (!absl::SimpleAtoi(text, &value) || value > max) {
    return absl::InvalidArgumentError(
        absl::StrCat(what, " \"", text, "\" is not a number in 0-", max));
  }
  return value;
}

absl::StatusOr<std::vector<uint8_t>> EncodePacket(const wsc::Packet& packet) {
  // The header, a full universe and an IPv4 descriptor fit easily.
  std::vector<uint8_t> out(2048);
  const absl::StatusOr<size_t> size = wsc::Encode(packet, out);
  if (!size.ok()) return size.status();
  out.resize(*size);
  return out;
}

}  // namespace

absl::StatusOr<std::vector<uint8_t>> ParseValues(std::string_view text) {
  std::vector<uint8_t> values;
  for (const std::string_view item : absl::StrSplit(text, ',')) {
    const std::vector<std::string_view> parts = absl::StrSplit(item, '*');
    if (parts.size() > 2) {
      return absl::InvalidArgumentError(
          absl::StrCat("value \"", item, "\" has more than one '*'"));
    }
    const absl::StatusOr<uint32_t> value = ParseNumber(parts[0], 255, "value");
    if (!value.ok()) return value.status();
    uint32_t count = 1;
    if (parts.size() == 2) {
      const absl::StatusOr<uint32_t> n = ParseNumber(parts[1], 512, "count");
      if (!n.ok()) return n.status();
      count = *n;
    }
    values.insert(values.end(), count, static_cast<uint8_t>(*value));
  }
  return values;
}

absl::StatusOr<Ipv4AddrData> ParseIpv4Endpoint(std::string_view text,
                                               uint16_t default_port) {
  const std::vector<std::string_view> host_port = absl::StrSplit(text, ':');
  const std::vector<std::string_view> octets =
      absl::StrSplit(host_port[0], '.');
  if (host_port.size() > 2 || octets.size() != 4) {
    return absl::InvalidArgumentError(
        absl::StrCat("\"", text, "\" is not a.b.c.d or a.b.c.d:port"));
  }
  Ipv4AddrData addr{};
  for (size_t i = 0; i < octets.size(); ++i) {
    const absl::StatusOr<uint32_t> octet =
        ParseNumber(octets[i], 255, "address byte");
    if (!octet.ok()) return octet.status();
    addr[i] = static_cast<uint8_t>(*octet);
  }
  uint32_t port = default_port;
  if (host_port.size() == 2) {
    const absl::StatusOr<uint32_t> parsed =
        ParseNumber(host_port[1], 0xFFFF, "port");
    if (!parsed.ok()) return parsed.status();
    port = *parsed;
  }
  addr[4] = static_cast<uint8_t>(port >> 8);
  addr[5] = static_cast<uint8_t>(port);
  return addr;
}

absl::StatusOr<std::vector<uint8_t>> EncodeChannels(
    const ChannelsRequest& request, uint32_t id) {
  uint16_t flags =
      wsc::flags::kTransport | wsc::flags::kGateway | wsc::flags::kAck;
  wsc::TransportDescriptor td{.protocol = wsc::Protocol::kSacn,
                              .iface = wsc::Iface::kUdp,
                              .addr_type = wsc::AddrType::kNone,
                              .addr_data = {},
                              .params = {}};
  if (request.unicast) {
    td.addr_type = wsc::AddrType::kIpv4;
    td.addr_data = *request.unicast;
  } else {
    flags |= wsc::flags::kMulticast;
  }
  td.params.push_back({.key = wsc::ParamKey::kPriority,
                       .value = std::span(&request.priority, 1)});
  const wsc::Packet packet{
      .header = {.flags = flags, .id = id},
      .transport = td,
      .body = wsc::StreamChannels{.universe = request.universe,
                                  .start_channel = request.start_channel,
                                  .values = request.values}};
  return EncodePacket(packet);
}

std::vector<uint8_t> EncodeKeepalive(uint32_t id) {
  // Cannot fail: no addresses, no values, id checked by the caller.
  return *EncodePacket(
      {.header = {.flags = 0, .id = id},
       .transport = std::nullopt,
       .body = wsc::StateQuery{
           .type = wsc::QueryType::kKeepalive, .target_id = {}, .extra = {}}});
}

absl::Status CheckReply(std::span<const uint8_t> reply, uint32_t id) {
  const absl::StatusOr<wsc::Packet> packet = wsc::Decode(reply);
  if (!packet.ok()) {
    return absl::DataLossError(
        absl::StrCat("undecodable reply: ", packet.status().message()));
  }
  if (const auto* answer = std::get_if<wsc::StateAnswer>(&packet->body);
      answer != nullptr && answer->query_id == id) {
    if (answer->status == wsc::AnswerStatus::kSuccess) {
      return absl::OkStatus();
    }
    return absl::UnknownError(
        absl::StrCat("STATE_ANSWER status ", static_cast<int>(answer->status)));
  }
  if (const auto* error = std::get_if<wsc::StateError>(&packet->body);
      error != nullptr && packet->header.id == id) {
    return absl::UnknownError(absl::StrCat(
        "STATE_ERROR 0x",
        absl::Hex(static_cast<uint16_t>(error->code), absl::kZeroPad4), ": ",
        error->message));
  }
  return absl::DataLossError(absl::StrCat(
      "unexpected ", wsc::TypeName(wsc::TypeOf(packet->body)), " reply"));
}

}  // namespace wsc_server::wsc_send
