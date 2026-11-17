#include "testing/fake_clock.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

TEST(FakeClockTest, MovesOnlyWhenAdvanced) {
  FakeClock clock;
  const Clock& as_clock = clock;
  EXPECT_EQ(as_clock.Now(), absl::UnixEpoch());
  clock.Advance(absl::Milliseconds(25));
  EXPECT_EQ(as_clock.Now(), absl::UnixEpoch() + absl::Milliseconds(25));
  EXPECT_EQ(as_clock.Now(), absl::UnixEpoch() + absl::Milliseconds(25));
}

}  // namespace
}  // namespace wsc_server
