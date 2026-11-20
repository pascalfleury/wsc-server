#ifndef WSC_SERVER_LIBWSC_VALIDATE_H_
#define WSC_SERVER_LIBWSC_VALIDATE_H_

#include "absl/status/status.h"
#include "libwsc/packet.h"

namespace wsc {

// Compatibility rules of spec 05/07 for a structurally valid packet (Decode
// and Encode check structure and flag invariants): the (protocol, iface)
// matrix, the (type, protocol) matrix when GW = 1, and TUNNEL_RAW only with
// protocol RAW. Failures carry the WSC error code (error.h).
absl::Status Validate(const Packet& packet);

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_VALIDATE_H_
