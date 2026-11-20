#include "src/core/uuid.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "cppitertools/enumerate.hpp"
#include "openssl/evp.h"

namespace wsc_server {
namespace {

// Canonical text form: 32 hexadecimal digits in groups of 8-4-4-4-12, with
// dashes at positions 8, 13, 18 and 23.
//
//   position  0         1         2         3
//             012345678901234567890123456789012345
//             |       |    |    |    |           |
//   example   123e4567-e89b-42d3-a456-426614174000
//                     ^    ^    ^    ^
//   dashes            8    13   18   23
constexpr size_t kTextLength = 36;
constexpr std::array<size_t, 4> kDashPositions = {8, 13, 18, 23};
constexpr bool IsDash(char c) { return c == '-'; }

// The text positions that hold hexadecimal digits: all but kDashPositions, in
// order. Element n is the position of nibble n.
constexpr size_t kDigitCount = kTextLength - kDashPositions.size();
constexpr std::array<size_t, kDigitCount> kDigitPositions = [] {
  std::array<size_t, kDigitCount> positions{};
  size_t count = 0;
  for (size_t position = 0; position < kTextLength; ++position) {
    if (!absl::c_contains(kDashPositions, position)) {
      positions[count++] = position;
    }
  }
  return positions;
}();

uint8_t HexValue(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  return static_cast<uint8_t>(absl::ascii_tolower(c) - 'a' + 10);
}

// The lower-case hexadecimal digit for `value`. Requires 0 <= value < 16.
char HexDigit(int value) {
  if (value < 10) return static_cast<char>('0' + value);
  return static_cast<char>('a' + value - 10);
}

// True when `text` (of kTextLength) has a dash at each of kDashPositions.
bool HasCanonicalDashes(std::string_view text) {
  return std::ranges::all_of(kDashPositions,
                             [text](size_t i) { return IsDash(text[i]); });
}

}  // namespace

// A version 4 UUID is 122 random bits plus 6 fixed bits that say what kind of
// UUID it is (RFC 9562). Without them the value is not a valid UUID and strict
// parsers may reject it.
//
//   byte      0        4    6    8    10
//             |        |    |    |    |
//   example   123e4567-e89b-42d3-a456-426614174000
//                           ^    ^
//                           |    variant: top 2 bits of byte 8 are 10,
//                           |    so this digit is always 8, 9, a or b
//                           version: top 4 bits of byte 6 are 0100,
//                           so this digit is always 4
Uuid Uuid::GenerateV4(absl::BitGenRef gen) {
  std::array<uint8_t, 16> bytes;
  std::ranges::generate(bytes, [&gen] { return absl::Uniform<uint8_t>(gen); });
  bytes[6] &= 0x0F;
  bytes[6] |= 0x40;  // version 4
  bytes[8] &= 0x3F;
  bytes[8] |= 0x80;  // RFC variant
  return Uuid(bytes);
}

Uuid Uuid::FromName(const Uuid& ns, std::string_view name) {
  std::vector<uint8_t> input(ns.bytes_.begin(), ns.bytes_.end());
  input.insert(input.end(), name.begin(), name.end());
  std::array<uint8_t, EVP_MAX_MD_SIZE> digest{};
  // SHA-1 cannot fail on in-memory input; RFC 9562 v5 requires SHA-1.
  CHECK(EVP_Digest(input.data(), input.size(), digest.data(), nullptr,
                   EVP_sha1(), nullptr) == 1);
  std::array<uint8_t, 16> bytes{};
  std::copy_n(digest.begin(), bytes.size(), bytes.begin());
  bytes[6] &= 0x0F;
  bytes[6] |= 0x50;  // version 5
  bytes[8] &= 0x3F;
  bytes[8] |= 0x80;  // RFC variant
  return Uuid(bytes);
}

absl::StatusOr<Uuid> Uuid::Parse(std::string_view text) {
  if (text.size() != kTextLength) {
    return absl::InvalidArgumentError("UUID must be 36 characters long");
  }
  if (!HasCanonicalDashes(text)) {
    return absl::InvalidArgumentError("UUID has a missing or misplaced '-'");
  }
  std::array<uint8_t, 16> bytes{};
  // A dash in a digit position fails the digit check.
  for (auto&& [nibble, position] : iter::enumerate(kDigitPositions)) {
    const char c = text[position];
    if (!absl::ascii_isxdigit(c)) {
      return absl::InvalidArgumentError("UUID has a non-hexadecimal digit");
    }
    uint8_t& byte = bytes[nibble / 2];
    byte <<= 4;
    byte |= HexValue(c);
  }
  return Uuid(bytes);
}

std::string Uuid::ToString() const {
  // Start with dashes everywhere, then write the 32 hexadecimal digits into
  // the positions that are not dash positions.
  std::string out(kTextLength, '-');
  for (auto&& [nibble, position] : iter::enumerate(kDigitPositions)) {
    const uint8_t byte = bytes_[nibble / 2];
    const bool high_nibble = nibble % 2 == 0;
    out[position] = HexDigit(high_nibble ? byte >> 4 : byte & 0x0F);
  }
  return out;
}

}  // namespace wsc_server
