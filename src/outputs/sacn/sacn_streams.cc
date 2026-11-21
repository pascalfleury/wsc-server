#include "src/outputs/sacn/sacn_streams.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "libwsc/error.h"
#include "libwsc/packet.h"
#include "libwsc/transport.h"
#include "libwsc/types.h"
#include "src/outputs/sacn/e131.h"

namespace wsc_server {
namespace {

absl::Status Invalid(std::string_view message) {
  return wsc::Error(wsc::ErrorCode::kValidationFailed, message);
}

}  // namespace

Uuid SacnCid(const Uuid& server_id, std::string_view output_id) {
  return Uuid::FromName(server_id, absl::StrCat("sacn-output/", output_id));
}

absl::Status SacnStreams::Apply(const wsc::Packet& packet) {
  const auto* channels = std::get_if<wsc::StreamChannels>(&packet.body);
  if (channels == nullptr || !packet.transport) {
    return Invalid("sACN carries only STREAM_CHANNELS with a transport");
  }
  const wsc::TransportDescriptor& td = *packet.transport;
  if (channels->universe < e131::kMinUniverse ||
      channels->universe > e131::kMaxUniverse) {
    return Invalid(absl::StrCat("sACN universe ", channels->universe,
                                " is not in ", e131::kMinUniverse, "-",
                                e131::kMaxUniverse));
  }

  Key key{.destination = {}, .universe = channels->universe};
  if ((packet.header.flags & wsc::flags::kMulticast) != 0) {
    key.destination = {.address = e131::MulticastAddress(channels->universe),
                       .port = e131::kPort};
  } else if (td.addr_type == wsc::AddrType::kIpv4) {
    const std::span<const uint8_t> a = td.addr_data;  // [ip x4][port]
    key.destination = {.address = {a[0], a[1], a[2], a[3]},
                       .port = static_cast<uint16_t>((a[4] << 8) | a[5])};
  } else {
    return Invalid("sACN needs an IPv4 address or the MC flag");
  }

  uint8_t priority = e131::kDefaultPriority;
  for (const wsc::Param& param : td.params) {
    if (param.key == wsc::ParamKey::kPriority && param.value.size() == 1) {
      priority = param.value[0];
    }
  }
  if (priority > e131::kMaxPriority) {
    return Invalid(absl::StrCat("sACN priority ", priority, " is not in 0-",
                                e131::kMaxPriority));
  }

  const auto [it, inserted] = streams_.try_emplace(key, config_.limits);
  Stream& stream = it->second;
  if (absl::Status status =
          stream.buffer.Apply(channels->start_channel, channels->values);
      !status.ok()) {
    if (inserted) streams_.erase(it);  // no stream for a rejected first slice
    return status;
  }
  if (channels->values.empty()) return absl::OkStatus();
  stream.priority = priority;
  Channels next;
  const std::span<const uint8_t> frame =
      stream.buffer.Frame(config_.partial_dmx);
  std::ranges::copy(frame, next.data.begin());
  next.size = frame.size();
  stream.slot.Update(next);
  return absl::OkStatus();
}

absl::Time SacnStreams::Tick(absl::Time now) {
  absl::Time next = absl::InfiniteFuture();
  for (auto& [key, stream] : streams_) {
    if (const Channels* channels = stream.slot.Poll(now)) {
      Send(key, stream, *channels);
    }
    next = std::min(next, stream.slot.NextDeadline());
  }
  return next;
}

void SacnStreams::Send(const Key& key, Stream& stream,
                       const Channels& channels) {
  const absl::StatusOr<size_t> size =
      e131::Encode({.cid = config_.cid,
                    .source_name = config_.source_name,
                    .priority = stream.priority,
                    .sync_address = 0,
                    .sequence = stream.sequence++,
                    .options = 0,
                    .universe = key.universe,
                    .slots = std::span(channels.data).first(channels.size)},
                   packet_);
  absl::Status status = size.status();
  if (size.ok()) {
    status = sender_.Send(key.destination, std::span(packet_).first(*size));
  }
  if (!status.ok()) {
    LOG_EVERY_N_SEC(WARNING, 5)
        << "sACN universe " << key.universe << ": " << status;
  }
}

}  // namespace wsc_server
