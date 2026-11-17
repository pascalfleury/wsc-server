#include "src/platform/foreground.h"

#include <csignal>
#include <stop_token>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

TEST(RunInForegroundTest, ReturnsTheStatusOfRun) {
  const absl::Status status = RunInForeground(
      [](std::stop_token) { return absl::AbortedError("from run"); });
  EXPECT_EQ(status, absl::AbortedError("from run"));
}

TEST(RunInForegroundTest, DoesNotRequestStopWithoutASignal) {
  bool stop_requested = true;
  const absl::Status status =
      RunInForeground([&stop_requested](std::stop_token stop) {
        stop_requested = stop.stop_requested();
        return absl::OkStatus();
      });
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_FALSE(stop_requested);
}

TEST(RunInForegroundTest, RequestsStopOnSigterm) {
  bool stopped = false;
  const absl::Status status = RunInForeground([&stopped](std::stop_token stop) {
    absl::Notification notified;
    const std::stop_callback on_stop(stop, [&notified] { notified.Notify(); });
    std::raise(SIGTERM);
    stopped = notified.WaitForNotificationWithTimeout(absl::Seconds(10));
    return absl::OkStatus();
  });
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_TRUE(stopped);
}

}  // namespace
}  // namespace wsc_server
