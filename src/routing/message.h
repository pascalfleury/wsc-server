#ifndef WSC_SERVER_SRC_ROUTING_MESSAGE_H_
#define WSC_SERVER_SRC_ROUTING_MESSAGE_H_

#include <memory>

#include "absl/status/status.h"
#include "libwsc/packet.h"
#include "src/memory/frame.h"

namespace wsc_server {

// Where an output adapter reports what it did with a message (F-15, F-16):
// the session that received it. Called on the adapter's strand; the sink
// moves the result to its own.
class ReplySink {
 public:
  virtual ~ReplySink() = default;
  // OK when the adapter accepted the packet; otherwise a status carrying the
  // WSC error code (libwsc/error.h).
  virtual void OnResult(const wsc::Header& header,
                        const absl::Status& status) = 0;
};

// A decoded packet with the Frame its views point into, and the sink for its
// result. Copies share the Frame (zero-copy fan-out, N-02).
struct Message {
  std::shared_ptr<const Frame> frame;
  wsc::Packet packet;
  std::weak_ptr<ReplySink> reply;  // empty or expired: nobody is listening
};

// Reports the adapter's result for `message`. Adapters call it exactly once
// per Submit().
inline void Report(const Message& message, const absl::Status& status) {
  if (const std::shared_ptr<ReplySink> sink = message.reply.lock()) {
    sink->OnResult(message.packet.header, status);
  }
}

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_ROUTING_MESSAGE_H_
