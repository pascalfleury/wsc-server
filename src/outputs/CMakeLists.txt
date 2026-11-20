# Output adapters (docs/ADAPTERS.org).
add_library(wsc_sacn sacn/e131.cc sacn/e131.h)
wsc_target_defaults(wsc_sacn)
target_link_libraries(wsc_sacn PUBLIC wsc_core absl::statusor
  PRIVATE wsc_codec absl::status absl::strings)

add_executable(e131_test sacn/e131_test.cc)
wsc_target_defaults(e131_test)
target_link_libraries(e131_test PRIVATE wsc_sacn GTest::gmock_main)
gtest_discover_tests(e131_test)
