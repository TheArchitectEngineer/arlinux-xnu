cmake_minimum_required(VERSION 3.20)
project(arlinux_darling_ipc LANGUAGES C CXX ASM)

# Supply the parent-build context required by the selected upstream targets.
# Source lists, XNU definitions, MIG interfaces and RPC generation stay upstream.
get_filename_component(DARLING_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../third_party/darling" ABSOLUTE)
include(GNUInstallDirs)
set(TARGET_ARM64 ON)
set(TARGET_X86_64 OFF)
set(TARGET_I386 OFF)
set(APPLE_ARCH_PRIMARY arm64)
set(APPLE_TARGET_TRIPLET_PRIMARY aarch64-apple-darwin20)
set(GIT_BRANCH pinned-community-arm64)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${DARLING_SOURCE}"
    OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
configure_file("${DARLING_SOURCE}/src/include/darling-config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/darling-config.h")
include_directories("${DARLING_SOURCE}/src/include" "${CMAKE_CURRENT_BINARY_DIR}/include")

add_subdirectory("${DARLING_SOURCE}/src/external/bootstrap_cmds" bootstrap_cmds)
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
