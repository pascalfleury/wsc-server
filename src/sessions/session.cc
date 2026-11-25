#include "src/sessions/session.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/types.h"
#include "src/memory/frame.h"
#include "src/routing/message.h"

namespace wsc_server {
namespace {

constexpr size_t kMaxErrorMessage = 200;
constexpr size_t kIdOffset =
    11;  // header: magic(4) version(3) type(2) flags(2)

// The id field of a packet that failed to decode, 0 when unreadable.
uint32_t RawId(std::span<const uint8_t> bytes) {
  if (bytes.size() < wsc::kHeaderSize) return 0;
  uint32_t id = 0;
  for (size_t i = kIdOffset; i < kIdOffset + 4; ++i) id = (id << 8) | bytes[i];
  return id;
}

}  // namespace

Session::Session(SessionTransport& transport, const Router& router,
                 FramePool& pool, const Clock& clock)
    : transport_(transport),
      router_(router),
      pool_(pool),
      clock_(clock),
      last_received_(clock.Now()) {}

void Session::OnMessage(std::span<const uint8_t> bytes) {
  if (closed_) return;
  last_received_ = clock_.Now();

  absl::StatusOr<std::shared_ptr<Frame>> frame = pool_.Acquire(bytes.size());
  if (!frame.ok()) {
    // shortcut: dropped and logged; DATAPATH.org wants STATE_ERROR for events
    // and a metric, which arrive with the metrics (Phase 6).
    LOG_EVERY_N_SEC(WARNING, 5) << "session: " << frame.status();
    return;
  }
  std::ranges::copy(bytes, (*frame)->mutable_buffer().begin());
  (*frame)->set_size(bytes.size());
  Message message{.frame = *std::move(frame), .packet = {}};

  absl::StatusOr<wsc::Packet> packet = wsc::Decode(message.frame->bytes());
  if (!packet.ok()) {
    if (!wsc::IsDrop(packet.status())) {
      SendError(RawId(bytes), packet.status());
    }
    return;
  }
  message.packet = *packet;
  Handle(*packet, std::move(message));
}

void Session::Handle(const wsc::Packet& packet, Message message) {
  const uint32_t id = packet.header.id;
  if (const auto* query = std::get_if<wsc::StateQuery>(&packet.body)) {
    SendAnswer(id, query->type == wsc::QueryType::kKeepalive
                       ? wsc::AnswerStatus::kSuccess
                       : wsc::AnswerStatus::kNotSupported);
    return;
  }
  if (std::holds_alternative<wsc::StateAnswer>(packet.body) ||
      std::holds_alternative<wsc::StateError>(packet.body)) {
    return;  // nothing of ours is waiting for answers yet
  }
  const bool ack = (packet.header.flags & wsc::flags::kAck) != 0;
  if (absl::Status status = router_.Route(std::move(message)); !status.ok()) {
    SendError(id, status);
    return;
  }
  // shortcut: ACK is answered once the router accepted the packet; F-16 wants
  // the adapter's send result, which needs a return path from the adapters.
  if (ack) SendAnswer(id, wsc::AnswerStatus::kSuccess);
}

bool Session::CloseIfTimedOut() {
  if (closed_ || clock_.Now() - last_received_ <= kTimeout) return false;
  closed_ = true;
  transport_.Close();
  return true;
}

void Session::SendAnswer(uint32_t query_id, wsc::AnswerStatus status) {
  SendPacket({.flags = 0, .id = NextId()},
             wsc::StateAnswer{.query_id = query_id,
                              .status = status,
                              .format = wsc::AnswerFormat::kRaw,
                              .data = {}});
}

void Session::SendError(uint32_t offending_id, const absl::Status& status) {
  const wsc::ErrorCode code =
      wsc::GetErrorCode(status).value_or(wsc::ErrorCode::kValidationFailed);
  const std::string_view message = status.message().substr(0, kMaxErrorMessage);
  // Spec 07: the STATE_ERROR id is the offending packet's id when known.
  SendPacket(
      {.flags = 0, .id = offending_id != 0 ? offending_id : NextId()},
      wsc::StateError{.code = code, .message = message, .context = "{}"});
}

void Session::SendPacket(wsc::Header header, const wsc::Body& body) {
  const absl::StatusOr<size_t> size = wsc::Encode(
      {.header = header, .transport = std::nullopt, .body = body}, out_);
  if (!size.ok()) {
    LOG(ERROR) << "session: cannot encode a reply: " << size.status();
    return;
  }
  transport_.Send(std::span(out_).first(*size));
}

uint32_t Session::NextId() {
  last_id_ = last_id_ == 0xFFFFFFFF ? 1 : last_id_ + 1;
  return last_id_;
}

}  // namespace wsc_server
