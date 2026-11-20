#ifndef WSC_SERVER_LIBWSC_ADDRESS_H_
#define WSC_SERVER_LIBWSC_ADDRESS_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

// WSC addresses (spec 03): a token sequence closed by 0x00.
namespace wsc {

namespace token {
inline constexpr uint8_t kNone = 0x00;
inline constexpr uint8_t kIndex = 0x80;
inline constexpr uint8_t kCustom = 0xFE;
}  // namespace token

struct Segment {
  enum class Kind : uint8_t { kToken, kIndex, kCustom };

  static Segment Token(uint8_t value) { return {Kind::kToken, value, 0, {}}; }
  static Segment Index(uint16_t value) { return {Kind::kIndex, 0, value, {}}; }
  static Segment Custom(std::string_view text) {
    return {Kind::kCustom, 0, 0, text};
  }

  Kind kind;
  uint8_t token;          // kToken
  uint16_t index;         // kIndex
  std::string_view text;  // kCustom; views the address bytes when decoded

  friend bool operator==(const Segment&, const Segment&) = default;
};

using Segments = absl::InlinedVector<Segment, 8>;

// Checks the wire form: every segment complete and the 0x00 terminator as the
// last byte. Unknown tokens are accepted (spec 03).
absl::Status CheckAddress(std::span<const uint8_t> bytes);

// Splits a wire address into segments. Two consecutive INDEX sentinels are one
// u16 index, as spec 03's parsing rule says.
absl::StatusOr<Segments> ParseAddress(std::span<const uint8_t> bytes);

// Writes the segments and the terminator to `out`; returns the byte count.
// Refuses an index directly followed by another index: those bytes would read
// back as one u16 index (CODEC.org, index ambiguity).
absl::StatusOr<size_t> EncodeAddress(std::span<const Segment> segments,
                                     std::span<uint8_t> out);

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_ADDRESS_H_
