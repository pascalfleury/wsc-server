# Integration tests; unit tests sit next to the code.
wsc_add_test(golden_test wsc_codec)
target_compile_definitions(golden_test PRIVATE
  WSC_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

wsc_add_test(udp_sender_test wsc_outputs)
