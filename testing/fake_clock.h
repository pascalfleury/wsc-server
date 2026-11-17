#ifndef WSC_SERVER_TESTING_FAKE_CLOCK_H_
#define WSC_SERVER_TESTING_FAKE_CLOCK_H_

#include "absl/time/time.h"
#include "src/core/clock.h"

namespace wsc_server {

// A Clock that only moves when the test says so. Not thread-safe.
class FakeClock final : public Clock {
 public:
  explicit FakeClock(absl::Time start = absl::UnixEpoch()) : now_(start) {}

  absl::Time Now() const override { return now_; }
  void Advance(absl::Duration by) { now_ += by; }

 private:
  absl::Time now_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_TESTING_FAKE_CLOCK_H_
