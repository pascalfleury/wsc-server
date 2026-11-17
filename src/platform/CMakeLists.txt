# Asio without exceptions; include it only through src/platform/asio.h.
add_library(wsc_asio INTERFACE)
target_link_libraries(wsc_asio INTERFACE asio::asio)
target_compile_definitions(wsc_asio INTERFACE
  ASIO_STANDALONE ASIO_NO_EXCEPTIONS ASIO_NO_TYPEID
  ASIO_DISABLE_STD_SOURCE_LOCATION ASIO_DISABLE_STD_EXPERIMENTAL_SOURCE_LOCATION)

add_library(wsc_platform foreground.cc foreground.h paths.cc paths.h asio.h)
wsc_target_defaults(wsc_platform)
target_link_libraries(wsc_platform PUBLIC
  absl::function_ref absl::status absl::statusor
  PRIVATE wsc_asio absl::strings)

wsc_add_test(paths_test wsc_platform)
wsc_add_test(foreground_test wsc_platform absl::synchronization)
