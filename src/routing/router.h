#ifndef WSC_SERVER_SRC_ROUTING_ROUTER_H_
#define WSC_SERVER_SRC_ROUTING_ROUTER_H_

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "libwsc/types.h"
#include "src/routing/message.h"
#include "src/routing/output_adapter.h"

namespace wsc_server {

// Stateless forward routing (DATAPATH.org): a packet's Transport Descriptor
// alone decides the output adapter. Adapters are registered at startup;
// Route() may then be called from any thread.
class Router {
 public:
  // Fails with AlreadyExists when the protocol has an adapter already.
  absl::Status Register(OutputAdapter& adapter);

  // Validates the packet and submits it to the adapter of its protocol.
  // Failures carry the WSC error code for the STATE_ERROR reply (error.h):
  // the Validate() codes, NO_ROUTE when GW = 0 or no adapter handles the
  // protocol, CONFIG_ERROR when that adapter is disabled.
  absl::Status Route(Message message) const;

 private:
  absl::flat_hash_map<wsc::Protocol, OutputAdapter*> adapters_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_ROUTING_ROUTER_H_
