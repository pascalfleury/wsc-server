#include "libwsc/packet.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "libwsc/address.h"
#include "libwsc/error.h"
#include "libwsc/status_macros.h"
#include "libwsc/transport.h"
#include "libwsc/wire.h"

namespace wsc {
namespace {

constexpr std::string_view kMagic = "WSC!";

// Value size for fixed-size types; nullopt for STRING and unknown types.
std::optional<size_t> FixedValueSize(ValueType type) {
  switch (type) {
    case ValueType::kU8:
    case ValueType::kI8:
    case ValueType::kBool:
      return 1;
    case ValueType::kU16:
    case ValueType::kI16:
      return 2;
    case ValueType::kU32:
    case ValueType::kI32:
    case ValueType::kF32:
      return 4;
    case ValueType::kU64:
    case ValueType::kI64:
    case ValueType::kF64:
      return 8;
    default:
      return std::nullopt;
  }
}

// Flag invariants (spec 02) and the TS rejection (D-17).
absl::Status CheckFlags(uint16_t f, bool has_transport) {
  const bool tr = (f & flags::kTransport) != 0;
  if ((f & flags::kGateway) != 0 && !tr) {
    return Error(ErrorCode::kMissingTransportFlag, "GW = 1 requires TR = 1");
  }
  if (!tr && has_transport) {
    return Error(ErrorCode::kMissingTransportFlag,
                 "transport descriptor present but TR = 0");
  }
  if (tr && !has_transport) {
    return Error(ErrorCode::kMissingTransport,
                 "TR = 1 but no transport descriptor");
  }
  if ((f & flags::kTimestamp) != 0) {
    return Error(ErrorCode::kValidationFailed,
                 "TS flag is not specified by WSC 1.1.0");
  }
  return absl::OkStatus();
}

std::span<const uint8_t> ReadAddress(Reader& in) { return in.Bytes(in.U16()); }

std::optional<Body> DecodeBody(MessageType type, Reader& in) {
  switch (type) {
    case MessageType::kStreamChannels: {
      StreamChannels b;
      b.universe = in.U16();
      b.start_channel = in.U16();
      b.values = in.Bytes(in.U16());
      return b;
    }
    case MessageType::kStreamTimecode:
      return StreamTimecode{.hours = in.U8(),
                            .minutes = in.U8(),
                            .seconds = in.U8(),
                            .frames = in.U8(),
                            .rate = in.U8()};
    case MessageType::kControlCue: {
      ControlCue b;
      b.address = ReadAddress(in);
      b.action = static_cast<CueAction>(in.U8());
      return b;
    }
    case MessageType::kControlParam: {
      ControlParam b;
      b.address = ReadAddress(in);
      b.type = static_cast<ValueType>(in.U8());
      if (const std::optional<size_t> size = FixedValueSize(b.type)) {
        b.value = in.Bytes(*size);
      } else if (b.type == ValueType::kString) {
        b.value = in.Bytes(in.U16());
      } else {
        return std::nullopt;
      }
      return b;
    }
    case MessageType::kTunnelRaw:
      return TunnelRaw{in.Rest()};
    case MessageType::kStateQuery: {
      StateQuery b;
      b.type = static_cast<QueryType>(in.U8());
      b.target_id = in.String16();
      b.extra = in.Rest();
      return b;
    }
    case MessageType::kStateAnswer: {
      StateAnswer b;
      b.query_id = in.U32();
      b.status = static_cast<AnswerStatus>(in.U8());
      b.format = static_cast<AnswerFormat>(in.U8());
      b.data = in.Bytes(in.U16());
      return b;
    }
    case MessageType::kStateError: {
      StateError b;
      b.code = static_cast<ErrorCode>(in.U16());
      b.message = in.String16();
      b.context = in.String16();
      return b;
    }
  }
  return std::nullopt;
}

bool IsKnownType(uint16_t type) {
  switch (static_cast<MessageType>(type)) {
    case MessageType::kStreamChannels:
    case MessageType::kStreamTimecode:
    case MessageType::kControlCue:
    case MessageType::kControlParam:
    case MessageType::kTunnelRaw:
    case MessageType::kStateQuery:
    case MessageType::kStateAnswer:
    case MessageType::kStateError:
      return true;
  }
  return false;
}

// Writes `bytes` with a u16 length prefix.
absl::Status WriteSized(std::span<const uint8_t> bytes, Writer& w) {
  if (bytes.size() > 0xFFFF) {
    return absl::InvalidArgumentError("field exceeds 65535 bytes");
  }
  w.U16(static_cast<uint16_t>(bytes.size()));
  w.Bytes(bytes);
  return absl::OkStatus();
}
absl::Status WriteSized(std::string_view s, Writer& w) {
  return WriteSized(
      std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()), w);
}

absl::Status WriteAddress(std::span<const uint8_t> address, Writer& w) {
  if (absl::Status status = CheckAddress(address); !status.ok()) {
    return absl::InvalidArgumentError(status.message());
  }
  return WriteSized(address, w);
}

struct BodyEncoder {
  Writer& w;

