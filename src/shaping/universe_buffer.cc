#include "src/shaping/universe_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "libwsc/error.h"

namespace wsc_server {

absl::Status UniverseBuffer::Apply(uint16_t start_channel,
                                   std::span<const uint8_t> values) {
  if (values.empty()) return absl::OkStatus();
  const size_t last = size_t{start_channel} + values.size() - 1;
  if (start_channel < 1 || last > kChannels) {
    return wsc::Error(wsc::ErrorCode::kValidationFailed,
                      absl::StrCat("channels ", start_channel, "-", last,
                                   " are outside 1-", kChannels));
  }
  std::ranges::copy(values, channels_.begin() + (start_channel - 1));
  extent_ = std::max(extent_, last);
  return absl::OkStatus();
}

}  // namespace wsc_server
