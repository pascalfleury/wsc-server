# Fakes shared by unit tests (docs/CODING.org, Testing).
add_library(wsc_testing INTERFACE)
target_include_directories(wsc_testing INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(wsc_testing INTERFACE wsc_core absl::time)

wsc_add_test(fake_clock_test wsc_testing)
