# Integration tests; unit tests sit next to the code.
wsc_add_test(golden_test wsc_codec)
target_compile_definitions(golden_test PRIVATE
  WSC_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

wsc_add_test(udp_sender_test wsc_outputs)
wsc_add_test(ws_server_test wsc_sessions wsc_testing
  LibDataChannel::LibDataChannel absl::strings absl::synchronization)
wsc_add_test(webrtc_test wsc_sessions wsc_testing
  LibDataChannel::LibDataChannel nlohmann_json::nlohmann_json absl::strings
  absl::synchronization)
target_compile_definitions(webrtc_test PRIVATE JSON_NOEXCEPTION)
