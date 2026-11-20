# Dependencies for the `asan` preset, built with the same compiler and
# sanitizers as the project. Abseil requires this: some of its types (Cord)
# change layout when a sanitizer is enabled.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/clang-asan.toolchain.cmake")
