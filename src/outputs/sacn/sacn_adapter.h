#ifndef WSC_SERVER_SRC_OUTPUTS_SACN_SACN_ADAPTER_H_
#define WSC_SERVER_SRC_OUTPUTS_SACN_SACN_ADAPTER_H_

#include <array>
#include <span>

#include "absl/time/time.h"
#include "libwsc/types.h"
#include "src/core/clock.h"
#include "src/outputs/datagram_sender.h"
#include "src/outputs/sacn/sacn_streams.h"
#include "src/platform/asio.h"
#include "src/routing/message.h"
#include "src/routing/output_adapter.h"
#include "src/shaping/policy.h"

namespace wsc_server {

// The sACN (E1.31) output adapter: runs SacnStreams on its own strand, with
// one timer for all streams (DATAPATH.org). Must outlive the io_context's
// run; destroy it only after the io_context has stopped.
class SacnAdapter final : public OutputAdapter {
 public:
  SacnAdapter(const asio::any_io_executor& executor, const Clock& clock,
              SacnConfig config, DatagramSender& sender);

  [[nodiscard]] wsc::Protocol protocol() const override {
    return wsc::Protocol::kSacn;
  }
  [[nodiscard]] std::span<const wsc::Iface> ifaces() const override {
    return kIfaces;
  }
  [[nodiscard]] DeliveryModel model() const override {
    return DeliveryModel::kCyclic;
  }
  [[nodiscard]] bool enabled() const override { return true; }

  void Submit(Message message) override;

 private:
  static constexpr std::array<wsc::Iface, 1> kIfaces = {wsc::Iface::kUdp};

  // On the strand: sends what is due and re-arms the timer.
  void Tick();

  asio::strand<asio::any_io_executor> strand_;
  asio::steady_timer timer_;
  const Clock& clock_;
  SacnStreams streams_;
  absl::Time armed_for_ = absl::InfiniteFuture();
};

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_OUTPUTS_SACN_SACN_ADAPTER_H_
