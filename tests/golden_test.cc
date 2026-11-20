// Decodes the golden packets produced by the upstream JS SDK (tests/golden/,
// tools/golden/gen_golden.mjs) and re-encodes the spec-conformant ones.

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwsc/address.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/validate.h"

namespace wsc {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Optional;

struct Case {
  std::string_view name;
  bool conformant;  // false: MANIFEST note "NOT SPEC-CONFORMANT"
};

constexpr auto kCases = std::to_array<Case>({
    {.name = "stream_channels_sacn_full", .conformant = true},
    {.name = "stream_channels_artnet_slice", .conformant = true},
    {.name = "stream_channels_dmx_usb", .conformant = true},
    {.name = "stream_channels_dmx_serial", .conformant = true},
    {.name = "stream_timecode", .conformant = true},
    {.name = "control_cue_ack", .conformant = true},
    {.name = "control_cue_osc", .conformant = true},
    {.name = "control_cue_u16_index", .conformant = true},
    {.name = "control_cue_point_cue", .conformant = true},
    {.name = "control_cue_custom", .conformant = false},
    {.name = "control_param_u8", .conformant = true},
    {.name = "control_param_u16", .conformant = true},
    {.name = "control_param_u32", .conformant = true},
    {.name = "control_param_u64", .conformant = true},
    {.name = "control_param_i8", .conformant = true},
    {.name = "control_param_i16", .conformant = true},
    {.name = "control_param_i32", .conformant = true},
    {.name = "control_param_f32", .conformant = true},
    {.name = "control_param_f64", .conformant = true},
    {.name = "control_param_string", .conformant = true},
    {.name = "control_param_bool", .conformant = true},
    {.name = "control_param_modbus_tcp", .conformant = true},
    {.name = "control_param_http_url", .conformant = true},
    {.name = "tunnel_raw_sdk_helper", .conformant = false},
    {.name = "tunnel_raw_udp", .conformant = true},
    {.name = "state_query_keepalive", .conformant = true},
    {.name = "state_answer_json", .conformant = true},
    {.name = "state_answer_raw", .conformant = true},
    {.name = "state_error", .conformant = true},
    {.name = "header_max_id", .conformant = true},
});

std::vector<uint8_t> Load(std::string_view name) {
  std::ifstream in(std::string(WSC_GOLDEN_DIR "/") + std::string(name) + ".bin",
                   std::ios::binary);
  EXPECT_TRUE(in.good()) << name;
  return {std::istreambuf_iterator<char>(in), {}};
}

// The address re-encoded from its parsed segments.
std::vector<uint8_t> ReencodeAddress(std::span<const uint8_t> address) {
  const absl::StatusOr<Segments> segments = ParseAddress(address);
  EXPECT_TRUE(segments.ok()) << segments.status();
  std::vector<uint8_t> out(address.size() + 8);
  const absl::StatusOr<size_t> size = EncodeAddress(*segments, out);
  EXPECT_TRUE(size.ok()) << size.status();
  out.resize(size.value_or(0));
  return out;
}

class GoldenTest : public testing::TestWithParam<Case> {};

TEST_P(GoldenTest, DecodesAndRoundTrips) {
  const Case& c = GetParam();
  const std::vector<uint8_t> bytes = Load(c.name);
  const absl::StatusOr<Packet> packet = Decode(bytes);
  ASSERT_TRUE(packet.ok()) << packet.status();
  if (!c.conformant) return;  // decoder robustness only

  EXPECT_TRUE(Validate(*packet).ok()) << Validate(*packet);
  std::vector<uint8_t> out(bytes.size());
  const absl::StatusOr<size_t> size = Encode(*packet, out);
  ASSERT_TRUE(size.ok()) << size.status();
  EXPECT_EQ(out, bytes);

  if (const auto* cue = std::get_if<ControlCue>(&packet->body)) {
    EXPECT_THAT(ReencodeAddress(cue->address), ElementsAreArray(cue->address));
  } else if (const auto* param = std::get_if<ControlParam>(&packet->body)) {
    EXPECT_THAT(ReencodeAddress(param->address),
                ElementsAreArray(param->address));
  }
}

INSTANTIATE_TEST_SUITE_P(Sdk, GoldenTest, testing::ValuesIn(kCases),
                         [](const testing::TestParamInfo<Case>& info) {
                           return std::string(info.param.name);
                         });

TEST(GoldenContentTest, MatchesGeneratorInputs) {
  const std::vector<uint8_t> sacn_bytes = Load("stream_channels_sacn_full");
  const Packet sacn = Decode(sacn_bytes).value();
  EXPECT_NE(sacn.header.flags & flags::kMulticast, 0);
  const auto& channels = std::get<StreamChannels>(sacn.body);
  EXPECT_EQ(channels.universe, 1);
  EXPECT_EQ(channels.start_channel, 1);
  EXPECT_EQ(channels.values.size(), 512u);
  if (!sacn.transport) FAIL() << "no transport descriptor";
  const auto& params = sacn.transport->params;
  ASSERT_EQ(params.size(), 1u);
  EXPECT_EQ(params[0].key, ParamKey::kPriority);
  EXPECT_THAT(params[0].value, ElementsAreArray({100}));

  const std::vector<uint8_t> tc_bytes = Load("stream_timecode");
  const auto tc = std::get<StreamTimecode>(Decode(tc_bytes).value().body);
  EXPECT_EQ(tc.hours, 1);
  EXPECT_EQ(tc.frames, 4);
  EXPECT_EQ(tc.rate, 25);

  const std::vector<uint8_t> error_bytes = Load("state_error");
  const auto error = std::get<StateError>(Decode(error_bytes).value().body);
  EXPECT_EQ(error.code, ErrorCode::kNoRoute);
  EXPECT_EQ(error.message, "no adapter");
  EXPECT_EQ(error.context, R"({"protocol":2})");

  const std::vector<uint8_t> id_bytes = Load("header_max_id");
  EXPECT_EQ(Decode(id_bytes).value().header.id, 0xFFFFFFFFu);

  const std::vector<uint8_t> raw_bytes = Load("tunnel_raw_sdk_helper");
  EXPECT_THAT(GetErrorCode(Validate(Decode(raw_bytes).value())),
              Optional(ErrorCode::kIncompatibleIface));
}

}  // namespace
}  // namespace wsc
