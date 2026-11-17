#include <cstdlib>
#include <filesystem>
#include <stop_token>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/app.h"
#include "src/platform/foreground.h"
#include "src/platform/paths.h"

ABSL_FLAG(std::string, state_dir, "",
          "Directory for persistent state (server ID, configuration). "
          "Default: the per-user configuration directory.");

namespace wsc_server {
namespace {

absl::Status Main() {
  std::filesystem::path state_dir = absl::GetFlag(FLAGS_state_dir);
  if (state_dir.empty()) {
    auto default_dir = DefaultStateDir(ProcessEnv);
    if (!default_dir.ok()) return default_dir.status();
    state_dir = *std::move(default_dir);
  }

  const auto app = App::Create({.state_dir = state_dir});
  if (!app.ok()) return app.status();
  return RunInForeground(
      [&app](std::stop_token stop) { return (*app)->Run(stop); });
}

}  // namespace
}  // namespace wsc_server

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage("Web Show Control (WSC) gateway server.");
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  const absl::Status status = wsc_server::Main();
  if (status.ok()) return EXIT_SUCCESS;

  LOG(ERROR) << status;
  return EXIT_FAILURE;
}
