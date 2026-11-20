#ifndef WSC_SERVER_LIBWSC_TRANSPORT_H_
#define WSC_SERVER_LIBWSC_TRANSPORT_H_

#include <cstdint>
#include <span>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "libwsc/types.h"
#include "libwsc/wire.h"

namespace wsc {

struct Param {
  ParamKey key;
  std::span<const uint8_t> value;
};

// Transport Descriptor (spec 05). Spans view the packet bytes when decoded.
struct TransportDescriptor {
  Protocol protocol;
  Iface iface;
  AddrType addr_type;
  std::span<const uint8_t> addr_data;  // layout per addr_type
  absl::InlinedVector<Param, 4> params;
};

// Parses a whole descriptor; trailing bytes are an error.
absl::StatusOr<TransportDescriptor> DecodeTransport(
    std::span<const uint8_t> bytes);

// Appends the descriptor to `out`. Fails when addr_data does not match
// addr_type or a field does not fit its length byte.
absl::Status EncodeTransport(const TransportDescriptor& td, Writer& out);

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_TRANSPORT_H_
