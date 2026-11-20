#ifndef WSC_SERVER_LIBWSC_ERROR_H_
#define WSC_SERVER_LIBWSC_ERROR_H_

#include <optional>
#include <string_view>

#include "absl/status/status.h"
#include "libwsc/types.h"

namespace wsc {

// A failed status carrying the WSC error code to send back in STATE_ERROR.
absl::Status Error(ErrorCode code, std::string_view message);

// A failed status for input that must be dropped silently (spec 07: buffer
// shorter than 19 bytes, bad magic).
absl::Status Drop(std::string_view message);

// The WSC error code of a status made by Error(), if any.
std::optional<ErrorCode> GetErrorCode(const absl::Status& status);

// True for a status made by Drop().
bool IsDrop(const absl::Status& status);

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_ERROR_H_
