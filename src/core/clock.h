#ifndef WSC_SERVER_SRC_CORE_CLOCK_H_
#define WSC_SERVER_SRC_CORE_CLOCK_H_

#include <chrono>

#include "absl/time/time.h"

namespace wsc_server {

// Source of the current time. Policy code takes a Clock instead of calling
// absl::Now(), so tests can control time (testing/fake_clock.h).
class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual absl::Time Now() const = 0;
};

// The real clock: monotonic, so timeouts and send schedules do not jump
// when the wall clock is set. Its absl::Time is time since an arbitrary
// epoch, not a date; never print or store it as one.
class SystemClock final : public Clock {
 public:
  [[nodiscard]] absl::Time Now() const override {
    return absl::UnixEpoch() +
           absl::FromChrono(
               std::chrono::steady_clock::now().time_since_epoch());
  }
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_CORE_CLOCK_H_
