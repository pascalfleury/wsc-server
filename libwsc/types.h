#ifndef WSC_SERVER_LIBWSC_TYPES_H_
#define WSC_SERVER_LIBWSC_TYPES_H_

#include <cstdint>

// WSC 1.1.0 code tables (spec 02-05, 07). The enums are open: a decoded value
// outside the listed ones is kept as-is, and Validate() decides what to reject.
namespace wsc {

enum class MessageType : uint16_t {
  kStreamChannels = 0x1000,
  kStreamTimecode = 0x1003,
  kControlCue = 0x2000,
  kControlParam = 0x2001,
  kTunnelRaw = 0xE000,
  kStateQuery = 0xF000,
  kStateAnswer = 0xF001,
  kStateError = 0xF004,
};

// Header flag bits (spec 02). Bits 10-0 are reserved.
namespace flags {
inline constexpr uint16_t kTransport = 0x8000;  // TR
inline constexpr uint16_t kGateway = 0x4000;    // GW
inline constexpr uint16_t kAck = 0x2000;        // ACK
inline constexpr uint16_t kMulticast = 0x1000;  // MC
inline constexpr uint16_t kTimestamp = 0x0800;  // TS
}  // namespace flags

enum class Protocol : uint8_t {
  kDmx512 = 0x01,
  kArtNet = 0x02,
  kSacn = 0x03,
  kKinet = 0x04,
  kRdm = 0x05,
  kMidi = 0x10,
  kMidi2 = 0x11,
  kMsc = 0x12,
  kMtc = 0x13,
  kMmc = 0x14,
  kOsc = 0x15,
  kNdi = 0x20,
  kVisca = 0x21,
  kModbus = 0x30,
  kCanOpen = 0x31,
  kEtherCat = 0x32,
  kProfinet = 0x33,
  kGpiGpo = 0x40,
  kTally = 0x41,
  kHttp = 0xF0,
  kWebSocket = 0xF1,
  kMqtt = 0xF2,
  kRaw = 0xFF,
};

enum class Iface : uint8_t {
  kUdp = 0x01,
  kTcp = 0x02,
  kSerial = 0x03,
  kUsb = 0x04,
  kWebSocket = 0x05,
  kHttp = 0x06,
};

enum class AddrType : uint8_t {
  kNone = 0x00,
  kIpv4 = 0x01,
  kIpv6 = 0x02,
  kSerial = 0x03,
  kUsb = 0x04,
  kHostname = 0x05,
  kUrl = 0x06,
};

enum class ParamKey : uint8_t {
  kPriority = 0x01,
  kTtl = 0x02,
  kBaudRate = 0x03,
  kSequence = 0x04,
  kIfaceIdx = 0x05,
};

enum class CueAction : uint8_t {
  kLoad = 0x00,
  kStart = 0x01,
  kStop = 0x02,
  kPause = 0x03,
  kResume = 0x04,
  kRelease = 0x05,
};

enum class ValueType : uint8_t {
  kU8 = 0x01,
  kU16 = 0x02,
  kU32 = 0x03,
  kU64 = 0x04,
  kI8 = 0x05,
  kI16 = 0x06,
  kI32 = 0x07,
  kI64 = 0x08,
  kF32 = 0x09,
  kF64 = 0x0A,
  kString = 0x0B,
  kBool = 0x0C,
};

// 0x80+ are our extension (CODEC.org, D-14).
enum class QueryType : uint8_t {
  kKeepalive = 0x00,
  kDiscover = 0x80,
  kRdmGet = 0x81,
  kRdmSet = 0x82,
};

enum class AnswerStatus : uint8_t {
  kSuccess = 0x00,
  kPartial = 0x01,
  kError = 0x02,
  kNotFound = 0x03,
  kNotSupported = 0x04,
  kBusy = 0x05,
};

enum class AnswerFormat : uint8_t { kRaw = 0x00, kJson = 0x01 };

enum class ErrorCode : uint16_t {
  kProtocolVersion = 0x0001,
  kInvalidMessageType = 0x0002,
  kMalformedPacket = 0x0003,
  kIncompatibleProtocol = 0x0004,
  kIncompatibleIface = 0x0005,
  kMissingTransport = 0x0006,
  kMissingTransportFlag = 0x0007,
  kInvalidTunnel = 0x0008,
  kNoRoute = 0x0009,
  kValidationFailed = 0x000A,
  kConfigError = 0x000B,
};

}  // namespace wsc

#endif  // WSC_SERVER_LIBWSC_TYPES_H_
