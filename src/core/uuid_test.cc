#include "src/core/uuid.h"

#include <random>
#include <string>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

TEST(UuidTest, ParseThenToStringRoundTripsInLowerCase) {
  const auto id = Uuid::Parse("123E4567-E89B-42D3-A456-426614174000");
  ASSERT_TRUE(id.ok()) << id.status();
  EXPECT_EQ(id->ToString(), "123e4567-e89b-42d3-a456-426614174000");
  EXPECT_EQ(id->bytes()[0], 0x12);
  EXPECT_EQ(id->bytes()[15], 0x00);
}

TEST(UuidTest, ParseRejectsMalformedText) {
  for (const char* text : {
           "",
           "123e4567-e89b-42d3-a456-42661417400",    // too short
           "123e4567-e89b-42d3-a456-4266141740000",  // too long
           "123e4567ae89b-42d3-a456-426614174000",   // missing dash
           "123e4567-e89b-42d3-a456-42661417400g",   // not hexadecimal
           "123e4567-e89b-42d3-a456-4266141-4000",   // dash in a digit position
       }) {
    const auto id = Uuid::Parse(text);
    EXPECT_EQ(id.status().code(), absl::StatusCode::kInvalidArgument) << text;
  }
}

TEST(UuidTest, GenerateV4SetsVersionAndVariantBits) {
  std::mt19937 gen(42);
  const Uuid id = Uuid::GenerateV4(gen);
  EXPECT_EQ(id.bytes()[6] & 0xF0, 0x40);
  EXPECT_EQ(id.bytes()[8] & 0xC0, 0x80);
}

TEST(UuidTest, GenerateV4GivesDifferentValuesOnSuccessiveCalls) {
  std::mt19937 gen(42);
  EXPECT_NE(Uuid::GenerateV4(gen), Uuid::GenerateV4(gen));
}

TEST(UuidTest, GeneratedUuidSurvivesTextRoundTrip) {
  std::mt19937 gen(7);
  const Uuid id = Uuid::GenerateV4(gen);
  const auto parsed = Uuid::Parse(id.ToString());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(*parsed, id);
}

}  // namespace
}  // namespace wsc_server
