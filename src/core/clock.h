#ifndef WSC_SERVER_SRC_CORE_CLOCK_H_
#define WSC_SERVER_SRC_CORE_CLOCK_H_

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace wsc_server {

// Source of the current time. Policy code takes a Clock instead of calling
// absl::Now(), so tests can control time (testing/fake_clock.h).
class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual absl::Time Now() const = 0;
};

// The real wall clock.
class SystemClock final : public Clock {
 public:
  [[nodiscard]] absl::Time Now() const override { return absl::Now(); }
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_CORE_CLOCK_H_
