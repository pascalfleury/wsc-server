#include "src/sessions/signalling.h"

#include <optional>
#include <string>
#include <variant>

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace wsc_server::signalling {
namespace {

TEST(SignallingTest, ParsesTheReferenceClientsOffer) {
  const auto message = Parse(
      R"({"type":"offer","offer":{"type":"offer","sdp":"v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\n"}})");
  ASSERT_TRUE(message.ok()) << message.status();
  const std::optional<Message>& offer = *message;
  if (!offer) FAIL() << "offer ignored";
  EXPECT_EQ(std::get<Offer>(*offer).sdp, "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\n");
}

TEST(SignallingTest, ParsesACandidate) {
  const auto message = Parse(
      R"({"type":"ice","candidate":{"candidate":"candidate:1 1 UDP 1 10.0.0.2 5000 typ host","sdpMid":"0","sdpMLineIndex":0,"usernameFragment":"ab"}})");
  ASSERT_TRUE(message.ok()) << message.status();
  const std::optional<Message>& signal = *message;
  if (!signal) FAIL() << "candidate ignored";
  const auto& candidate = std::get<Candidate>(*signal);
  EXPECT_EQ(candidate.candidate, "candidate:1 1 UDP 1 10.0.0.2 5000 typ host");
  EXPECT_EQ(candidate.mid, "0");
}

TEST(SignallingTest, IgnoresWhatTheServerDoesNotHandle) {
  for (const char* text :
       {R"({"type":"answer","answer":{"type":"answer","sdp":"x"}})",
        R"({"type":"ice","candidate":{"candidate":"","sdpMid":"0"}})",
        R"({"type":"bye"})"}) {
    const auto message = Parse(text);
    ASSERT_TRUE(message.ok()) << text;
    EXPECT_FALSE(message->has_value()) << text;
  }
}

TEST(SignallingTest, RejectsMalformedMessagesWithoutAborting) {
  for (const char* text :
       {"not json", "[]", R"({"type":7})", R"({"type":"offer"})",
        R"({"type":"offer","offer":{"sdp":5}})",
        R"({"type":"offer","offer":"x"})"}) {
    EXPECT_FALSE(Parse(text).ok()) << text;
  }
}

TEST(SignallingTest, BuildsAnswerAndCandidateLikeTheReferenceGateway) {
  const auto answer = nlohmann::json::parse(AnswerJson("answer", "v=0"));
  EXPECT_EQ(answer["type"], "answer");
  EXPECT_EQ(answer["answer"]["type"], "answer");
  EXPECT_EQ(answer["answer"]["sdp"], "v=0");
  const auto ice = nlohmann::json::parse(CandidateJson("candidate:1", "0"));
  EXPECT_EQ(ice["type"], "ice");
  EXPECT_EQ(ice["candidate"]["candidate"], "candidate:1");
  EXPECT_EQ(ice["candidate"]["sdpMid"], "0");
  EXPECT_EQ(ice["candidate"]["sdpMLineIndex"], 0);
}

}  // namespace
}  // namespace wsc_server::signalling
