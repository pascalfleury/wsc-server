#ifndef WSC_SERVER_SRC_SHAPING_POLICY_H_
#define WSC_SERVER_SRC_SHAPING_POLICY_H_

#include <cstdint>

#include "absl/time/time.h"
#include "libwsc/types.h"

namespace wsc_server {

// How a downstream protocol delivers data (D-21); declared by each adapter.
enum class DeliveryModel : uint8_t { kCyclic, kEvent, kTransactional };

enum class ShapingPolicy : uint8_t {
  kLatestValue,  // coalesce to the newest value (StreamSlot)
  kFifo,         // every message in order (EventQueue)
  kTransaction,  // one request in flight per target (RDM module, later)
};

struct RateLimits {
  absl::Duration min_send_interval;   // 1 / max rate: never send faster
  absl::Duration keepalive_interval;  // 1 / min rate; zero = no repeats
};

struct Shaping {
  ShapingPolicy policy;
  bool keepalive;  // repeat the last value at keepalive_interval
};

  // The policy table of DATAPATH.org (Rate shaping, Policies).
constexpr Shaping ShapingFor(DeliveryModel model, wsc::MessageType type) {
  using enum wsc::MessageType;

  switch (model) {
    case DeliveryModel::kCyclic:
      // Timecode that stops must look stopped: no keepalive.
      return {.policy = ShapingPolicy::kLatestValue,
        .keepalive = type == kStreamChannels};

    case DeliveryModel::kEvent:
      if (type == kControlCue || type == kTunnelRaw) {
        return {.policy = ShapingPolicy::kFifo, .keepalive = false};
      }
      return {.policy = ShapingPolicy::kLatestValue, .keepalive = false};

    case DeliveryModel::kTransactional:
      return {.policy = ShapingPolicy::kTransaction, .keepalive = false};
  }

  return {.policy = ShapingPolicy::kFifo, .keepalive = false};
}

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SHAPING_POLICY_H_
