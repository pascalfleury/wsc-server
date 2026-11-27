#ifndef WSC_SERVER_TESTING_FAKE_REPLY_SINK_H_
#define WSC_SERVER_TESTING_FAKE_REPLY_SINK_H_

#include <cstdint>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "libwsc/packet.h"
#include "src/routing/message.h"

namespace wsc_server {

// Records the results adapters report. Not thread-safe.
class FakeReplySink final : public ReplySink {
 public:
  struct Result {
    uint32_t id;
    absl::Status status;
  };

  void OnResult(const wsc::Header& header,
                const absl::Status& status) override {
    results.push_back({.id = header.id, .status = status});
  }

  std::vector<Result> results;
};

}  // namespace wsc_server

#endif  // WSC_SERVER_TESTING_FAKE_REPLY_SINK_H_
