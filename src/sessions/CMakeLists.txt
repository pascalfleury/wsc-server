# WSC sessions and their transports (docs/CODEC.org, Sessions).
add_library(wsc_sessions
  rtc_transports.cc rtc_transports.h session.cc session.h session_host.cc
  session_host.h signalling.cc signalling.h ws_server.cc ws_server.h)
wsc_target_defaults(wsc_sessions)
target_compile_definitions(wsc_sessions PRIVATE JSON_NOEXCEPTION)
target_link_libraries(wsc_sessions PUBLIC
  wsc_asio wsc_codec wsc_core wsc_memory wsc_routing absl::flat_hash_map
  absl::status absl::statusor absl::synchronization absl::time
  LibDataChannel::LibDataChannel
  PRIVATE nlohmann_json::nlohmann_json absl::base absl::log absl::strings)

wsc_add_test(session_test wsc_sessions wsc_testing)
wsc_add_test(signalling_test wsc_sessions nlohmann_json::nlohmann_json)
target_compile_definitions(signalling_test PRIVATE JSON_NOEXCEPTION)
