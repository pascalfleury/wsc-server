#ifndef WSC_SERVER_SRC_OUTPUTS_SACN_E131_H_
#define WSC_SERVER_SRC_OUTPUTS_SACN_E131_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "absl/status/statusor.h"
#include "src/core/uuid.h"

// E1.31 (sACN) data packets, ANSI E1.31-2018 section 4.1.
namespace wsc_server::e131 {

inline constexpr uint16_t kPort = 5568;
inline constexpr size_t kMaxSlots = 512;
inline constexpr size_t kHeaderSize = 126;  // up to and including START code
inline constexpr size_t kMaxPacketSize = kHeaderSize + kMaxSlots;
inline constexpr uint16_t kMinUniverse = 1;
inline constexpr uint16_t kMaxUniverse = 63999;
inline constexpr uint8_t kMaxPriority = 200;
inline constexpr uint8_t kDefaultPriority = 100;
inline constexpr size_t kMaxSourceNameSize = 63;  // plus a terminating null

// Framing-layer option bits.
inline constexpr uint8_t kOptionPreview = 0x80;
inline constexpr uint8_t kOptionStreamTerminated = 0x40;
inline constexpr uint8_t kOptionForceSync = 0x20;

struct DataPacket {
  Uuid cid;
  std::string_view source_name;  // UTF-8, at most kMaxSourceNameSize bytes
  uint8_t priority = kDefaultPriority;  // 0 to kMaxPriority
  uint16_t sync_address = 0;            // 0 = no synchronisation
  uint8_t sequence = 0;
  uint8_t options = 0;
  uint16_t universe = kMinUniverse;  // kMinUniverse to kMaxUniverse
  std::span<const uint8_t> slots;    // DMX slots 1..n after the START code
};

// Writes the packet (START code 0x00, then the slots) to `out`; returns its
// size, kHeaderSize + slots.size().
absl::StatusOr<size_t> Encode(const DataPacket& packet, std::span<uint8_t> out);

// Multicast group of a universe: 239.255.<hi>.<lo>.
std::array<uint8_t, 4> MulticastAddress(uint16_t universe);

}  // namespace wsc_server::e131

#endif  // WSC_SERVER_SRC_OUTPUTS_SACN_E131_H_
