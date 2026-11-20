#include "libwsc/address.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace wsc {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

constexpr uint8_t kLighting = 0x01;
constexpr uint8_t kLayer = 0x10;
constexpr uint8_t kCue = 0x19;
constexpr uint8_t kIntensity = 0x40;

std::vector<uint8_t> Encode(std::span<const Segment> segments) {
  std::array<uint8_t, 512> buffer{};
  const absl::StatusOr<size_t> size = EncodeAddress(segments, buffer);
  EXPECT_TRUE(size.ok()) << size.status();
  return {buffer.begin(), buffer.begin() + size.value_or(0)};
}

TEST(AddressTest, EncodesSpecExample) {
  const std::array segments = {Segment::Token(kLighting),
                               Segment::Token(kLayer), Segment::Index(2),
                               Segment::Token(kIntensity)};
  const std::vector<uint8_t> bytes = Encode(segments);
  EXPECT_THAT(bytes, ElementsAre(0x01, 0x10, 0x80, 0x02, 0x40, 0x00));
  EXPECT_THAT(ParseAddress(bytes).value(), ElementsAreArray(segments));
}

TEST(AddressTest, EncodesU16IndexWithTwoSentinels) {
  const std::array segments = {Segment::Token(kLighting), Segment::Token(kCue),
                               Segment::Index(300)};
  const std::vector<uint8_t> bytes = Encode(segments);
  EXPECT_THAT(bytes, ElementsAre(0x01, 0x19, 0x80, 0x01, 0x80, 0x2C, 0x00));
  EXPECT_THAT(ParseAddress(bytes).value(), ElementsAreArray(segments));
}

TEST(AddressTest, RoundTripsCustomAndUnknownTokens) {
  const std::array segments = {Segment::Token(kLighting),
                               Segment::Custom("house_lights_on"),
                               Segment::Token(0xFF), Segment::Token(0x5A)};
  EXPECT_THAT(ParseAddress(Encode(segments)).value(),
              ElementsAreArray(segments));
}

TEST(AddressTest, ReadsTwoSmallIndicesAsOneU16Index) {
  // lighting.cue.10.5 as a naive encoder writes it (spec 03 ambiguity).
  const std::array<uint8_t, 7> bytes = {0x01, 0x19, 0x80, 0x0A,
                                        0x80, 0x05, 0x00};
  EXPECT_THAT(ParseAddress(bytes).value(),
              ElementsAre(Segment::Token(kLighting), Segment::Token(kCue),
                          Segment::Index(0x0A05)));
}

TEST(AddressTest, RefusesIndexAfterIndex) {
  std::array<uint8_t, 16> buffer{};
  const std::array segments = {Segment::Token(kCue), Segment::Index(10),
                               Segment::Index(5)};
  EXPECT_EQ(EncodeAddress(segments, buffer).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(AddressTest, RefusesReservedTokensAndLongCustom) {
  std::array<uint8_t, 512> buffer{};
  for (const uint8_t t : {0x00, 0x80, 0xFE}) {
    const std::array s = {Segment::Token(t)};
    EXPECT_FALSE(EncodeAddress(s, buffer).ok()) << int{t};
  }
  const std::string text(256, 'x');
  const std::array s = {Segment::Custom(text)};
  EXPECT_FALSE(EncodeAddress(s, buffer).ok());
}

TEST(AddressTest, FailsWhenBufferIsTooSmall) {
  std::array<uint8_t, 2> buffer{};
  const std::array s = {Segment::Token(kLighting), Segment::Index(1)};
  EXPECT_EQ(EncodeAddress(s, buffer).status().code(),
            absl::StatusCode::kResourceExhausted);
}

TEST(AddressTest, RejectsMalformedWireForms) {
  const std::vector<std::vector<uint8_t>> bad = {
      {},                            // empty
      {0x01, 0x10},                  // no terminator
      {0x01, 0x00, 0x10},            // bytes after the terminator
      {0x01, 0x80},                  // index without value
      {0x01, 0x80, 0x01, 0x80},      // u16 index without low byte
      {0x01, 0xFE, 0x05, 'a', 0x00}  // custom string runs past the end
  };
  for (const std::vector<uint8_t>& bytes : bad) {
    EXPECT_FALSE(CheckAddress(bytes).ok()) << bytes.size();
  }
  EXPECT_TRUE(CheckAddress(std::vector<uint8_t>{0x00}).ok());
}

}  // namespace
}  // namespace wsc
