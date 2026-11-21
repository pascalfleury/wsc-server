#ifndef WSC_SERVER_SRC_ROUTING_MESSAGE_H_
#define WSC_SERVER_SRC_ROUTING_MESSAGE_H_

#include <memory>

#include "libwsc/packet.h"
#include "src/memory/frame.h"

namespace wsc_server {

// A decoded packet with the Frame its views point into. Copies share the
// Frame (zero-copy fan-out, N-02).
struct Message {
  std::shared_ptr<const Frame> frame;
  wsc::Packet packet;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_ROUTING_MESSAGE_H_
