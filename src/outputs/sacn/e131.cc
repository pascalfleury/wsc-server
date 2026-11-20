#include "src/outputs/sacn/e131.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "libwsc/wire.h"

namespace wsc_server::e131 {
namespace {

constexpr std::array<uint8_t, 12> kAcnPacketId = {
    0x41, 0x53, 0x43, 0x2D, 0x45, 0x31, 0x2E, 0x31, 0x37, 0x00, 0x00, 0x00};
constexpr uint32_t kVectorRootData = 0x00000004;
constexpr uint32_t kVectorFramingData = 0x00000002;
constexpr uint8_t kVectorDmpSetProperty = 0x02;
constexpr size_t kSourceNameSize = kMaxSourceNameSize + 1;  // field size

// Offsets of the three PDU flags-and-length fields; each PDU runs to the end.
constexpr size_t kRootPdu = 16;
constexpr size_t kFramingPdu = 38;
constexpr size_t kDmpPdu = 115;

uint16_t FlagsAndLength(size_t total, size_t pdu_start) {
  return static_cast<uint16_t>(0x7000 | (total - pdu_start));
}

}  // namespace

absl::StatusOr<size_t> Encode(const DataPacket& p, std::span<uint8_t> out) {
  if (p.universe < kMinUniverse || p.universe > kMaxUniverse) {
    return absl::InvalidArgumentError(absl::StrCat("sACN universe ", p.universe,
                                                   " is not in ", kMinUniverse,
                                                   "-", kMaxUniverse));
  }
  if (p.priority > kMaxPriority) {
    return absl::InvalidArgumentError(absl::StrCat(
        "sACN priority ", p.priority, " is not in 0-", kMaxPriority));
  }
  if (p.slots.size() > kMaxSlots) {
    return absl::InvalidArgumentError(absl::StrCat(
        p.slots.size(), " DMX slots exceed the maximum of ", kMaxSlots));
  }
  if (p.source_name.size() > kMaxSourceNameSize) {
    return absl::InvalidArgumentError(
        absl::StrCat("sACN source name of ", p.source_name.size(),
                     " bytes exceeds ", kMaxSourceNameSize));
  }
  const size_t total = kHeaderSize + p.slots.size();
  if (out.size() < total) {
    return absl::ResourceExhaustedError(absl::StrCat(
        "output buffer of ", out.size(), " bytes is below ", total));
  }

  wsc::Writer w(out);
  // Root layer.
  w.U16(0x0010);  // preamble size
  w.U16(0x0000);  // post-amble size
  w.Bytes(kAcnPacketId);
  w.U16(FlagsAndLength(total, kRootPdu));
  w.U32(kVectorRootData);
  w.Bytes(p.cid.bytes());
  // Framing layer.
  w.U16(FlagsAndLength(total, kFramingPdu));
  w.U32(kVectorFramingData);
  w.Bytes(p.source_name);
  for (size_t i = p.source_name.size(); i < kSourceNameSize; ++i) w.U8(0);
  w.U8(p.priority);
  w.U16(p.sync_address);
  w.U8(p.sequence);
  w.U8(p.options);
  w.U16(p.universe);
  // DMP layer.
  w.U16(FlagsAndLength(total, kDmpPdu));
  w.U8(kVectorDmpSetProperty);
  w.U8(0xA1);     // address type and data type
  w.U16(0x0000);  // first property address
  w.U16(0x0001);  // address increment
  w.U16(static_cast<uint16_t>(1 + p.slots.size()));  // property value count
  w.U8(0x00);                                        // DMX START code
  w.Bytes(p.slots);
  return w.written();
}

std::array<uint8_t, 4> MulticastAddress(uint16_t universe) {
  return {239, 255, static_cast<uint8_t>(universe >> 8),
          static_cast<uint8_t>(universe)};
}

}  // namespace wsc_server::e131
