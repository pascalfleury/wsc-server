#ifndef WSC_SERVER_SRC_SHAPING_STREAM_SLOT_H_
#define WSC_SERVER_SRC_SHAPING_STREAM_SLOT_H_

#include <algorithm>
#include <utility>

#include "absl/time/time.h"
#include "src/shaping/policy.h"

namespace wsc_server {

// Double-buffered latest-value slot of one output stream (DATAPATH.org,
// kLatestValue). Updates between two sends are coalesced: only the newest is
// sent. With a keepalive interval, the last sent value is repeated when
// nothing new arrives. Not thread-safe: lives on its adapter's strand.
template <class Value>
class StreamSlot {
 public:
  explicit StreamSlot(RateLimits limits) : limits_(limits) {}

  // Stores the newest value; it is sent at the next due Poll().
  void Update(Value value) {
    back_ = std::move(value);
    dirty_ = true;
  }

  // The value to send now, or nullptr when nothing is due. The returned value
  // stays valid until the next Update() or Poll().
  const Value* Poll(absl::Time now) {
    if (now < NextDeadline()) return nullptr;
    if (dirty_) {
      std::swap(front_, back_);
      dirty_ = false;
      has_front_ = true;
    }
    last_sent_ = now;
    return &front_;
  }

  // When Poll() has something to send next; InfiniteFuture when nothing will
  // be due until the next Update().
  [[nodiscard]] absl::Time NextDeadline() const {
    if (dirty_) return last_sent_ + limits_.min_send_interval;
    // A zero keepalive interval means no repeats at all: falls through to
    // InfiniteFuture() below.
    if (has_front_ && limits_.keepalive_interval > absl::ZeroDuration()) {
      return last_sent_ + limits_.keepalive_interval;
    }
    return absl::InfiniteFuture();
  }

 private:
  RateLimits limits_;
  Value back_{};   // newest received
  Value front_{};  // last sent; repeated for keepalive
  bool dirty_ = false;
  bool has_front_ = false;
  absl::Time last_sent_ = absl::InfinitePast();
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SHAPING_STREAM_SLOT_H_
