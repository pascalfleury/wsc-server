#ifndef WSC_SERVER_SRC_CORE_UUID_H_
#define WSC_SERVER_SRC_CORE_UUID_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/statusor.h"

namespace wsc_server {

// A 128-bit UUID value (RFC 9562).
class Uuid {
 public:
  // Returns a random (version 4) UUID drawn from `gen`.
  static Uuid GenerateV4(absl::BitGenRef gen);

  // Parses the canonical 8-4-4-4-12 hexadecimal form, in either case.
  static absl::StatusOr<Uuid> Parse(std::string_view text);

  // Returns the canonical lower-case form.
  std::string ToString() const;

  const std::array<uint8_t, 16>& bytes() const { return bytes_; }

  friend bool operator==(const Uuid&, const Uuid&) = default;

 private:
  explicit Uuid(const std::array<uint8_t, 16>& bytes) : bytes_(bytes) {}

  std::array<uint8_t, 16> bytes_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_CORE_UUID_H_
