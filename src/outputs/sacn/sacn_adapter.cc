#include "src/outputs/sacn/sacn_adapter.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "src/platform/asio.h"

namespace wsc_server {

SacnAdapter::SacnAdapter(const asio::any_io_executor& executor,
                         const Clock& clock, SacnConfig config,
                         DatagramSender& sender)
    : strand_(asio::make_strand(executor)),
      timer_(strand_),
      clock_(clock),
      streams_(std::move(config), sender) {}

void SacnAdapter::Submit(Message message) {
  asio::post(strand_, [this, message = std::move(message)] {
    const absl::Status status = streams_.Apply(message.packet);
    Report(message, status);
    if (status.ok()) Tick();
  });
}

void SacnAdapter::Tick() {
  const absl::Time now = clock_.Now();
  const absl::Time next = streams_.Tick(now);
  if (next == armed_for_) return;  // the armed timer already fits
  armed_for_ = next;
  if (next == absl::InfiniteFuture()) {
    timer_.cancel();
    return;
  }
  timer_.expires_after(
      absl::ToChronoNanoseconds(std::max(next - now, absl::ZeroDuration())));
  timer_.async_wait([this](const asio::error_code& ec) {
    if (ec) return;  // cancelled or re-armed
    armed_for_ = absl::InfiniteFuture();
    Tick();
  });
}

}  // namespace wsc_server
