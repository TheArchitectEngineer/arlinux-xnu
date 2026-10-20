cmake_minimum_required(VERSION 3.20)
project(arlinux_darling_ipc LANGUAGES C CXX ASM)

# Supply the parent-build context required by the selected upstream targets.
# Source lists, XNU definitions, MIG interfaces and RPC generation live here.
get_filename_component(DARLING_SOURCE "${CMAKE_CURRENT_LIST_DIR}/source" ABSOLUTE)
include(GNUInstallDirs)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ARLINUX_SESSION_CORE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/session-core.cpp")
set(TARGET_ARM64 ON)
set(TARGET_X86_64 OFF)
set(TARGET_I386 OFF)
set(DSERVER_GUEST_PAGE_SHIFT 14 CACHE STRING "ARLinux guest page shift")
set(DSERVER_SINGLE_CPU ON CACHE BOOL "ARLinux single guest CPU")
set(APPLE_ARCH_PRIMARY arm64)
set(APPLE_TARGET_TRIPLET_PRIMARY aarch64-apple-darwin20)
set(GIT_BRANCH arlinux-integrated-runtime)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${DARLING_SOURCE}"
    OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
configure_file("${DARLING_SOURCE}/src/include/darling-config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/darling-config.h")
include_directories("${DARLING_SOURCE}/src/include" "${CMAKE_CURRENT_BINARY_DIR}/include")
add_compile_definitions(DARLING)
# Match the upstream parent policy for pre-C99 XNU/MIG sources on modern Clang.
add_compile_options(-Wno-error=implicit-function-declaration -Wno-error=implicit-int
    -Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types
    -Wno-error=incompatible-pointer-types)

add_subdirectory("${DARLING_SOURCE}/src/external/bootstrap_cmds" bootstrap_cmds)
target_include_directories(migcom PRIVATE "${DARLING_SOURCE}/basic-headers")
include("${DARLING_SOURCE}/cmake/mig.cmake")
add_subdirectory("${DARLING_SOURCE}/src/libsimple" libsimple-linux)

# Use Darling's native signal-header generator, without building its launcher.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/src/startup")
add_executable(rtsig "${DARLING_SOURCE}/src/startup/rtsig.c")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/src/startup/rtsig.h"
    COMMAND $<TARGET_FILE:rtsig> "${CMAKE_BINARY_DIR}/src/startup/rtsig.h"
    DEPENDS rtsig VERBATIM)
add_custom_target(rtsig_h DEPENDS "${CMAKE_BINARY_DIR}/src/startup/rtsig.h")

add_subdirectory("${DARLING_SOURCE}/src/external/darlingserver" darlingserver)

# A shared library otherwise exports every upstream XNU interface and retains
# unreachable kernel paths that the standalone executable discarded by GC.
target_link_options(arlinux_mach_core PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/session-core.map")
set_property(TARGET arlinux_mach_core APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/session-core.map")
