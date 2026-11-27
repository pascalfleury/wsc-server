#include "src/sessions/signalling.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"

namespace wsc_server::signalling {
namespace {

using Json = nlohmann::json;

// The string at `key` of an object, or nullptr. Never throws (exceptions are
// off, so a type error would abort): every type is checked first.
const std::string* StringAt(const Json& object, std::string_view key) {
  if (!object.is_object()) return nullptr;
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) return nullptr;
  return it->get_ptr<const std::string*>();
}

const Json* ObjectAt(const Json& object, std::string_view key) {
  if (!object.is_object()) return nullptr;
  const auto it = object.find(key);
  return it == object.end() || !it->is_object() ? nullptr : &*it;
}

std::string Dump(const Json& json) {
  // Replace invalid UTF-8 instead of failing; SDP and candidates are ASCII.
  return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}  // namespace

absl::StatusOr<std::optional<Message>> Parse(std::string_view text) {
  const Json json = Json::parse(text, nullptr, /*allow_exceptions=*/false);
  const std::string* type = StringAt(json, "type");
  if (json.is_discarded() || type == nullptr) {
    return absl::InvalidArgumentError(
        "signalling message is not JSON with a type");
  }
  if (*type == "offer") {
    const Json* offer = ObjectAt(json, "offer");
    const std::string* sdp =
        offer != nullptr ? StringAt(*offer, "sdp") : nullptr;
    if (sdp == nullptr) return absl::InvalidArgumentError("offer without sdp");
    return Message(Offer{.sdp = *sdp});
  }
  if (*type == "ice") {
    const Json* init = ObjectAt(json, "candidate");
    const std::string* candidate =
        init != nullptr ? StringAt(*init, "candidate") : nullptr;
    if (candidate == nullptr || candidate->empty()) return std::nullopt;
    const std::string* mid = StringAt(*init, "sdpMid");
    return Message(
        Candidate{.candidate = *candidate, .mid = mid != nullptr ? *mid : "0"});
  }
  return std::nullopt;
}

std::string AnswerJson(std::string_view type, std::string_view sdp) {
  return Dump({{"type", "answer"}, {"answer", {{"type", type}, {"sdp", sdp}}}});
}

std::string CandidateJson(std::string_view candidate, std::string_view mid) {
  return Dump(
      {{"type", "ice"},
       {"candidate",
        {{"candidate", candidate}, {"sdpMid", mid}, {"sdpMLineIndex", 0}}}});
}

}  // namespace wsc_server::signalling
