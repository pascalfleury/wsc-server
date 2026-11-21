#include "src/routing/router.h"

#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "libwsc/error.h"
#include "libwsc/status_macros.h"
#include "libwsc/types.h"
#include "libwsc/validate.h"

namespace wsc_server {
namespace {

std::string Describe(wsc::Protocol protocol, wsc::Iface iface) {
  return absl::StrCat("protocol 0x", absl::Hex(static_cast<uint8_t>(protocol)),
                      " on iface 0x", absl::Hex(static_cast<uint8_t>(iface)));
}

}  // namespace

absl::Status Router::Register(OutputAdapter& adapter) {
  const wsc::Protocol protocol = adapter.protocol();
  for (const wsc::Iface iface : adapter.ifaces()) {
    if (adapters_.contains({protocol, iface})) {
      return absl::AlreadyExistsError(
          absl::StrCat(Describe(protocol, iface), " has an adapter"));
    }
  }
  for (const wsc::Iface iface : adapter.ifaces()) {
    adapters_.emplace(Key{protocol, iface}, &adapter);
  }
  return absl::OkStatus();
}

absl::Status Router::Route(Message message) const {
  const wsc::Packet& packet = message.packet;
  RETURN_IF_ERROR(wsc::Validate(packet));
  // GW = 1 implies a Transport Descriptor (flag invariants, checked by Decode).
  if ((packet.header.flags & wsc::flags::kGateway) == 0 ||
      !packet.transport.has_value()) {
    return wsc::Error(wsc::ErrorCode::kNoRoute,
                      "packet does not request gateway forwarding (GW = 0)");
  }
  const wsc::TransportDescriptor& td = *packet.transport;
  const auto it = adapters_.find(Key{td.protocol, td.iface});
  if (it == adapters_.end()) {
    return wsc::Error(wsc::ErrorCode::kNoRoute,
                      absl::StrCat("no output adapter for ",
                                   Describe(td.protocol, td.iface)));
  }
  OutputAdapter& adapter = *it->second;
  if (!adapter.enabled()) {
    return wsc::Error(
        wsc::ErrorCode::kConfigError,
        absl::StrCat("output adapter for ", Describe(td.protocol, td.iface),
                     " is disabled"));
  }
  adapter.Submit(std::move(message));
  return absl::OkStatus();
}

}  // namespace wsc_server
