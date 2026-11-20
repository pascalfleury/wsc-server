#include "src/shaping/stream_slot.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/shaping/policy.h"

namespace wsc_server {
namespace {

constexpr RateLimits kLimits = {.min_send_interval = absl::Milliseconds(25),
                                .keepalive_interval = absl::Seconds(1)};
const absl::Time kStart = absl::UnixEpoch();

int Sent(StreamSlot<int>& slot, absl::Time now) {
  const int* value = slot.Poll(now);
  return value != nullptr ? *value : -1;  // -1: nothing sent
}

TEST(StreamSlotTest, SendsNothingBeforeTheFirstUpdate) {
  StreamSlot<int> slot(kLimits);
  EXPECT_EQ(slot.NextDeadline(), absl::InfiniteFuture());
  EXPECT_EQ(Sent(slot, kStart + absl::Hours(1)), -1);
}

TEST(StreamSlotTest, SendsTheFirstUpdateAtOnce) {
  StreamSlot<int> slot(kLimits);
  slot.Update(1);
  EXPECT_EQ(Sent(slot, kStart), 1);
}

TEST(StreamSlotTest, CoalescesUpdatesWithinTheMinimumInterval) {
  StreamSlot<int> slot(kLimits);
  slot.Update(1);
  ASSERT_EQ(Sent(slot, kStart), 1);
  slot.Update(2);
  slot.Update(3);
  EXPECT_EQ(slot.NextDeadline(), kStart + absl::Milliseconds(25));
  EXPECT_EQ(Sent(slot, kStart + absl::Milliseconds(10)), -1);
  EXPECT_EQ(Sent(slot, kStart + absl::Milliseconds(25)), 3);  // 2 never sent
}

TEST(StreamSlotTest, RepeatsTheLastValueAtTheKeepaliveInterval) {
  StreamSlot<int> slot(kLimits);
  slot.Update(7);
  ASSERT_EQ(Sent(slot, kStart), 7);
  EXPECT_EQ(slot.NextDeadline(), kStart + absl::Seconds(1));
  EXPECT_EQ(Sent(slot, kStart + absl::Milliseconds(999)), -1);
  EXPECT_EQ(Sent(slot, kStart + absl::Seconds(1)), 7);
  EXPECT_EQ(Sent(slot, kStart + absl::Seconds(2)), 7);
}

TEST(StreamSlotTest, AFreshSendRestartsTheKeepaliveInterval) {
  StreamSlot<int> slot(kLimits);
  slot.Update(1);
  ASSERT_EQ(Sent(slot, kStart), 1);
  slot.Update(2);
  ASSERT_EQ(Sent(slot, kStart + absl::Milliseconds(500)), 2);
  EXPECT_EQ(slot.NextDeadline(), kStart + absl::Milliseconds(1500));
}

TEST(StreamSlotTest, NoKeepaliveWithAZeroInterval) {
  StreamSlot<int> slot({.min_send_interval = absl::Milliseconds(25),
                        .keepalive_interval = absl::ZeroDuration()});
  slot.Update(1);
  ASSERT_EQ(Sent(slot, kStart), 1);
  EXPECT_EQ(slot.NextDeadline(), absl::InfiniteFuture());
  EXPECT_EQ(Sent(slot, kStart + absl::Hours(1)), -1);
}

TEST(PolicyTest, FollowsTheDatapathTable) {
  using enum wsc::MessageType;
  constexpr Shaping kCyclicChannels =
      ShapingFor(DeliveryModel::kCyclic, kStreamChannels);
  static_assert(kCyclicChannels.policy == ShapingPolicy::kLatestValue);
  static_assert(kCyclicChannels.keepalive);
  static_assert(!ShapingFor(DeliveryModel::kCyclic, kStreamTimecode).keepalive);
  static_assert(ShapingFor(DeliveryModel::kEvent, kControlParam).policy ==
                ShapingPolicy::kLatestValue);
  static_assert(!ShapingFor(DeliveryModel::kEvent, kControlParam).keepalive);
  static_assert(ShapingFor(DeliveryModel::kEvent, kControlCue).policy ==
                ShapingPolicy::kFifo);
  static_assert(ShapingFor(DeliveryModel::kEvent, kTunnelRaw).policy ==
                ShapingPolicy::kFifo);
  static_assert(
      ShapingFor(DeliveryModel::kTransactional, kControlParam).policy ==
      ShapingPolicy::kTransaction);
  SUCCEED();
}

}  // namespace
}  // namespace wsc_server
