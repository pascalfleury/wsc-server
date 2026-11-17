#ifndef WSC_SERVER_SRC_PLATFORM_PATHS_H_
#define WSC_SERVER_SRC_PLATFORM_PATHS_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"

namespace wsc_server {

// Looks up an environment variable; std::nullopt when unset or empty.
using EnvLookup =
    absl::FunctionRef<std::optional<std::string>(std::string_view)>;

// EnvLookup backed by the process environment.
std::optional<std::string> ProcessEnv(std::string_view name);

// Returns the per-user directory for persistent state and configuration:
//   Linux    $XDG_CONFIG_HOME/wsc-server, else $HOME/.config/wsc-server
//   macOS    $HOME/Library/Application Support/wsc-server
//   Windows  %APPDATA%\wsc-server
// The directory is not created.
absl::StatusOr<std::filesystem::path> DefaultStateDir(EnvLookup env);

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_PLATFORM_PATHS_H_
