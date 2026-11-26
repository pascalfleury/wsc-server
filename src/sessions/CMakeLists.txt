# WSC sessions and their transports (docs/CODEC.org, Sessions).
add_library(wsc_sessions session.cc session.h ws_server.cc ws_server.h)
wsc_target_defaults(wsc_sessions)
target_link_libraries(wsc_sessions PUBLIC
  wsc_asio wsc_codec wsc_core wsc_memory wsc_routing absl::flat_hash_map
  absl::status absl::statusor absl::synchronization absl::time
  PRIVATE LibDataChannel::LibDataChannel absl::base absl::log absl::strings)

wsc_add_test(session_test wsc_sessions wsc_testing)
