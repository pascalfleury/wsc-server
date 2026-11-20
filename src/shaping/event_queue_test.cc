#include "src/shaping/event_queue.h"

#include <memory>
#include <optional>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

using ::testing::Optional;

const absl::Time kStart = absl::UnixEpoch();
constexpr absl::Duration kInterval = absl::Milliseconds(10);

TEST(EventQueueTest, DrainsInOrderAtTheMinimumInterval) {
  EventQueue<int> queue(kInterval, 4);
  for (const int v : {1, 2, 3}) ASSERT_TRUE(queue.Push(v));
  EXPECT_THAT(queue.Poll(kStart), Optional(1));
  EXPECT_EQ(queue.Poll(kStart + absl::Milliseconds(5)), std::nullopt);
  EXPECT_EQ(queue.NextDeadline(), kStart + kInterval);
  EXPECT_THAT(queue.Poll(kStart + kInterval), Optional(2));
  EXPECT_THAT(queue.Poll(kStart + 2 * kInterval), Optional(3));
  EXPECT_EQ(queue.NextDeadline(), absl::InfiniteFuture());
  EXPECT_EQ(queue.Poll(kStart + absl::Hours(1)), std::nullopt);  // no repeat
}

TEST(EventQueueTest, RejectsWhenFullAndAcceptsAgainAfterDraining) {
  EventQueue<int> queue(kInterval, 2);
  EXPECT_TRUE(queue.Push(1));
  EXPECT_TRUE(queue.Push(2));
  EXPECT_FALSE(queue.Push(3));
  ASSERT_THAT(queue.Poll(kStart), Optional(1));
  EXPECT_TRUE(queue.Push(4));  // wraps around the ring
  EXPECT_THAT(queue.Poll(kStart + kInterval), Optional(2));
  EXPECT_THAT(queue.Poll(kStart + 2 * kInterval), Optional(4));
}

TEST(EventQueueTest, ZeroCapacityRejectsEverything) {
  EventQueue<int> queue(kInterval, 0);
  EXPECT_FALSE(queue.Push(1));
  EXPECT_EQ(queue.Poll(kStart), std::nullopt);
}

TEST(EventQueueTest, ReleasesSentValues) {
  EventQueue<std::shared_ptr<int>> queue(kInterval, 2);
  auto value = std::make_shared<int>(1);
  ASSERT_TRUE(queue.Push(value));
  EXPECT_EQ(value.use_count(), 2);
  queue.Poll(kStart);
  EXPECT_EQ(value.use_count(), 1);  // the ring holds no copy
}

}  // namespace
}  // namespace wsc_server
