#ifndef WSC_SERVER_SRC_CORE_APP_H_
#define WSC_SERVER_SRC_CORE_APP_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>

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
    uint16_t port = 4515;             // ws://host:port/ws; 0 = any free port
    std::string bind_address;         // empty = all interfaces
  };

  static absl::StatusOr<std::unique_ptr<App>> Create(const Options& options);

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Blocks until `stop` is requested.
  absl::Status Run(std::stop_token stop);

  [[nodiscard]] const Uuid& server_id() const { return server_id_; }

 private:
  App(const Uuid& server_id, Options options)
      : server_id_(server_id), options_(std::move(options)) {}

  Uuid server_id_;
  Options options_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_CORE_APP_H_
