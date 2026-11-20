// libFuzzer target for wsc::Decode. Property: a packet that decodes re-encodes
// to the same bytes, and Validate() never crashes on it.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "absl/status/statusor.h"
#include "libwsc/packet.h"
#include "libwsc/validate.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::span<const uint8_t> bytes(data, size);
  const absl::StatusOr<wsc::Packet> packet = wsc::Decode(bytes);
  if (!packet.ok()) return 0;
  (void)wsc::Validate(*packet);

  std::vector<uint8_t> out(size);
  const absl::StatusOr<size_t> written = wsc::Encode(*packet, out);
  if (!written.ok() || *written != size ||
      std::memcmp(out.data(), data, size) != 0) {
    std::abort();
  }
  return 0;
}
