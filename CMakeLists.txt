cmake_minimum_required(VERSION 3.25)
project(wsc_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WSC_SANITIZE "" CACHE STRING "Sanitizers for project targets, e.g. address,undefined")
option(WSC_COVERAGE "Clang source-based coverage on project targets" OFF)
option(WSC_CLANG_TIDY "Run clang-tidy on project targets while building" OFF)
option(WSC_FUZZ "Build the libFuzzer targets in fuzz/ (Clang only)" OFF)

include(cmake/ProjectOptions.cmake)

find_package(absl CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(cppitertools CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

enable_testing()
include(GoogleTest)

add_subdirectory(testing)
add_subdirectory(libwsc)
add_subdirectory(src)
add_subdirectory(tests)
if(WSC_FUZZ)
  add_subdirectory(fuzz)
endif()
