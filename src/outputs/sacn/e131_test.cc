#include "src/outputs/sacn/e131.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/uuid.h"

namespace wsc_server::e131 {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

uint16_t U16At(std::span<const uint8_t> b, size_t offset) {
  return static_cast<uint16_t>((b[offset] << 8) | b[offset + 1]);
}

DataPacket Sample(std::span<const uint8_t> slots) {
  return {.cid = Uuid::Parse("35797cde-3def-48f8-bb5a-19713477591e").value(),
          .source_name = "wsc-server",
          .priority = 120,
          .sync_address = 7,
          .sequence = 42,
          .options = kOptionPreview,
          .universe = 0x0102,
          .slots = slots};
}

// Field offsets from ANSI E1.31-2018, table 4-1.
TEST(E131Test, EncodesAFullUniverseAtSpecOffsets) {
  std::vector<uint8_t> slots(512);
  for (size_t i = 0; i < slots.size(); ++i) slots[i] = static_cast<uint8_t>(i);
  std::array<uint8_t, kMaxPacketSize> out{};
  const DataPacket packet = Sample(slots);
  ASSERT_EQ(Encode(packet, out).value(), 638u);

  const std::span<const uint8_t> b = out;
  EXPECT_EQ(U16At(b, 0), 0x0010);
  EXPECT_EQ(U16At(b, 2), 0x0000);
  EXPECT_THAT(b.subspan(4, 12), ElementsAre('A', 'S', 'C', '-', 'E', '1', '.',
                                            '1', '7', 0, 0, 0));
  EXPECT_EQ(U16At(b, 16), 0x7000 | 622);  // root PDU length
  EXPECT_THAT(b.subspan(18, 4), ElementsAre(0, 0, 0, 4));
  EXPECT_THAT(b.subspan(22, 16), ElementsAreArray(packet.cid.bytes()));
  EXPECT_EQ(U16At(b, 38), 0x7000 | 600);  // framing PDU length
  EXPECT_THAT(b.subspan(40, 4), ElementsAre(0, 0, 0, 2));
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(&b[44])), "wsc-server");
  EXPECT_EQ(b[107], 0);  // source name stays null-terminated
  EXPECT_EQ(b[108], 120);
  EXPECT_EQ(U16At(b, 109), 7);
  EXPECT_EQ(b[111], 42);
  EXPECT_EQ(b[112], kOptionPreview);
  EXPECT_EQ(U16At(b, 113), 0x0102);
  EXPECT_EQ(U16At(b, 115), 0x7000 | 523);  // DMP PDU length
  EXPECT_EQ(b[117], 0x02);
  EXPECT_EQ(b[118], 0xA1);
  EXPECT_EQ(U16At(b, 119), 0x0000);
  EXPECT_EQ(U16At(b, 121), 0x0001);
  EXPECT_EQ(U16At(b, 123), 513);
  EXPECT_EQ(b[125], 0x00);  // START code
  EXPECT_THAT(b.subspan(126), ElementsAreArray(slots));
}

TEST(E131Test, ShortUniverseShortensAllLengths) {
  const std::array<uint8_t, 3> slots = {1, 2, 3};
  std::array<uint8_t, kMaxPacketSize> out{};
  ASSERT_EQ(Encode(Sample(slots), out).value(), 129u);
  EXPECT_EQ(U16At(out, 16), 0x7000 | 113);
  EXPECT_EQ(U16At(out, 38), 0x7000 | 91);
  EXPECT_EQ(U16At(out, 115), 0x7000 | 14);
  EXPECT_EQ(U16At(out, 123), 4);
}

TEST(E131Test, RejectsOutOfRangeFields) {
  std::array<uint8_t, kMaxPacketSize> out{};
  const std::vector<uint8_t> too_many(513);
  DataPacket p = Sample({});
  p.universe = 0;
  EXPECT_FALSE(Encode(p, out).ok());
  p.universe = 64000;
  EXPECT_FALSE(Encode(p, out).ok());
  p = Sample({});
  p.priority = 201;
  EXPECT_FALSE(Encode(p, out).ok());
  p = Sample(too_many);
  EXPECT_FALSE(Encode(p, out).ok());
  const std::string long_name(64, 'x');
  p = Sample({});
  p.source_name = long_name;
  EXPECT_FALSE(Encode(p, out).ok());
  EXPECT_EQ(Encode(Sample({}), std::span(out).first(125)).status().code(),
            absl::StatusCode::kResourceExhausted);
}

TEST(E131Test, MulticastAddressFollowsUniverse) {
  EXPECT_THAT(MulticastAddress(1), ElementsAre(239, 255, 0, 1));
  EXPECT_THAT(MulticastAddress(0x1234), ElementsAre(239, 255, 0x12, 0x34));
}

}  // namespace
}  // namespace wsc_server::e131
