# WSC codec: encode/decode/validate, no I/O (docs/CODEC.org).
add_library(wsc_codec
  address.cc address.h error.cc error.h packet.cc packet.h transport.cc
  transport.h types.h validate.cc validate.h wire.h)
wsc_target_defaults(wsc_codec)
target_link_libraries(wsc_codec PUBLIC
  absl::inlined_vector absl::status absl::statusor
  PRIVATE absl::cord)

wsc_add_test(address_test wsc_codec)
wsc_add_test(packet_test wsc_codec)
wsc_add_test(validate_test wsc_codec)
