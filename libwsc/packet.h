#ifndef WSC_SERVER_LIBWSC_PACKET_H_
#define WSC_SERVER_LIBWSC_PACKET_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "absl/status/statusor.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"

// WSC packets (spec 02, 04). Decoded bodies are views into the input bytes and
// must not outlive them; the caller keeps the Frame alive (Message, DATAPATH).
namespace wsc {

inline constexpr size_t kHeaderSize = 19;
inline constexpr std::array<uint8_t, 3> kVersion = {1, 1, 0};

// The header fields that are not derived. The type comes from the body, and
// payload_len / transport_len from the encoded regions.
struct Header {
  std::array<uint8_t, 3> version = kVersion;
  uint16_t flags = 0;
  uint32_t id = 0;
};

struct StreamChannels {
  uint16_t universe;
  uint16_t start_channel;
  std::span<const uint8_t> values;
};

struct StreamTimecode {
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
  uint8_t frames;
  uint8_t rate;
};

struct ControlCue {
  std::span<const uint8_t> address;  // wire form incl. terminator (address.h)
  CueAction action;
};

struct ControlParam {
  std::span<const uint8_t> address;
  ValueType type;
  // Big-endian value bytes of the type's size; for STRING the UTF-8 bytes
  // without the length prefix.
  std::span<const uint8_t> value;
};

struct TunnelRaw {
  std::span<const uint8_t> data;
};

struct StateQuery {
  QueryType type;
  std::string_view target_id;
  std::span<const uint8_t> extra;  // bytes after target_id (RDM_SET, D-14)
};

struct StateAnswer {
  uint32_t query_id;
  AnswerStatus status;
  AnswerFormat format;
  std::span<const uint8_t> data;
};

struct StateError {
  ErrorCode code;
  std::string_view message;
  std::string_view context;  // JSON object
};

using Body =
    std::variant<StreamChannels, StreamTimecode, ControlCue, ControlParam,
                 TunnelRaw, StateQuery, StateAnswer, StateError>;

MessageType TypeOf(const Body& body);

struct Packet {
  Header header;
  std::optional<TransportDescriptor> transport;  // present iff TR = 1
  Body body;
};

// Parses one packet that fills `bytes` exactly. Failures carry the WSC error
// code to answer with (error.h); IsDrop() ones are dropped silently.
// Packets with the TS flag fail with VALIDATION_FAILED (D-17).
absl::StatusOr<Packet> Decode(std::span<const uint8_t> bytes);

// Writes the packet to `out` and returns its size. Fails on flag invariants,
// id 0, malformed addresses or values, and lengths that exceed 65535 bytes.
// Does not apply the compatibility rules: see Validate().
absl::StatusOr<size_t> Encode(const Packet& packet, std::span<uint8_t> out);

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_PACKET_H_
