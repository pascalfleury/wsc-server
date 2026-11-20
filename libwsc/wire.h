#ifndef WSC_SERVER_LIBWSC_WIRE_H_
#define WSC_SERVER_LIBWSC_WIRE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace wsc {

// Big-endian cursor over received bytes. A read past the end sets a sticky
// failure and returns zeros / empty spans, so a parser checks ok() once.
class Reader {
 public:
  explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

  bool ok() const { return ok_; }
  size_t remaining() const { return bytes_.size(); }
  // Next byte without consuming it; 0 at the end (callers check remaining()).
  uint8_t Peek() const { return bytes_.empty() ? 0 : bytes_[0]; }

  uint8_t U8() { return static_cast<uint8_t>(Uint(1)); }
  uint16_t U16() { return static_cast<uint16_t>(Uint(2)); }
  uint32_t U32() { return static_cast<uint32_t>(Uint(4)); }
  uint64_t U64() { return Uint(8); }

  std::span<const uint8_t> Bytes(size_t n) {
    if (!ok_ || n > bytes_.size()) {
      ok_ = false;
      return {};
    }
    std::span<const uint8_t> out = bytes_.first(n);
    bytes_ = bytes_.subspan(n);
    return out;
  }
  std::span<const uint8_t> Rest() { return Bytes(bytes_.size()); }

  // A UTF-8 string with a u16 length prefix (not checked for valid UTF-8).
  std::string_view String16() {
    const std::span<const uint8_t> s = Bytes(U16());
    return {reinterpret_cast<const char*>(s.data()), s.size()};
  }

 private:
  uint64_t Uint(size_t n) {
    uint64_t v = 0;
    for (const uint8_t b : Bytes(n)) v = (v << 8) | b;
    return v;
  }

  std::span<const uint8_t> bytes_;
  bool ok_ = true;
};

// Big-endian cursor over an output buffer. Writing past the end sets a sticky
// failure; nothing is written beyond the buffer.
class Writer {
 public:
  explicit Writer(std::span<uint8_t> out) : out_(out) {}

  bool ok() const { return ok_; }
  size_t written() const { return pos_; }

  void U8(uint8_t v) { Uint(v, 1); }
  void U16(uint16_t v) { Uint(v, 2); }
  void U32(uint32_t v) { Uint(v, 4); }
  void U64(uint64_t v) { Uint(v, 8); }

  void Bytes(std::span<const uint8_t> bytes) {
    uint8_t* dst = Reserve(bytes.size());
    if (dst != nullptr && !bytes.empty()) {
      std::memcpy(dst, bytes.data(), bytes.size());
    }
  }
  void Bytes(std::string_view s) {
    Bytes(std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
  }

  // Writes a big-endian u16 at an earlier position (length back-patching).
  void PatchU16(size_t pos, uint16_t v) {
    if (ok_ && pos + 2 <= pos_) {
      out_[pos] = static_cast<uint8_t>(v >> 8);
      out_[pos + 1] = static_cast<uint8_t>(v);
    }
  }

 private:
  uint8_t* Reserve(size_t n) {
    if (!ok_ || n > out_.size() - pos_) {
      ok_ = false;
      return nullptr;
    }
    uint8_t* p = out_.data() + pos_;
    pos_ += n;
    return p;
  }
  void Uint(uint64_t v, size_t n) {
    uint8_t* dst = Reserve(n);
    if (dst == nullptr) return;
    for (size_t i = 0; i < n; ++i) {
      dst[i] = static_cast<uint8_t>(v >> (8 * (n - 1 - i)));
    }
  }

  std::span<uint8_t> out_;
  size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_WIRE_H_
