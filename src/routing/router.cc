#include "src/routing/router.h"

#include <cstdint>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "libwsc/error.h"
#include "libwsc/status_macros.h"
#include "libwsc/types.h"
#include "libwsc/validate.h"

namespace wsc_server {
namespace {

uint8_t Code(wsc::Protocol protocol) { return static_cast<uint8_t>(protocol); }

}  // namespace

absl::Status Router::Register(OutputAdapter& adapter) {
  if (!adapters_.try_emplace(adapter.protocol(), &adapter).second) {
    return absl::AlreadyExistsError(absl::StrCat(
        "protocol 0x", absl::Hex(Code(adapter.protocol())), " has an adapter"));
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
  const uint8_t protocol = Code(packet.transport->protocol);
  const auto it = adapters_.find(packet.transport->protocol);
  if (it == adapters_.end()) {
    return wsc::Error(
        wsc::ErrorCode::kNoRoute,
        absl::StrCat("no output adapter for protocol 0x", absl::Hex(protocol)));
  }
  OutputAdapter& adapter = *it->second;
  if (!adapter.enabled()) {
    return wsc::Error(wsc::ErrorCode::kConfigError,
                      absl::StrCat("output adapter for protocol 0x",
                                   absl::Hex(protocol), " is disabled"));
  }
  adapter.Submit(std::move(message));
  return absl::OkStatus();
}

}  // namespace wsc_server
