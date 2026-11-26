#include "src/memory/frame_pool.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "absl/base/config.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/memory/frame.h"
#include "testing/fake_clock.h"

// Counts global allocations for the steady-traffic test (N-01). Not under
// ThreadSanitizer, whose runtime defines operator new/delete itself.
#ifndef ABSL_HAVE_THREAD_SANITIZER
#define WSC_COUNT_ALLOCATIONS 1
namespace {
std::atomic<uint64_t> g_allocations{0};
}  // namespace

void* operator new(size_t size) {
  ++g_allocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  std::abort();
}
void* operator new[](size_t size) { return operator new(size); }
void* operator new(size_t size, const std::nothrow_t& /*tag*/) noexcept {
  return operator new(size);
}
void* operator new[](size_t size, const std::nothrow_t& /*tag*/) noexcept {
  return operator new(size);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t /*size*/) noexcept { std::free(p); }
void operator delete[](void* p, size_t /*size*/) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t& /*tag*/) noexcept {
  std::free(p);
}
void operator delete[](void* p, const std::nothrow_t& /*tag*/) noexcept {
  std::free(p);
}
#endif  // ABSL_HAVE_THREAD_SANITIZER

namespace wsc_server {
namespace {

std::shared_ptr<Frame> AcquireOrDie(FramePool& pool, size_t size) {
  absl::StatusOr<std::shared_ptr<Frame>> frame = pool.Acquire(size);
  EXPECT_TRUE(frame.ok()) << frame.status();
  return frame.ok() ? *std::move(frame) : nullptr;
}

TEST(FramePoolTest, FrameIsWritableThenSharedReadOnly) {
  FakeClock clock;
  FramePool pool(clock);
  std::shared_ptr<Frame> frame = AcquireOrDie(pool, 100);
  ASSERT_EQ(frame->capacity(), 100u);
  EXPECT_TRUE(frame->bytes().empty());
  frame->mutable_buffer()[0] = 42;
  frame->set_size(1);
  const std::shared_ptr<const Frame> shared = std::move(frame);
  EXPECT_EQ(shared->bytes()[0], 42);
  EXPECT_EQ(pool.stats().bytes_in_use, 100u);
}

TEST(FramePoolTest, FrameIsFreeWhenLastOwnerReleases) {
  FakeClock clock;
  FramePool pool(clock);
  std::shared_ptr<const Frame> a = AcquireOrDie(pool, 100);
  std::shared_ptr<const Frame> b = a;
  a.reset();
  EXPECT_EQ(pool.stats().bytes_free, 0u);
  b.reset();
  EXPECT_EQ(pool.stats().bytes_in_use, 0u);
  EXPECT_EQ(pool.stats().bytes_free, 100u);
}

TEST(FramePoolTest, ReusesAFreeFrameWithinTheWasteLimit) {
  FakeClock clock;
  FramePool pool(clock);
  {
    std::vector<std::shared_ptr<Frame>> held;
    for (const size_t size : {300u, 100u, 200u}) {
      held.push_back(AcquireOrDie(pool, size));
    }
  }
  EXPECT_LE(AcquireOrDie(pool, 150)->capacity(), 300u);
  EXPECT_EQ(AcquireOrDie(pool, 250)->capacity(), 300u);
  EXPECT_EQ(pool.stats().allocations, 3u);
}

TEST(FramePoolTest, ResetsSizeOnReuse) {
  FakeClock clock;
  FramePool pool(clock);
  AcquireOrDie(pool, 100)->set_size(100);
  EXPECT_TRUE(AcquireOrDie(pool, 100)->bytes().empty());
}

TEST(FramePoolTest, WasteGuardAllocatesInsteadOfUsingABigFrame) {
  FakeClock clock;
  FramePool pool(clock, {.max_slot_waste = 1024});
  AcquireOrDie(pool, 4000);
  EXPECT_EQ(AcquireOrDie(pool, 100)->capacity(), 100u);
  EXPECT_EQ(pool.stats().allocations, 2u);
  EXPECT_EQ(AcquireOrDie(pool, 3000)->capacity(), 4000u);  // within 1 KiB
}

TEST(FramePoolTest, HandsOutAnIdleBigFrameOneLastTime) {
  FakeClock clock;
  FramePool pool(clock, {.slot_idle_timeout = absl::Seconds(10)});
  AcquireOrDie(pool, 4000);
  clock.Advance(absl::Seconds(11));
  std::shared_ptr<Frame> frame = AcquireOrDie(pool, 100);
  EXPECT_EQ(frame->capacity(), 4000u);
  EXPECT_EQ(pool.stats().bytes_in_use + pool.stats().bytes_free, 0u);
  frame.reset();  // freed here
  EXPECT_EQ(AcquireOrDie(pool, 100)->capacity(), 100u);
  EXPECT_EQ(pool.stats().allocations, 2u);
}

TEST(FramePoolTest, BigFrameDoesNotSurviveSteadySmallTraffic) {
  FakeClock clock;
  FramePool pool(clock, {.slot_idle_timeout = absl::Seconds(10)});
  AcquireOrDie(pool, 4000);
  for (int i = 0; i < 44 * 30; ++i) {  // 30 s of 44 Hz small frames
    clock.Advance(absl::Milliseconds(23));
    AcquireOrDie(pool, 100);
    if (i % 44 == 0) pool.Sweep();
  }
  EXPECT_EQ(pool.stats().bytes_free, 100u);
}

TEST(FramePoolTest, SweepFreesOnlyIdleFrames) {
  FakeClock clock;
  FramePool pool(clock, {.slot_idle_timeout = absl::Seconds(10)});
  AcquireOrDie(pool, 1000);
  clock.Advance(absl::Seconds(5));
  AcquireOrDie(pool, 2000);
  std::shared_ptr<Frame> held = AcquireOrDie(pool, 3000);
  clock.Advance(absl::Seconds(20));
  pool.Sweep();
  EXPECT_EQ(pool.stats().idle_evictions, 2u);
  EXPECT_EQ(pool.stats().bytes_in_use, 3000u);  // in use, never swept
  EXPECT_EQ(pool.stats().bytes_free, 0u);
}

TEST(FramePoolTest, SurplusFramesOfOneSizeAgeOut) {
  FakeClock clock;
  FramePool pool(clock, {.slot_idle_timeout = absl::Seconds(10)});
  {
    // Two frames of the same size; afterwards only one is in regular use.
    std::shared_ptr<Frame> a = AcquireOrDie(pool, 500);
    std::shared_ptr<Frame> b = AcquireOrDie(pool, 500);
  }
  for (int i = 0; i < 20; ++i) {
    clock.Advance(absl::Seconds(1));
    AcquireOrDie(pool, 500);
    pool.Sweep();
  }
  EXPECT_EQ(pool.stats().idle_evictions, 1u);
  EXPECT_EQ(pool.stats().bytes_free, 500u);
}

TEST(FramePoolTest, CapDropsFreeFramesThenFails) {
  FakeClock clock;
  FramePool pool(clock, {.max_pool_bytes = 1000});
  std::shared_ptr<Frame> held = AcquireOrDie(pool, 600);
  EXPECT_EQ(pool.Acquire(600).status().code(),
            absl::StatusCode::kResourceExhausted);
  held.reset();
  EXPECT_EQ(AcquireOrDie(pool, 900)->capacity(), 900u);  // drops the 600
  EXPECT_EQ(pool.stats().cap_evictions, 1u);
  EXPECT_FALSE(pool.Acquire(1001).ok());
}

TEST(FramePoolTest, SteadyTrafficDoesNotAllocate) {
#ifndef WSC_COUNT_ALLOCATIONS
  GTEST_SKIP() << "allocation counting is off under ThreadSanitizer";
#else
  FakeClock clock;
  FramePool pool(clock);
  auto traffic = [&pool, &clock] {
    for (const size_t size : {600u, 40u, 600u, 1500u, 40u}) {
      std::shared_ptr<Frame> frame = AcquireOrDie(pool, size);
      frame->set_size(size);
      const std::shared_ptr<const Frame> a = frame;
      const std::shared_ptr<const Frame> b = a;  // NOLINT: fan-out copy
      EXPECT_EQ(a, b);
    }
    clock.Advance(absl::Milliseconds(25));
    pool.Sweep();
  };
  traffic();  // warm-up: frames are allocated here
  const uint64_t before = g_allocations.load();
  const uint64_t frames = pool.stats().allocations;
  for (int i = 0; i < 1000; ++i) traffic();
  EXPECT_EQ(g_allocations.load(), before);
  EXPECT_EQ(pool.stats().allocations, frames);
#endif
}

}  // namespace
}  // namespace wsc_server
