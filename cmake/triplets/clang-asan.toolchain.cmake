# Toolchain for the x64-linux-asan triplet. No vptr check: the project builds
# without RTTI, which that check needs.
include("$ENV{VCPKG_ROOT}/scripts/toolchains/linux.cmake")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer")
