#ifndef WSC_SERVER_SRC_SESSIONS_SESSION_HOST_H_
#define WSC_SERVER_SRC_SESSIONS_SESSION_HOST_H_

#include <atomic>
#include <memory>

#include "src/core/clock.h"
#include "src/memory/frame.h"
#include "src/memory/frame_pool.h"
#include "src/platform/asio.h"
#include "src/routing/router.h"
#include "src/sessions/session.h"

namespace wsc_server {

// Runs one Session on its own strand, with a once-a-second timeout check.
// Owns the session's transport. Receive() and Stop() may be called from any
// thread.
class SessionHost : public std::enable_shared_from_this<SessionHost> {
 public:
  SessionHost(std::unique_ptr<SessionTransport> transport,
              const asio::any_io_executor& executor, const Router& router,
              FramePool& pool, const Clock& clock);

  // Starts the timeout checks; later calls do nothing.
  void Start();
  void Receive(std::shared_ptr<const Frame> frame);
  // Stops the timer; the last posted work then releases the host.
  void Stop();

  [[nodiscard]] SessionTransport& transport() { return *transport_; }

 private:
  void Arm();

  std::unique_ptr<SessionTransport> transport_;  // before session_: it uses it
  std::atomic<bool> started_ = false;
  asio::strand<asio::any_io_executor> strand_;
  asio::steady_timer timer_;
  Session session_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SESSIONS_SESSION_HOST_H_
