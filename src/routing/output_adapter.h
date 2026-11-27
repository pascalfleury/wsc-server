#ifndef WSC_SERVER_SRC_ROUTING_OUTPUT_ADAPTER_H_
#define WSC_SERVER_SRC_ROUTING_OUTPUT_ADAPTER_H_

#include <span>

#include "libwsc/types.h"
#include "src/routing/message.h"
#include "src/shaping/policy.h"

namespace wsc_server {

// Forward sink for one downstream protocol over one or more interfaces
// (ADAPTERS.org). It derives the
// stream from each message and owns that stream's arbiter and shaper.
class OutputAdapter {
 public:
  virtual ~OutputAdapter() = default;

  [[nodiscard]] virtual wsc::Protocol protocol() const = 0;
  // The interfaces this adapter serves for its protocol.
  [[nodiscard]] virtual std::span<const wsc::Iface> ifaces() const = 0;
  [[nodiscard]] virtual DeliveryModel model() const = 0;
  // False when disabled by config; the router answers CONFIG_ERROR.
  [[nodiscard]] virtual bool enabled() const = 0;

  // Takes a validated message for this protocol. Called on any thread; the
  // adapter posts it to its own strand, and reports the result exactly once
  // with Report() (message.h).
  virtual void Submit(Message message) = 0;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_ROUTING_OUTPUT_ADAPTER_H_
