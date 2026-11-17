# Low-level value types and services with no I/O module dependencies.
add_library(wsc_core
  clock.h identity.cc identity.h uuid.cc uuid.h)
wsc_target_defaults(wsc_core)
target_link_libraries(wsc_core PUBLIC
  absl::random_bit_gen_ref absl::random_random absl::status absl::statusor
  absl::strings absl::time
  PRIVATE absl::algorithm_container cppitertools::cppitertools)

# The application object: the only target that may depend on every module.
add_library(wsc_app app.cc app.h)
wsc_target_defaults(wsc_app)
target_link_libraries(wsc_app PUBLIC wsc_core absl::status absl::statusor
  PRIVATE wsc_asio absl::log absl::memory absl::random_random)

wsc_add_test(uuid_test wsc_core)
wsc_add_test(identity_test wsc_core)
wsc_add_test(app_test wsc_app)
