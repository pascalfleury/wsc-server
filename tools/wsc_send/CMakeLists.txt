# wsc-send: sends one WSC packet over WebSocket (PLAN.org, phase 3).
add_library(wsc_send_request request.cc request.h)
wsc_target_defaults(wsc_send_request)
target_link_libraries(wsc_send_request PUBLIC
  wsc_codec absl::status absl::statusor PRIVATE absl::strings)

add_executable(wsc-send main.cc)
wsc_target_defaults(wsc-send)
target_link_libraries(wsc-send PRIVATE
  wsc_send_request LibDataChannel::LibDataChannel
  absl::flags absl::flags_parse absl::flags_usage absl::strings
  absl::synchronization absl::time)

wsc_add_test(request_test wsc_send_request)
