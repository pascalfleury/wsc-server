#ifndef WSC_SERVER_SRC_SESSIONS_WS_SERVER_H_
#define WSC_SERVER_SRC_SESSIONS_WS_SERVER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "src/core/clock.h"
#include "src/memory/frame_pool.h"
#include "src/platform/asio.h"
#include "src/routing/router.h"

namespace wsc_server {

// The WebSocket endpoint ws://host:port/ws (F-13), on libdatachannel's C API.
// Each connection that sends binary messages is a plain WebSocket WSC session:
// one WSC packet per message. libdatachannel calls in from its own threads;
// each connection's Session runs on its own strand.
class WsServer {
 public:
  struct Options {
    uint16_t port = 4515;      // the upstream gateway's port; 0 = any free one
    std::string bind_address;  // empty = all interfaces
  };

  static absl::StatusOr<std::unique_ptr<WsServer>> Create(
      const Options& options, const asio::any_io_executor& executor,
      const Router& router, FramePool& pool, const Clock& clock);

  // Stops accepting and closes every connection. Posted session work may
  // still run on the executor; the router, pool and clock must outlive it.
  ~WsServer();

  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  [[nodiscard]] uint16_t port() const;
  [[nodiscard]] size_t connection_count() const;

 private:
  class Connection;

  WsServer(asio::any_io_executor executor, const Router& router,
           FramePool& pool, const Clock& clock)
      : executor_(std::move(executor)),
        router_(router),
        pool_(pool),
        clock_(clock) {}

  // libdatachannel callbacks; `ptr` is the WsServer.
  static void OnClient(int server, int ws, void* ptr);
  static void OnOpen(int ws, void* ptr);
  static void OnClosed(int ws, void* ptr);
  static void OnMessage(int ws, const char* data, int size, void* ptr);

  std::shared_ptr<Connection> Find(int ws) const;

  asio::any_io_executor executor_;
  const Router& router_;
  FramePool& pool_;
  const Clock& clock_;
  int server_ = -1;

  mutable absl::Mutex mutex_;
  // Every socket that is not deleted yet. Whoever removes an entry deletes
  // its socket: OnClosed, or the destructor, which so waits for callbacks
  // still using this object.
  absl::flat_hash_map<int, std::shared_ptr<Connection>> connections_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SESSIONS_WS_SERVER_H_
