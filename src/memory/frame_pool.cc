#include "src/memory/frame_pool.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace wsc_server {
namespace {

// True when the pool holds the only reference. use_count() is a relaxed load;
// the fence orders our reuse after the last owner's release.
bool IsFree(const std::shared_ptr<Frame>& frame) {
  if (frame.use_count() != 1) return false;
  std::atomic_thread_fence(std::memory_order_acquire);
  return true;
}

}  // namespace

FramePool::FramePool(const Clock& clock, FramePoolOptions options)
    : clock_(clock), options_(options) {}

absl::StatusOr<std::shared_ptr<Frame>> FramePool::Acquire(size_t size) {
  const absl::Time now = clock_.Now();
  absl::MutexLock lock(mutex_);

  // Most recently acquired first, so surplus frames age out at the front.
  auto reusable = [&](const std::shared_ptr<Frame>& frame) {
    return frame->capacity_ >= size && IsFree(frame) &&
           (FrameWaste(*frame, size) <= options_.max_slot_waste ||
            FrameAge(*frame, now) > options_.slot_idle_timeout);
  };
  const auto found =
      std::ranges::find_if(frames_ | std::views::reverse, reusable);
  if (found != std::ranges::rend(frames_)) {
    const auto it = std::prev(found.base());
    if (FrameWaste(**it, size) > options_.max_slot_waste) {
      // Idle and too big: hand it out one last time, unpooled, so it is freed
      // after this use instead of staying young by being reused.
      std::shared_ptr<Frame> last_use = std::move(*it);
      frames_.erase(it);
      last_use->size_ = 0;
      return last_use;
    }
    std::rotate(it, it + 1, frames_.end());  // to the back, no allocation
  } else {
    if (!MakeRoom(size)) {
      return absl::ResourceExhaustedError("frame pool memory cap reached");
    }
    ++stats_.allocations;
    frames_.push_back(std::make_shared<Frame>(size));
  }
  Frame& frame = *frames_.back();
  frame.last_acquired_ = now;
  frame.size_ = 0;
  return frames_.back();
}

void FramePool::Sweep() {
  const absl::Time now = clock_.Now();
  absl::MutexLock lock(mutex_);
  stats_.idle_evictions += std::erase_if(frames_, [&](const auto& frame) {
    return FrameAge(*frame, now) > options_.slot_idle_timeout && IsFree(frame);
  });
}

bool FramePool::MakeRoom(size_t size) {
  size_t total = 0;
  for (const auto& frame : frames_) total += frame->capacity_;
  // Drop free frames, least recently acquired first, until `size` fits.
  for (auto it = frames_.begin();
       total + size > options_.max_pool_bytes && it != frames_.end();) {
    if (IsFree(*it)) {
      total -= (*it)->capacity_;
      it = frames_.erase(it);
      ++stats_.cap_evictions;
    } else {
      ++it;
    }
  }
  return total + size <= options_.max_pool_bytes;
}

FramePoolStats FramePool::stats() const {
  absl::MutexLock lock(mutex_);
  FramePoolStats stats = stats_;
  for (const auto& frame : frames_) {
    (IsFree(frame) ? stats.bytes_free : stats.bytes_in_use) += frame->capacity_;
  }
  return stats;
}

}  // namespace wsc_server
