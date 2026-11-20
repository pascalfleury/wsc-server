#ifndef WSC_SERVER_SRC_MEMORY_FRAME_POOL_H_
#define WSC_SERVER_SRC_MEMORY_FRAME_POOL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "src/core/clock.h"
#include "src/memory/frame.h"

namespace wsc_server {

struct FramePoolOptions {
  // A free frame bigger than the request by more than this is not reused; a
  // frame of the requested size is allocated instead.
  size_t max_slot_waste = 1024;
  // Free frames not acquired for longer than this are freed by Sweep(), and
  // may be reused for any smaller request.
  absl::Duration slot_idle_timeout = absl::Seconds(10);
  // Cap on the capacity of all pooled frames (frames on their last use no
  // longer count).
  size_t max_pool_bytes = size_t{64} << 20;
};

struct FramePoolStats {
  uint64_t allocations = 0;
  uint64_t idle_evictions = 0;
  uint64_t cap_evictions = 0;
  size_t bytes_in_use = 0;
  size_t bytes_free = 0;
};

// Reuses Frames (DATAPATH.org, D-26). The pool keeps a shared_ptr to every
// frame; a frame is free when the pool holds the only reference
// (use_count() == 1). Do not keep weak_ptrs to pooled frames: locking one
// after release would share a frame that is being reused. Thread-safe.
class FramePool {
 public:
  explicit FramePool(const Clock& clock, FramePoolOptions options = {});

  FramePool(const FramePool&) = delete;
  FramePool& operator=(const FramePool&) = delete;

  // A Frame with capacity >= `size` and size 0: the most recently acquired
  // free frame that fits and either wastes at most max_slot_waste or is idle,
  // else a new frame. An idle frame that wastes more is handed out one last
  // time and leaves the pool. Fails with ResourceExhausted when the memory cap
  // leaves no room even after dropping free frames.
  absl::StatusOr<std::shared_ptr<Frame>> Acquire(size_t size);

  // Frees free frames idle for longer than slot_idle_timeout. Call about once
  // a second.
  void Sweep();

  FramePoolStats stats() const;

 private:
  // Time since `frame` was last acquired.
  static absl::Duration FrameAge(const Frame& frame, absl::Time now) {
    return now - frame.last_acquired_;
  }
  // Bytes left unused when `frame` holds `size` bytes; needs capacity >= size.
  static size_t FrameWaste(const Frame& frame, size_t size) {
    return frame.capacity_ - size;
  }

  // Drops free frames until `size` more bytes fit under max_pool_bytes.
  bool MakeRoom(size_t size) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const Clock& clock_;
  const FramePoolOptions options_;

  mutable absl::Mutex mutex_;
  // Recently acquired frames at the back; idle ones gather at the front.
  std::vector<std::shared_ptr<Frame>> frames_ ABSL_GUARDED_BY(mutex_);
  FramePoolStats stats_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_MEMORY_FRAME_POOL_H_
