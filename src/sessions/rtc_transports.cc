#include "src/sessions/rtc_transports.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "libwsc/packet.h"
#include "libwsc/types.h"
#include "rtc/rtc.h"

namespace wsc_server {
namespace {

constexpr std::string_view kLabelPrefix = "WSC!DC";
constexpr size_t kTypeOffset = 7;  // header: magic(4) version(3)
// Spec 06 send path: above this many queued bytes a packet is dropped rather
// than queued, so latency cannot build up behind a slow peer.
constexpr int kMaxBufferedBytes = 64 * 1024;

// Sends one binary message on a libdatachannel WebSocket or data channel.
void SendOn(int id, std::span<const uint8_t> packet) {
  if (rtcGetBufferedAmount(id) > kMaxBufferedBytes) {
    LOG_EVERY_N_SEC(WARNING, 5)
        << "channel " << id << ": send buffer full, packet dropped";
    return;
  }
  rtcSendMessage(id, reinterpret_cast<const char*>(packet.data()),
                 static_cast<int>(packet.size()));
}

}  // namespace

bool IsWscChannelLabel(std::string_view label) {
  return absl::StartsWith(label, kLabelPrefix);
}

void WebSocketTransport::Send(std::span<const uint8_t> packet) {
  SendOn(ws_, packet);
}

void WebSocketTransport::Close() { rtcClose(ws_); }

void PeerTransport::AddChannel(std::string label, int dc) {
  absl::MutexLock lock(mutex_);
  channels_.insert_or_assign(std::move(label), dc);
}

void PeerTransport::RemoveChannel(int dc) {
  absl::MutexLock lock(mutex_);
  absl::erase_if(channels_,
                 [dc](const auto& entry) { return entry.second == dc; });
  if (last_received_ == dc) last_received_ = -1;
}

void PeerTransport::NoteReceived(int dc) {
  absl::MutexLock lock(mutex_);
  last_received_ = dc;
}

void PeerTransport::Send(std::span<const uint8_t> packet) {
  int dc = -1;
  if (packet.size() >= wsc::kHeaderSize) {
    const auto type = static_cast<wsc::MessageType>((packet[kTypeOffset] << 8) |
                                                    packet[kTypeOffset + 1]);
    const std::string label =
        absl::StrCat(kLabelPrefix, ":", wsc::TypeName(type));
    absl::MutexLock lock(mutex_);
    const auto it = channels_.find(label);
    dc = it != channels_.end() ? it->second : last_received_;
  }
  if (dc < 0) {
    LOG_EVERY_N_SEC(WARNING, 5) << "peer " << pc_ << ": no channel to send on";
    return;
  }
  SendOn(dc, packet);
}

void PeerTransport::Close() { rtcClosePeerConnection(pc_); }

}  // namespace wsc_server
