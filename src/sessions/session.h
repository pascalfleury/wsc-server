#ifndef WSC_SERVER_SRC_SESSIONS_SESSION_H_
#define WSC_SERVER_SRC_SESSIONS_SESSION_H_

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "libwsc/packet.h"
#include "src/core/clock.h"
#include "src/memory/frame.h"
#include "src/memory/frame_pool.h"
#include "src/routing/message.h"
#include "src/routing/router.h"

namespace wsc_server {

// The channel a Session talks over: one WSC packet per message (WebSocket
// binary message, WebRTC DataChannel message).
class SessionTransport {
 public:
  virtual ~SessionTransport() = default;
  virtual void Send(std::span<const uint8_t> packet) = 0;
  virtual void Close() = 0;
};

// One connected WSC peer (CODEC.org, Sessions; spec 06). Places each received
// message in a pooled Frame, decodes it, answers keepalives itself and routes
// everything else; errors go back as STATE_ERROR with the offending packet's
// id (F-15), ACK requests are answered with STATE_ANSWER once the adapter
// accepted the packet (F-16). Transport agnostic and not thread-safe: the
// transport calls it from one strand.
class Session {
 public:
  // A session silent for longer than this is closed (spec 06, F-12).
  static constexpr absl::Duration kTimeout = absl::Seconds(5);

  Session(SessionTransport& transport, const Router& router, FramePool& pool,
          const Clock& clock);

  // Handles one received message: copies it into a pooled Frame, then
  // OnFrame().
  void OnMessage(std::span<const uint8_t> bytes,
                 std::weak_ptr<ReplySink> reply = {});

  // Handles one received message already placed in a Frame (the transport
  // fills it on its own thread; FramePool is thread-safe). Routed messages
  // carry `reply`, where their adapter reports back (OnResult()).
  void OnFrame(std::shared_ptr<const Frame> frame,
               std::weak_ptr<ReplySink> reply);

  // An adapter's result for a routed packet: STATE_ERROR with its id when it
  // failed, else STATE_ANSWER(SUCCESS) when it asked for ACK (F-15, F-16).
  void OnResult(const wsc::Header& header, const absl::Status& status);

  // Restarts the silence clock: the timeout counts from now. For transports
  // that connect after the session is created (WebRTC).
  void ResetTimeout() { last_received_ = clock_.Now(); }

  // Closes the transport when the peer has been silent for kTimeout; returns
  // true then. Call periodically.
  bool CloseIfTimedOut();

 private:
  void Handle(Message message);
  void SendAnswer(uint32_t query_id, wsc::AnswerStatus status);
  // STATE_ERROR for a failed status that carries a WSC error code.
  void SendError(uint32_t offending_id, const absl::Status& status);
  void SendPacket(wsc::Header header, const wsc::Body& body);
  uint32_t NextId();

  SessionTransport& transport_;
  const Router& router_;
  FramePool& pool_;
  const Clock& clock_;
  absl::Time last_received_;
  uint32_t last_id_ = 0;
  bool closed_ = false;
  std::array<uint8_t, 512> out_{};  // replies are small (no data payloads)
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_SESSIONS_SESSION_H_
