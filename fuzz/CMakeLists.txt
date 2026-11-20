# libFuzzer targets (Clang only). Run: build/<preset>/fuzz/decode_fuzzer tests/golden
add_executable(decode_fuzzer decode_fuzzer.cc)
wsc_target_defaults(decode_fuzzer)
target_compile_options(decode_fuzzer PRIVATE -fsanitize=fuzzer)
target_link_options(decode_fuzzer PRIVATE -fsanitize=fuzzer)
target_link_libraries(decode_fuzzer PRIVATE wsc_codec)
