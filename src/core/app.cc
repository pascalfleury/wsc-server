#include "src/core/app.h"

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "src/core/identity.h"
#include "src/platform/asio.h"

namespace wsc_server {

absl::StatusOr<std::unique_ptr<App>> App::Create(const Options& options) {
  absl::BitGen gen;
  const auto server_id = LoadOrCreateServerId(options.state_dir, gen);
  if (!server_id.ok()) return server_id.status();
  return absl::WrapUnique(new App(*server_id));
}

absl::Status App::Run(std::stop_token stop) {
  asio::io_context io;
  const auto work = asio::make_work_guard(io);
  // io_context::stop() is thread-safe; if stop was already requested the
  // callback runs here and run() returns at once.
  const std::stop_callback on_stop(stop, [&io] { io.stop(); });

  LOG(INFO) << "wsc-server " << server_id_.ToString() << " running";
  io.run();
  LOG(INFO) << "wsc-server stopped";
  return absl::OkStatus();
}

}  // namespace wsc_server
