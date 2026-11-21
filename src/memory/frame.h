#ifndef WSC_SERVER_SRC_MEMORY_FRAME_H_
#define WSC_SERVER_SRC_MEMORY_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "absl/time/time.h"

namespace wsc_server {

// A byte buffer holding one packet. The owner fills it through
// mutable_buffer() and set_size(), then shares it read-only as
// std::shared_ptr<const Frame>.
class Frame {
 public:
  explicit Frame(size_t capacity)
      // NOLINTNEXTLINE(modernize-avoid-c-arrays): a run-time sized buffer
      : buffer_(std::make_unique<uint8_t[]>(capacity)), capacity_(capacity) {}

  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  [[nodiscard]] std::span<const uint8_t> bytes() const {
    return {buffer_.get(), size_};
  }
  [[nodiscard]] size_t capacity() const { return capacity_; }

  std::span<uint8_t> mutable_buffer() { return {buffer_.get(), capacity_}; }
  // Sets the filled length, clamped to the capacity.
  void set_size(size_t size) { size_ = size < capacity_ ? size : capacity_; }

 private:
  friend class FramePool;

  std::unique_ptr<uint8_t[]> buffer_;  // NOLINT(modernize-avoid-c-arrays)
  size_t capacity_;
  size_t size_ = 0;
  absl::Time last_acquired_ = absl::InfinitePast();  // set by FramePool
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_MEMORY_FRAME_H_
