#ifndef WSC_SERVER_SRC_PLATFORM_FOREGROUND_H_
#define WSC_SERVER_SRC_PLATFORM_FOREGROUND_H_

#include <stop_token>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"

namespace wsc_server {

// Foreground (non-service) mode: runs `run` on the calling thread and requests
// stop on its token when the process receives SIGINT or SIGTERM. Returns what
// `run` returns.
absl::Status RunInForeground(
    absl::FunctionRef<absl::Status(std::stop_token)> run);

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_PLATFORM_FOREGROUND_H_
