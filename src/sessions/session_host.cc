#include "src/sessions/session_host.h"

#include <memory>
#include <utility>

#include "absl/time/time.h"
#include "src/platform/asio.h"

namespace wsc_server {
namespace {

constexpr absl::Duration kTimeoutCheck = absl::Seconds(1);

}  // namespace

SessionHost::SessionHost(std::unique_ptr<SessionTransport> transport,
                         const asio::any_io_executor& executor,
                         const Router& router, FramePool& pool,
                         const Clock& clock)
    : transport_(std::move(transport)),
      strand_(asio::make_strand(executor)),
      timer_(strand_),
      session_(*transport_, router, pool, clock) {}

void SessionHost::Start() {
  if (started_.exchange(true)) return;
  asio::post(strand_, [self = shared_from_this()] {
    self->session_.ResetTimeout();
    self->Arm();
  });
}

void SessionHost::Receive(std::shared_ptr<const Frame> frame) {
  asio::post(strand_, [self = shared_from_this(), frame = std::move(frame)] {
    self->session_.OnFrame(frame);
  });
}

void SessionHost::Stop() {
  asio::post(strand_, [self = shared_from_this()] { self->timer_.cancel(); });
}

void SessionHost::Arm() {
  timer_.expires_after(absl::ToChronoNanoseconds(kTimeoutCheck));
  timer_.async_wait([self = shared_from_this()](const asio::error_code& ec) {
    if (ec) return;  // stopped
    if (!self->session_.CloseIfTimedOut()) self->Arm();
  });
}

}  // namespace wsc_server
