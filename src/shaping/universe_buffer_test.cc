#include "src/shaping/universe_buffer.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/error.h"

namespace wsc_server {
namespace {

using ::testing::Each;
using ::testing::Optional;

std::vector<uint8_t> Filled(size_t n, uint8_t value) {
  return std::vector<uint8_t>(n, value);
}

TEST(UniverseBufferTest, StartsEmpty) {
  const UniverseBuffer buffer;
  EXPECT_EQ(buffer.extent(), 0u);
  EXPECT_TRUE(buffer.Frame(/*partial_dmx=*/true).empty());
  EXPECT_EQ(buffer.Frame(/*partial_dmx=*/false).size(), 512u);
  EXPECT_THAT(buffer.Frame(/*partial_dmx=*/false), Each(0));
}

TEST(UniverseBufferTest, SliceIsWrittenAtItsStartChannel) {
  // The DATAPATH.org example: a slice for channels 50-124.
  UniverseBuffer buffer;
  ASSERT_TRUE(buffer.Apply(50, Filled(75, 7)).ok());
  EXPECT_EQ(buffer.extent(), 124u);

  const auto partial = buffer.Frame(/*partial_dmx=*/true);
  ASSERT_EQ(partial.size(), 124u);
  EXPECT_THAT(partial.first(49), Each(0));
  EXPECT_THAT(partial.subspan(49), Each(7));

  const auto full = buffer.Frame(/*partial_dmx=*/false);
  ASSERT_EQ(full.size(), 512u);
  EXPECT_THAT(full.subspan(49, 75), Each(7));
  EXPECT_THAT(full.subspan(124), Each(0));
}

TEST(UniverseBufferTest, LaterSlicesOverwriteAndExtentOnlyGrows) {
  UniverseBuffer buffer;
  ASSERT_TRUE(buffer.Apply(1, Filled(200, 1)).ok());
  ASSERT_TRUE(buffer.Apply(10, Filled(5, 2)).ok());
  EXPECT_EQ(buffer.extent(), 200u);
  EXPECT_EQ(buffer.Frame(true)[9], 2);
  EXPECT_EQ(buffer.Frame(true)[14], 1);
  ASSERT_TRUE(buffer.Apply(512, Filled(1, 3)).ok());
  EXPECT_EQ(buffer.extent(), 512u);
}

TEST(UniverseBufferTest, RejectsSlicesOutsideTheUniverse) {
  UniverseBuffer buffer;
  EXPECT_THAT(wsc::GetErrorCode(buffer.Apply(0, Filled(1, 1))),
              Optional(wsc::ErrorCode::kValidationFailed));
  EXPECT_THAT(wsc::GetErrorCode(buffer.Apply(500, Filled(14, 1))),
              Optional(wsc::ErrorCode::kValidationFailed));
  EXPECT_TRUE(UniverseBuffer().Apply(500, Filled(13, 1)).ok());  // 500-512
  EXPECT_THAT(wsc::GetErrorCode(buffer.Apply(1, Filled(513, 1))),
              Optional(wsc::ErrorCode::kValidationFailed));
  EXPECT_EQ(buffer.extent(), 0u);           // nothing written
  EXPECT_TRUE(buffer.Apply(600, {}).ok());  // empty slice: no-op
}

}  // namespace
}  // namespace wsc_server
