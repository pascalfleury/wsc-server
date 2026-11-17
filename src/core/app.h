#ifndef WSC_SERVER_SRC_CORE_APP_H_
#define WSC_SERVER_SRC_CORE_APP_H_

#include <filesystem>
#include <memory>
#include <stop_token>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/uuid.h"

namespace wsc_server {

// The server application: builds the components and runs them until asked to
// stop. OS-agnostic; foreground mode and the service wrappers only differ in
// where the stop request comes from.
class App {
 public:
  struct Options {
    std::filesystem::path state_dir;  // persistent state, created if missing
  };

  static absl::StatusOr<std::unique_ptr<App>> Create(const Options& options);

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Blocks until `stop` is requested.
  absl::Status Run(std::stop_token stop);

  const Uuid& server_id() const { return server_id_; }

 private:
  explicit App(const Uuid& server_id) : server_id_(server_id) {}

  Uuid server_id_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_CORE_APP_H_
