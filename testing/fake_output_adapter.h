#ifndef WSC_SERVER_TESTING_FAKE_OUTPUT_ADAPTER_H_
#define WSC_SERVER_TESTING_FAKE_OUTPUT_ADAPTER_H_

#include <span>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "libwsc/types.h"
#include "src/routing/message.h"
#include "src/routing/output_adapter.h"
#include "src/shaping/policy.h"

namespace wsc_server {

// Records submitted messages instead of sending them, and reports `result`
// for each at once.
class FakeOutputAdapter final : public OutputAdapter {
 public:
  FakeOutputAdapter(wsc::Protocol protocol, std::vector<wsc::Iface> ifaces)
      : protocol_(protocol), ifaces_(std::move(ifaces)) {}

  [[nodiscard]] wsc::Protocol protocol() const override { return protocol_; }
  [[nodiscard]] std::span<const wsc::Iface> ifaces() const override {
    return ifaces_;
  }
  [[nodiscard]] DeliveryModel model() const override {
    return DeliveryModel::kCyclic;
  }
  [[nodiscard]] bool enabled() const override { return enabled_; }
  void Submit(Message message) override {
    Report(message, result);
    submitted.push_back(std::move(message));
  }

  bool enabled_ = true;
  absl::Status result;
  std::vector<Message> submitted;

 private:
  wsc::Protocol protocol_;
  std::vector<wsc::Iface> ifaces_;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_TESTING_FAKE_OUTPUT_ADAPTER_H_
