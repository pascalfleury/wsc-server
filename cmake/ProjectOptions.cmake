# Build settings shared by all project targets (docs/CODING.org).

if(MSVC)
  # CMake's defaults enable exceptions and RTTI; we set both per target.
  string(REGEX REPLACE "/EH[a-z]+|/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Older libc++ (Apple) ships <stop_token> / std::jthread as experimental.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  "#include <stop_token>
   int main() { std::stop_source s; return s.stop_requested() ? 1 : 0; }"
  WSC_HAS_STOP_TOKEN)
if(NOT WSC_HAS_STOP_TOKEN)
  add_compile_options(-fexperimental-library)
  add_link_options(-fexperimental-library)
endif()

# Applies language, warning and instrumentation settings to a project target.
function(wsc_target_defaults target)
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR})
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHs-c- /GR-)
    target_compile_definitions(${target} PRIVATE
      _HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
  endif()
  if(WSC_SANITIZE)
    target_compile_options(${target} PRIVATE
      -fsanitize=${WSC_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${WSC_SANITIZE})
  endif()
  if(WSC_COVERAGE)
    target_compile_options(${target} PRIVATE
      -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${target} PRIVATE -fprofile-instr-generate)
  endif()
  if(WSC_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "clang-tidy")
  endif()
endfunction()

# wsc_add_test(<name> <libraries...>): builds <name>.cc as a GoogleTest binary.
function(wsc_add_test name)
  add_executable(${name} ${name}.cc)
  wsc_target_defaults(${name})
  target_link_libraries(${name} PRIVATE ${ARGN} GTest::gmock_main)
  gtest_discover_tests(${name})
endfunction()
