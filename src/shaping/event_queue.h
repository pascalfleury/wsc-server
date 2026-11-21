#ifndef WSC_SERVER_SRC_SHAPING_EVENT_QUEUE_H_
#define WSC_SERVER_SRC_SHAPING_EVENT_QUEUE_H_

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "absl/time/time.h"

namespace wsc_server {

// Bounded FIFO of discrete events (cues, notes, tunnels; DATAPATH.org,
// kFifo), drained at most once per min_send_interval. Events are never
// coalesced or repeated. The ring is allocated once, at construction. Not
// thread-safe: lives on its adapter's strand.
template <class Value>
class EventQueue {
 public:
  EventQueue(absl::Duration min_send_interval, size_t capacity)
      : min_send_interval_(min_send_interval), ring_(capacity) {}

  // Appends an event; false when the queue is full (the caller answers the
  // originator with STATE_ERROR).
  bool Push(Value value) {
    if (size_ == ring_.size()) return false;
    ring_[(head_ + size_) % ring_.size()] = std::move(value);
    ++size_;
    return true;
  }

  // The oldest event when one is due, else nullopt.
  std::optional<Value> Poll(absl::Time now) {
    if (now < NextDeadline()) return std::nullopt;
    Value value = std::move(ring_[head_]);
    ring_[head_] = Value{};  // drop the reference held by the ring
    head_ = (head_ + 1) % ring_.size();
    --size_;
    last_sent_ = now;
    return value;
  }

  // When Poll() returns the next event; InfiniteFuture when empty.
  [[nodiscard]] absl::Time NextDeadline() const {
    if (size_ == 0) return absl::InfiniteFuture();
    return last_sent_ + min_send_interval_;
  }

  [[nodiscard]] size_t size() const { return size_; }

 private:
  absl::Duration min_send_interval_;
  std::vector<Value> ring_;
  size_t head_ = 0;
  size_t size_ = 0;
  absl::Time last_sent_ = absl::InfinitePast();
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SHAPING_EVENT_QUEUE_H_
