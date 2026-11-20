#include "libwsc/transport.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "libwsc/error.h"
#include "libwsc/wire.h"

namespace wsc {
namespace {

// Byte length of addr_data for fixed-size types; nullopt for variable ones.
std::optional<size_t> FixedAddrSize(AddrType type) {
  switch (type) {
    case AddrType::kNone:
      return 0;
    case AddrType::kIpv4:
      return 6;
    case AddrType::kIpv6:
      return 18;
    case AddrType::kSerial:
      return 1;
    case AddrType::kUsb:
      return 4;
    default:
      return std::nullopt;
  }
}

// Reads addr_data from `in`; empty with !in.ok() on failure.
std::span<const uint8_t> ReadAddr(AddrType type, Reader& in) {
  if (const std::optional<size_t> size = FixedAddrSize(type)) {
    return in.Bytes(*size);
  }
  switch (type) {
    case AddrType::kHostname:
      return in.Bytes(1 + in.Peek() + 2);  // port
    case AddrType::kUrl:
      return in.Bytes(1 + in.Peek());
    default:
      return in.Bytes(in.remaining() + 1);  // unknown layout: fail
  }
}

}  // namespace

absl::StatusOr<TransportDescriptor> DecodeTransport(
    std::span<const uint8_t> bytes) {
  Reader in(bytes);
  TransportDescriptor td;
  td.protocol = static_cast<Protocol>(in.U8());
  td.iface = static_cast<Iface>(in.U8());
  td.addr_type = static_cast<AddrType>(in.U8());
  td.addr_data = ReadAddr(td.addr_type, in);
  const uint8_t count = in.U8();
  for (uint8_t i = 0; i < count && in.ok(); ++i) {
    const auto key = static_cast<ParamKey>(in.U8());
    td.params.push_back({key, in.Bytes(in.U8())});
  }
  if (!in.ok() || in.remaining() != 0) {
    return Error(ErrorCode::kMissingTransport,
                 "transport descriptor is malformed");
  }
  return td;
}

absl::Status EncodeTransport(const TransportDescriptor& td, Writer& out) {
  const std::optional<size_t> fixed = FixedAddrSize(td.addr_type);
  const std::span<const uint8_t> addr = td.addr_data;
  bool addr_ok = false;
  if (fixed) {
    addr_ok = addr.size() == *fixed;
  } else if (td.addr_type == AddrType::kHostname ||
             td.addr_type == AddrType::kUrl) {
    const size_t extra = td.addr_type == AddrType::kHostname ? 3 : 1;
    addr_ok = !addr.empty() && addr.size() == extra + addr[0];
  }
  if (!addr_ok) {
    return absl::InvalidArgumentError("addr_data does not match addr_type");
  }
  if (td.params.size() > 0xFF) {
    return absl::InvalidArgumentError("more than 255 transport params");
  }
  out.U8(static_cast<uint8_t>(td.protocol));
  out.U8(static_cast<uint8_t>(td.iface));
  out.U8(static_cast<uint8_t>(td.addr_type));
  out.Bytes(addr);
  out.U8(static_cast<uint8_t>(td.params.size()));
  for (const Param& p : td.params) {
    if (p.value.size() > 0xFF) {
      return absl::InvalidArgumentError("transport param exceeds 255 bytes");
    }
    out.U8(static_cast<uint8_t>(p.key));
    out.U8(static_cast<uint8_t>(p.value.size()));
    out.Bytes(p.value);
  }
  return absl::OkStatus();
}

}  // namespace wsc
