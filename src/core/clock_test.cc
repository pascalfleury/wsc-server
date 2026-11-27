#include "src/core/clock.h"

#include <chrono>

#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

absl::Time SteadyNow() {
  return absl::UnixEpoch() +
         absl::FromChrono(std::chrono::steady_clock::now().time_since_epoch());
}

// Timeouts and send schedules must not move when NTP or an operator sets the
// wall clock, so SystemClock reads the monotonic clock.
TEST(SystemClockTest, FollowsTheMonotonicClockNotWallTime) {
  const SystemClock clock;
  const absl::Time before = SteadyNow();
  const absl::Time now = clock.Now();
  const absl::Time after = SteadyNow();
  EXPECT_GE(now, before);
  EXPECT_LE(now, after);
}

}  // namespace
}  // namespace wsc_server