  absl::Status operator()(const StreamChannels& b) const {
    w.U16(b.universe);
    w.U16(b.start_channel);
    return WriteSized(b.values, w);
  }
  absl::Status operator()(const StreamTimecode& b) const {
    for (const uint8_t v : {b.hours, b.minutes, b.seconds, b.frames, b.rate}) {
      w.U8(v);
    }
    return absl::OkStatus();
  }
  absl::Status operator()(const ControlCue& b) const {
    absl::Status status = WriteAddress(b.address, w);
    w.U8(static_cast<uint8_t>(b.action));
    return status;
  }
  absl::Status operator()(const ControlParam& b) const {
    RETURN_IF_ERROR(WriteAddress(b.address, w));
    w.U8(static_cast<uint8_t>(b.type));
    if (b.type == ValueType::kString) return WriteSized(b.value, w);
    if (FixedValueSize(b.type) != b.value.size()) {
      return absl::InvalidArgumentError("value size does not match its type");
    }
    w.Bytes(b.value);
    return absl::OkStatus();
  }
  absl::Status operator()(const TunnelRaw& b) const {
    w.Bytes(b.data);
    return absl::OkStatus();
  }
  absl::Status operator()(const StateQuery& b) const {
    w.U8(static_cast<uint8_t>(b.type));
    absl::Status status = WriteSized(b.target_id, w);
    w.Bytes(b.extra);
    return status;
  }
  absl::Status operator()(const StateAnswer& b) const {
    w.U32(b.query_id);
    w.U8(static_cast<uint8_t>(b.status));
    w.U8(static_cast<uint8_t>(b.format));
    return WriteSized(b.data, w);
  }
  absl::Status operator()(const StateError& b) const {
    w.U16(static_cast<uint16_t>(b.code));
    RETURN_IF_ERROR(WriteSized(b.message, w));
    return WriteSized(b.context, w);
  }
};

}  // namespace

MessageType TypeOf(const Body& body) {
  struct {
    MessageType operator()(const StreamChannels&) const {
      return MessageType::kStreamChannels;
    }
    MessageType operator()(const StreamTimecode&) const {
      return MessageType::kStreamTimecode;
    }
    MessageType operator()(const ControlCue&) const {
      return MessageType::kControlCue;
    }
    MessageType operator()(const ControlParam&) const {
      return MessageType::kControlParam;
    }
    MessageType operator()(const TunnelRaw&) const {
      return MessageType::kTunnelRaw;
    }
    MessageType operator()(const StateQuery&) const {
      return MessageType::kStateQuery;
    }
    MessageType operator()(const StateAnswer&) const {
      return MessageType::kStateAnswer;
    }
    MessageType operator()(const StateError&) const {
      return MessageType::kStateError;
    }
  } visitor;
  return std::visit(visitor, body);
}

absl::StatusOr<Packet> Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < kHeaderSize) return Drop("shorter than a WSC header");
  Reader in(bytes);
  const std::span<const uint8_t> magic = in.Bytes(kMagic.size());
  if (!std::ranges::equal(magic, kMagic, {}, {},
                          [](char c) { return static_cast<uint8_t>(c); })) {
    return Drop("bad magic");
  }
  Packet packet;
  for (uint8_t& v : packet.header.version) v = in.U8();
  if (packet.header.version[0] != kVersion[0]) {
    return Error(ErrorCode::kProtocolVersion, "unsupported major version");
  }
  const uint16_t type = in.U16();
  packet.header.flags = in.U16();
  packet.header.id = in.U32();
  const uint16_t payload_len = in.U16();
  const uint16_t transport_len = in.U16();

  if (!IsKnownType(type)) {
    return Error(ErrorCode::kInvalidMessageType, "unknown message type");
  }
  if (in.remaining() != size_t{payload_len} + transport_len) {
    return Error(ErrorCode::kMalformedPacket,
                 "length fields do not match the packet size");
  }
  if (packet.header.id == 0) {
    return Error(ErrorCode::kMalformedPacket, "packet id 0");
  }
  RETURN_IF_ERROR(CheckFlags(packet.header.flags, transport_len > 0));

  Reader payload(in.Bytes(payload_len));
  std::optional<Body> body =
      DecodeBody(static_cast<MessageType>(type), payload);
  if (!body || !payload.ok() || payload.remaining() != 0) {
    return Error(ErrorCode::kMalformedPacket, "payload cannot be parsed");
  }
  if (const auto* cue = std::get_if<ControlCue>(&*body)) {
    RETURN_IF_ERROR(CheckAddress(cue->address));
  } else if (const auto* param = std::get_if<ControlParam>(&*body)) {
    RETURN_IF_ERROR(CheckAddress(param->address));
  }
  packet.body = *body;

  if (transport_len > 0) {
    absl::StatusOr<TransportDescriptor> td = DecodeTransport(in.Rest());
    if (!td.ok()) return td.status();
    packet.transport = *std::move(td);
  }
  return packet;
}

absl::StatusOr<size_t> Encode(const Packet& packet, std::span<uint8_t> out) {
  const Header& h = packet.header;
  if (h.id == 0) return absl::InvalidArgumentError("packet id 0");
  RETURN_IF_ERROR(CheckFlags(h.flags, packet.transport.has_value()));

  Writer w(out);
  w.Bytes(kMagic);
  w.Bytes(h.version);
  w.U16(static_cast<uint16_t>(TypeOf(packet.body)));
  w.U16(h.flags);
  w.U32(h.id);
  w.U16(0);  // payload_len, patched below
  w.U16(0);  // transport_len, patched below

  RETURN_IF_ERROR(std::visit(BodyEncoder{w}, packet.body));
  const size_t payload_len = w.written() - kHeaderSize;
  if (packet.transport) {
    RETURN_IF_ERROR(EncodeTransport(*packet.transport, w));
  }
  const size_t transport_len = w.written() - kHeaderSize - payload_len;
  if (payload_len > 0xFFFF || transport_len > 0xFFFF) {
    return absl::InvalidArgumentError(
        "payload or transport exceeds 65535 bytes");
  }
  if (!w.ok()) return absl::ResourceExhaustedError("output buffer too small");
  w.PatchU16(15, static_cast<uint16_t>(payload_len));
  w.PatchU16(17, static_cast<uint16_t>(transport_len));
  return w.written();
}

}  // namespace wsc
