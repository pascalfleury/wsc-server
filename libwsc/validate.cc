#include "libwsc/validate.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <variant>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"

namespace wsc {
namespace {

using enum Protocol;

using ProtocolList = std::initializer_list<Protocol>;
using IfaceList = std::initializer_list<Iface>;

constexpr uint8_t Bit(Iface iface) {
  const auto code = static_cast<uint8_t>(iface);
  return code < 8 ? static_cast<uint8_t>(1u << code) : 0;
}

// Spec 05, protocol-interface matrix: allowed interfaces as a bitmask, indexed
// by protocol code.
constexpr std::array<uint8_t, 256> kIfacesByProtocol = [] {
  using enum Iface;
  std::array<uint8_t, 256> table{};
  auto allow = [&table](Protocol protocol, IfaceList ifaces) {
    for (const Iface iface : ifaces) {
      table[static_cast<uint8_t>(protocol)] |= Bit(iface);
    }
  };
  allow(kDmx512, {kSerial, kUsb});
  allow(kArtNet, {kUdp});
  allow(kSacn, {kUdp});
  allow(kKinet, {kUdp});
  allow(kRdm, {kSerial, kUsb, kUdp});
  allow(kMidi, {kSerial, kUsb, kUdp});
  allow(kMidi2, {kUsb, kUdp});
  allow(kMsc, {kSerial, kUsb, kUdp});
  allow(kMtc, {kSerial, kUsb, kUdp});
  allow(kMmc, {kSerial, kUsb});
  allow(kOsc, {kUdp, kTcp, Iface::kWebSocket});
  allow(kNdi, {kUdp, kTcp});
  allow(kVisca, {kSerial, kUdp});
  allow(kModbus, {kSerial, kTcp, kUdp});
  allow(kCanOpen, {kSerial, kUsb});
  allow(kEtherCat, {kUdp});
  allow(kProfinet, {kUdp});
  allow(kGpiGpo, {kSerial, kUsb});
  allow(kTally, {kUdp, kTcp, kSerial});
  allow(Protocol::kHttp, {Iface::kHttp});
  allow(Protocol::kWebSocket, {Iface::kWebSocket});
  allow(kMqtt, {kTcp, Iface::kWebSocket});
  allow(kRaw, {kUdp, kTcp, kSerial, kUsb, Iface::kWebSocket, Iface::kHttp});
  return table;
}();

bool IfaceAllowed(Protocol protocol, Iface iface) {
  return (kIfacesByProtocol[static_cast<uint8_t>(protocol)] & Bit(iface)) != 0;
}

// A set of protocol codes (0-255) as a 256-bit mask.
using ProtocolSet = std::array<uint64_t, 4>;

constexpr ProtocolSet Protocols(ProtocolList protocols) {
  ProtocolSet set{};
  for (const Protocol protocol : protocols) {
    const auto code = static_cast<uint8_t>(protocol);
    set[code / 64] |= uint64_t{1} << (code % 64);
  }
  return set;
}

constexpr ProtocolSet kAllProtocols = {~uint64_t{0}, ~uint64_t{0}, ~uint64_t{0},
                                       ~uint64_t{0}};

struct TypeRule {
  MessageType type;
  ProtocolSet protocols;
};

// Spec 05, message type-protocol matrix (applies when GW = 1).
constexpr auto kProtocolsByType = std::to_array<TypeRule>({
    {MessageType::kStreamChannels,
     Protocols({kDmx512, kArtNet, kSacn, kKinet, kModbus, kOsc, kRaw})},
    {MessageType::kStreamTimecode,
     Protocols({kMtc, kOsc, kMidi, kArtNet, kRaw})},
    {MessageType::kControlCue,
     Protocols({kMsc, kOsc, kMidi, kHttp, kWebSocket, kGpiGpo, kRaw})},
    {MessageType::kControlParam,
     Protocols({kOsc, kMidi, kMidi2, kModbus, kHttp, kWebSocket, kCanOpen,
                kProfinet, kEtherCat, kRaw})},
    {MessageType::kTunnelRaw, Protocols({kRaw})},
    {MessageType::kStateQuery, kAllProtocols},
    {MessageType::kStateAnswer, kAllProtocols},
    {MessageType::kStateError, kAllProtocols},
});

bool ProtocolAllowed(MessageType type, Protocol protocol) {
  const auto rule = absl::c_find_if(
      kProtocolsByType, [type](const TypeRule& r) { return r.type == type; });
  if (rule == kProtocolsByType.end()) return false;
  const auto code = static_cast<uint8_t>(protocol);
  return ((rule->protocols[code / 64] >> (code % 64)) & 1) != 0;
}

}  // namespace

absl::Status Validate(const Packet& packet) {
  const MessageType type = TypeOf(packet.body);
  const std::optional<TransportDescriptor>& td = packet.transport;
  if (type == MessageType::kTunnelRaw && (!td || td->protocol != kRaw)) {
    return Error(ErrorCode::kInvalidTunnel,
                 "TUNNEL_RAW requires a transport with protocol RAW");
  }
  if (!td) return absl::OkStatus();
  if (!IfaceAllowed(td->protocol, td->iface)) {
    return Error(ErrorCode::kIncompatibleIface,
                 "protocol not permitted on this interface");
  }
  if ((packet.header.flags & flags::kGateway) != 0 &&
      !ProtocolAllowed(type, td->protocol)) {
    return Error(ErrorCode::kIncompatibleProtocol,
                 "message type not permitted for this protocol");
  }
  return absl::OkStatus();
}

}  // namespace wsc
