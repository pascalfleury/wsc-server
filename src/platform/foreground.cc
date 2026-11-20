#include "src/platform/foreground.h"

#include <csignal>
#include <thread>

#include "absl/strings/str_cat.h"
#include "src/platform/asio.h"

namespace wsc_server {

absl::Status RunInForeground(
    absl::FunctionRef<absl::Status(std::stop_token)> run) {
  std::stop_source stop;
  asio::io_context io;
  asio::signal_set signals(io);
  for (const int signal : {SIGINT, SIGTERM}) {
    asio::error_code ec;
    if (signals.add(signal, ec)) {
      return absl::InternalError(
          absl::StrCat("cannot handle signal ", signal, ": ", ec.message()));
    }
  }
  signals.async_wait([&stop](const asio::error_code& ec, int /*signal*/) {
    if (!ec) stop.request_stop();
  });

  // Destroyed (joined) before `signals` and `io`, which it uses.
  std::jthread waiter([&io] { io.run(); });
  absl::Status status = run(stop.get_token());
  io.stop();
  return status;
}

}  // namespace wsc_server
