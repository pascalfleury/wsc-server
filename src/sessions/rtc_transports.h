#ifndef WSC_SERVER_SRC_SESSIONS_RTC_TRANSPORTS_H_
#define WSC_SERVER_SRC_SESSIONS_RTC_TRANSPORTS_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "src/sessions/session.h"

// SessionTransports over libdatachannel objects (C API ids).
namespace wsc_server {

// Data channels carrying WSC: labels starting with "WSC!DC" (spec 06 uses
// "WSC!DC!<timestamp>", the reference client "WSC!DC:<TYPE>").
bool IsWscChannelLabel(std::string_view label);

// One plain WebSocket: one WSC packet per binary message.
class WebSocketTransport final : public SessionTransport {
 public:
  explicit WebSocketTransport(int ws) : ws_(ws) {}
  void Send(std::span<const uint8_t> packet) override;
  void Close() override;

 private:
  int ws_;
};

// One WebRTC peer connection with any number of WSC data channels. Each
// packet goes out on the channel named after its type ("WSC!DC:<TYPE>", as
// the reference client and gateway do), else on the channel the peer used
// last. Channels are added and removed from libdatachannel threads.
class PeerTransport final : public SessionTransport {
 public:
  explicit PeerTransport(int pc) : pc_(pc) {}

  void AddChannel(std::string label, int dc);
  void RemoveChannel(int dc);
  void NoteReceived(int dc);

  void Send(std::span<const uint8_t> packet) override;
  void Close() override;

 private:
  int pc_;
  absl::Mutex mutex_;
  absl::flat_hash_map<std::string, int> channels_ ABSL_GUARDED_BY(mutex_);
  int last_received_ ABSL_GUARDED_BY(mutex_) = -1;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SESSIONS_RTC_TRANSPORTS_H_
