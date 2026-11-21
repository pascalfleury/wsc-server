add_subdirectory(core)
add_subdirectory(memory)
add_subdirectory(outputs)
add_subdirectory(platform)
add_subdirectory(routing)
add_subdirectory(shaping)

add_executable(wsc-server main.cc)
wsc_target_defaults(wsc-server)
target_link_libraries(wsc-server PRIVATE
  wsc_app wsc_platform
  absl::flags absl::flags_parse absl::flags_usage
  absl::log absl::log_globals absl::log_initialize absl::status)
