# Output adapters (docs/ADAPTERS.org).
add_library(wsc_outputs
  datagram_sender.h udp_sender.cc udp_sender.h
  sacn/e131.cc sacn/e131.h sacn/sacn_adapter.cc sacn/sacn_adapter.h
  sacn/sacn_streams.cc sacn/sacn_streams.h)
wsc_target_defaults(wsc_outputs)
target_link_libraries(wsc_outputs PUBLIC
  wsc_asio wsc_codec wsc_core wsc_routing wsc_shaping absl::flat_hash_map
  absl::status absl::statusor absl::time
  PRIVATE absl::log absl::strings)

foreach(test e131_test sacn_streams_test sacn_adapter_test)
  add_executable(${test} sacn/${test}.cc)
  wsc_target_defaults(${test})
  target_link_libraries(${test} PRIVATE wsc_outputs wsc_testing
    GTest::gmock_main)
  gtest_discover_tests(${test})
endforeach()
