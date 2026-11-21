#ifndef WSC_SERVER_SRC_OUTPUTS_SACN_SACN_STREAMS_H_
#define WSC_SERVER_SRC_OUTPUTS_SACN_SACN_STREAMS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "libwsc/packet.h"
#include "src/core/uuid.h"
#include "src/outputs/datagram_sender.h"
#include "src/outputs/sacn/e131.h"
#include "src/shaping/policy.h"
#include "src/shaping/stream_slot.h"
#include "src/shaping/universe_buffer.h"

namespace wsc_server {

struct SacnConfig {
  Uuid cid;  // SacnCid(); one per output (D-28)
  std::string source_name = "wsc-server";
  // 44 Hz at most, repeated at least every second (ADAPTERS.org).
  RateLimits limits = {.min_send_interval = absl::Seconds(1) / 44,
                       .keepalive_interval = absl::Seconds(1)};
  bool partial_dmx = false;  // F-27
};

// The CID of an sACN output: name-based UUID of "sacn-output/<output id>" in
// the server ID namespace (D-28).
Uuid SacnCid(const Uuid& server_id, std::string_view output_id);

// The streams of one sACN output: per (destination, universe) a universe
// buffer and a latest-value slot (DATAPATH.org). Pure logic: the caller
// supplies the time and runs Tick(). Not thread-safe: lives on a strand.
class SacnStreams {
 public:
  SacnStreams(SacnConfig config, DatagramSender& sender)
      : config_(std::move(config)), sender_(sender) {}

  // Applies a STREAM_CHANNELS packet with an sACN Transport Descriptor.
  // Failures carry a WSC error code (VALIDATION_FAILED).
  absl::Status Apply(const wsc::Packet& packet);

  // Sends every stream that is due; returns the next deadline
  // (InfiniteFuture when no stream will be due).
  absl::Time Tick(absl::Time now);

  [[nodiscard]] size_t stream_count() const { return streams_.size(); }

 private:
  struct Key {
    Ipv4Endpoint destination;
    uint16_t universe;

    friend bool operator==(const Key&, const Key&) = default;
    template <typename H>
    friend H AbslHashValue(H h, const Key& k) {
      return H::combine(std::move(h), k.destination, k.universe);
    }
  };
  struct Channels {
    std::array<uint8_t, UniverseBuffer::kChannels> data{};
    size_t size = 0;
  };
  struct Stream {
    explicit Stream(RateLimits limits) : slot(limits) {}

    UniverseBuffer buffer;
    StreamSlot<Channels> slot;
    uint8_t priority = e131::kDefaultPriority;
    uint8_t sequence = 0;
  };

  void Send(const Key& key, Stream& stream, const Channels& channels);

  SacnConfig config_;
  DatagramSender& sender_;
  absl::flat_hash_map<Key, Stream> streams_;
  std::array<uint8_t, e131::kMaxPacketSize> packet_{};  // encode scratch
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_OUTPUTS_SACN_SACN_STREAMS_H_
