#include "src/platform/paths.h"

#include <cstdlib>

#include "absl/status/status.h"

namespace wsc_server {
namespace {

constexpr std::string_view kAppDir = "wsc-server";

}  // namespace

std::optional<std::string> ProcessEnv(std::string_view name) {
  const std::string key(name);
  const char* value = std::getenv(key.c_str());
  if (value == nullptr || *value == '\0') return std::nullopt;
  return std::string(value);
}

absl::StatusOr<std::filesystem::path> DefaultStateDir(EnvLookup env) {
#if defined(_WIN32)
  if (const auto appdata = env("APPDATA")) {
    return std::filesystem::path(*appdata) / kAppDir;
  }
  return absl::NotFoundError("APPDATA is not set");
#elif defined(__APPLE__)
  if (const auto home = env("HOME")) {
    return std::filesystem::path(*home) / "Library" / "Application Support" /
           kAppDir;
  }
  return absl::NotFoundError("HOME is not set");
#else
  // The XDG base directory spec says to ignore relative paths.
  if (const auto xdg = env("XDG_CONFIG_HOME");
      xdg && std::filesystem::path(*xdg).is_absolute()) {
    return std::filesystem::path(*xdg) / kAppDir;
  }
  if (const auto home = env("HOME")) {
    return std::filesystem::path(*home) / ".config" / kAppDir;
  }
  return absl::NotFoundError("neither XDG_CONFIG_HOME nor HOME is set");
#endif
}

}  // namespace wsc_server
