#include "libwsc/error.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/cord.h"

namespace wsc {
namespace {

constexpr std::string_view kPayloadUrl = "wsc.error_code";
// Not a WSC error code; marks Drop().
constexpr uint16_t kDropCode = 0;

absl::Status WithCode(absl::Status status, uint16_t code) {
  const std::array<char, 2> bytes = {static_cast<char>(code >> 8),
                                     static_cast<char>(code & 0xFF)};
  status.SetPayload(kPayloadUrl,
                    absl::Cord(std::string_view(bytes.data(), bytes.size())));
  return status;
}

std::optional<uint16_t> RawCode(const absl::Status& status) {
  const std::optional<absl::Cord> payload = status.GetPayload(kPayloadUrl);
  if (!payload || payload->size() != 2) return std::nullopt;
  const std::string bytes(*payload);
  return static_cast<uint16_t>((static_cast<uint8_t>(bytes[0]) << 8) |
                               static_cast<uint8_t>(bytes[1]));
}

}  // namespace

absl::Status Error(ErrorCode code, std::string_view message) {
  return WithCode(absl::InvalidArgumentError(message),
                  static_cast<uint16_t>(code));
}

absl::Status Drop(std::string_view message) {
  return WithCode(absl::InvalidArgumentError(message), kDropCode);
}

std::optional<ErrorCode> GetErrorCode(const absl::Status& status) {
  const std::optional<uint16_t> code = RawCode(status);
  if (!code || *code == kDropCode) return std::nullopt;
  return static_cast<ErrorCode>(*code);
}

bool IsDrop(const absl::Status& status) { return RawCode(status) == kDropCode; }

}  // namespace wsc
