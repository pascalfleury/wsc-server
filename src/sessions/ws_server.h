#ifndef WSC_SERVER_SRC_SESSIONS_WS_SERVER_H_
#define WSC_SERVER_SRC_SESSIONS_WS_SERVER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "rtc/rtc.h"
#include "src/core/clock.h"
#include "src/memory/frame_pool.h"
#include "src/platform/asio.h"
#include "src/routing/router.h"
#include "src/sessions/rtc_transports.h"
#include "src/sessions/session_host.h"

namespace wsc_server {

// The endpoint ws://host:port/ws (F-13), on libdatachannel's C API. A
// WebSocket that sends binary messages is a plain WebSocket session (one WSC
// packet per message). One that sends text is WebRTC signalling (spec 06):
// the client's offer creates a peer connection, whose WSC data channels
// together form one session. libdatachannel calls in from its own threads;
// each session runs on its own strand (SessionHost).
class WsServer {
 public:
  struct Options {
    uint16_t port = 4515;      // the upstream gateway's port; 0 = any free one
    std::string bind_address;  // empty = all interfaces
  };

  static absl::StatusOr<std::unique_ptr<WsServer>> Create(
      const Options& options, const asio::any_io_executor& executor,
      const Router& router, FramePool& pool, const Clock& clock);

  // Stops accepting, deletes every socket, peer connection and channel, and
  // waits for scheduled reaps. The executor must still be running. Posted
  // session work may still run afterwards; the router, pool and clock must
  // outlive it.
  ~WsServer();

  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  // Stops libdatachannel's background threads and frees its global state.
  // Call after the last WsServer is destroyed, before the process exits:
  // otherwise those threads can outlive main() and race with static
  // destructors. A later Create() loads the library again.
  static void ShutdownLibrary();

  [[nodiscard]] uint16_t port() const;
  // Live sessions: WebSockets with a binary session, and peer connections.
  [[nodiscard]] size_t session_count() const;

 private:
  // One libdatachannel object (ids are unique across kinds).
  struct Entry {
    enum class Kind : uint8_t { kWebSocket, kPeerConnection, kDataChannel };
    Kind kind = Kind::kWebSocket;
    // WebSocket: its binary session, created on the first binary message.
    // Peer connection: the peer's session. Data channel: its peer's session.
    std::shared_ptr<SessionHost> host = nullptr;
    PeerTransport* peer = nullptr;  // peer connections and their channels
    int link = -1;  // WebSocket: its peer; peer: its signalling WebSocket
    bool closing = false;  // a reap is scheduled
  };

  WsServer(asio::any_io_executor executor, const Router& router,
           FramePool& pool, const Clock& clock)
      : executor_(std::move(executor)),
        router_(router),
        pool_(pool),
        clock_(clock) {}

  // libdatachannel callbacks; `ptr` is the WsServer.
  static void OnClient(int server, int ws, void* ptr);
  static void OnOpen(int ws, void* ptr);
  static void OnClosed(int id, void* ptr);
  static void OnWebSocketMessage(int ws, const char* data, int size, void* ptr);
  static void OnLocalDescription(int pc, const char* sdp, const char* type,
                                 void* ptr);
  static void OnLocalCandidate(int pc, const char* candidate, const char* mid,
                               void* ptr);
  static void OnStateChange(int pc, rtcState state, void* ptr);
  static void OnDataChannel(int pc, int dc, void* ptr);
  static void OnChannelMessage(int dc, const char* data, int size, void* ptr);

  void HandleBinary(int ws, const char* data, int size);
  void HandleSignalling(int ws, std::string_view text);
  void StartPeer(int ws, const std::string& sdp);
  // Copies a received message into a pooled Frame and hands it to `host`.
  void Deliver(SessionHost& host, const char* data, int size);
  int SignallingSocketOf(int pc) const;

  // Marks the entry closing and posts Reap(id). Callbacks never delete their
  // own object: libdatachannel's delete does not wait for other callbacks of
  // that object when called from one of them.
  void ScheduleReap(int id, Entry& entry) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // On the executor: removes the entry and deletes its object, which waits
  // until the object's callbacks have returned.
  void Reap(int id);

  asio::any_io_executor executor_;
  const Router& router_;
  FramePool& pool_;
  const Clock& clock_;
  int server_ = -1;

  mutable absl::Mutex mutex_;
  // Every object not deleted yet. Whoever removes an entry deletes its
  // object: Reap() or the destructor, both outside libdatachannel callbacks,
  // so each delete waits for the callbacks still using this server.
  absl::flat_hash_map<int, Entry> entries_ ABSL_GUARDED_BY(mutex_);
  int pending_reaps_ ABSL_GUARDED_BY(mutex_) = 0;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SESSIONS_WS_SERVER_H_
