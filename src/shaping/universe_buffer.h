#ifndef WSC_SERVER_SRC_SHAPING_UNIVERSE_BUFFER_H_
#define WSC_SERVER_SRC_SHAPING_UNIVERSE_BUFFER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/status/status.h"

namespace wsc_server {

// One source's view of a DMX-family universe (DATAPATH.org, F-27). Partial
// STREAM_CHANNELS slices are written at their start channel; channels never
// written stay 0.
class UniverseBuffer {
 public:
  static constexpr size_t kChannels = 512;

  // Writes `values` to channels start_channel .. start_channel + size - 1
  // (1-based). Fails with VALIDATION_FAILED when the slice leaves the
  // universe; an empty slice is a no-op.
  absl::Status Apply(uint16_t start_channel, std::span<const uint8_t> values);

  // The highest channel written so far; 0 when nothing was written.
  [[nodiscard]] size_t extent() const { return extent_; }

  // The channels to send: all kChannels by default, or with `partial_dmx`
  // channels 1 up to extent() (sACN, Art-Net and DMX512 accept short frames
  // but no start offset).
  [[nodiscard]] std::span<const uint8_t> Frame(bool partial_dmx) const {
    return std::span(channels_).first(partial_dmx ? extent_ : kChannels);
  }

 private:
  std::array<uint8_t, kChannels> channels_{};
  size_t extent_ = 0;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SHAPING_UNIVERSE_BUFFER_H_
