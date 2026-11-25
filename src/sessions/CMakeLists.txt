# WSC sessions and their transports (docs/CODEC.org, Sessions).
add_library(wsc_sessions session.cc session.h)
wsc_target_defaults(wsc_sessions)
target_link_libraries(wsc_sessions PUBLIC
  wsc_codec wsc_core wsc_memory wsc_routing absl::status absl::time
  PRIVATE absl::log absl::statusor)

wsc_add_test(session_test wsc_sessions wsc_testing)
