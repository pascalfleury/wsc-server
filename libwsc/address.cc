#include "libwsc/address.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "libwsc/error.h"
#include "libwsc/wire.h"

namespace wsc {
namespace {

// Parses `bytes`, appending segments to `out` when it is not null.
absl::Status Parse(std::span<const uint8_t> bytes, Segments* out) {
  Reader in(bytes);
  while (in.ok() && in.remaining() > 0) {
    const uint8_t t = in.U8();
    Segment segment;
    if (t == token::kNone) {
      if (in.remaining() != 0) break;  // bytes after the terminator
      return absl::OkStatus();
    }
    if (t == token::kIndex) {
      uint16_t index = in.U8();
      if (in.remaining() > 0 && in.Peek() == token::kIndex) {
        in.U8();
        index = static_cast<uint16_t>((index << 8) | in.U8());
      }
      segment = Segment::Index(index);
    } else if (t == token::kCustom) {
      const std::span<const uint8_t> text = in.Bytes(in.U8());
      segment = Segment::Custom(
          {reinterpret_cast<const char*>(text.data()), text.size()});
    } else {
      segment = Segment::Token(t);
    }
    if (out != nullptr && in.ok()) out->push_back(segment);
  }
  return Error(ErrorCode::kMalformedPacket,
               "address is truncated or not terminated by its last byte");
}

}  // namespace

absl::Status CheckAddress(std::span<const uint8_t> bytes) {
  return Parse(bytes, nullptr);
}

absl::StatusOr<Segments> ParseAddress(std::span<const uint8_t> bytes) {
  Segments segments;
  if (absl::Status status = Parse(bytes, &segments); !status.ok()) {
    return status;
  }
  return segments;
}

absl::StatusOr<size_t> EncodeAddress(std::span<const Segment> segments,
                                     std::span<uint8_t> out) {
  Writer w(out);
  bool after_index = false;
  for (const Segment& s : segments) {
    switch (s.kind) {
      case Segment::Kind::kToken:
        if (s.token == token::kNone || s.token == token::kIndex ||
            s.token == token::kCustom) {
          return absl::InvalidArgumentError(
              "0x00, 0x80 and 0xFE are not named tokens");
        }
        w.U8(s.token);
        break;
      case Segment::Kind::kIndex:
        if (after_index) {
          return absl::InvalidArgumentError(
              "an index directly after an index reads back as one u16 index");
        }
        w.U8(token::kIndex);
        if (s.index > 0xFF) {
          w.U8(static_cast<uint8_t>(s.index >> 8));
          w.U8(token::kIndex);
        }
        w.U8(static_cast<uint8_t>(s.index));
        break;
      case Segment::Kind::kCustom:
        if (s.text.size() > 0xFF) {
          return absl::InvalidArgumentError("custom segment exceeds 255 bytes");
        }
        w.U8(token::kCustom);
        w.U8(static_cast<uint8_t>(s.text.size()));
        w.Bytes(s.text);
        break;
    }
    after_index = s.kind == Segment::Kind::kIndex;
  }
  w.U8(token::kNone);
  if (!w.ok()) return absl::ResourceExhaustedError("address buffer too small");
  return w.written();
}

}  // namespace wsc
