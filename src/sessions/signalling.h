#ifndef WSC_SERVER_SRC_SESSIONS_SIGNALLING_H_
#define WSC_SERVER_SRC_SESSIONS_SIGNALLING_H_

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "absl/status/statusor.h"

// The JSON signalling of spec 06 on ws://host:port/ws: the client sends an
// offer and ICE candidates, the server answers and sends its candidates.
namespace wsc_server::signalling {

struct Offer {
  std::string sdp;
};
struct Candidate {
  std::string candidate;
  std::string mid;
};
using Message = std::variant<Offer, Candidate>;

// Parses a client message: {"type":"offer","offer":{"type":"offer","sdp":..}}
// or {"type":"ice","candidate":{"candidate":..,"sdpMid":..}}. nullopt for a
// message the server ignores (an answer, an empty end-of-candidates
// candidate, an unknown type); InvalidArgument for malformed JSON.
absl::StatusOr<std::optional<Message>> Parse(std::string_view text);

// {"type":"answer","answer":{"type":<type>,"sdp":<sdp>}}
std::string AnswerJson(std::string_view type, std::string_view sdp);

// {"type":"ice","candidate":{"candidate":..,"sdpMid":..,"sdpMLineIndex":0}}
std::string CandidateJson(std::string_view candidate, std::string_view mid);

}  // namespace wsc_server::signalling

#endif  // WSC_SERVER_SRC_SESSIONS_SIGNALLING_H_
