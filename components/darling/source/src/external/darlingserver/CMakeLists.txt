project(darlingserver)

cmake_minimum_required(VERSION 3.13)

# Zero retains the upstream host-page policy. Embedders may select a larger
# guest granule; Linux hooks still exchange counts in host-page units.
set(DSERVER_GUEST_PAGE_SHIFT 0 CACHE STRING "Guest VM page shift, or zero for host geometry")
if (NOT DSERVER_GUEST_PAGE_SHIFT MATCHES "^(0|12|14|16)$")
	message(FATAL_ERROR "Unsupported guest VM page shift")
endif()
add_compile_definitions(DSERVER_GUEST_PAGE_SHIFT=${DSERVER_GUEST_PAGE_SHIFT})
option(DSERVER_SINGLE_CPU "Advertise and schedule one guest CPU" OFF)
if (DSERVER_SINGLE_CPU)
	add_compile_definitions(DSERVER_SINGLE_CPU=1)
endif()

option(DSERVER_TOOLS "Build darlingserver tools (for debugging)" OFF)
option(DSERVER_ASAN "Build darlingserver with ASAN" OFF)
option(DSERVER_UBSAN "Build darlingserver with UBSAN" OFF)

if (DSERVER_TOOLS)
	add_subdirectory(tools)
endif()

if (DSERVER_ASAN)
	add_compile_definitions(
		DSERVER_ASAN=1
	)
else()
	add_compile_definitions(
		DSERVER_ASAN=0
	)
endif()

if (DSERVER_UBSAN)
	add_compile_definitions(
		DSERVER_UBSAN=1
	)
else()
	add_compile_definitions(
		DSERVER_UBSAN=0
	)
endif()

set(dserver_sanitizers "")
if (DSERVER_ASAN)
	if (NOT (dserver_sanitizers STREQUAL ""))
		set(dserver_sanitizers "${dserver_sanitizers},")
	endif()
	set(dserver_sanitizers "${dserver_sanitizers}address")
endif()
if (DSERVER_UBSAN)
	if (NOT (dserver_sanitizers STREQUAL ""))
		set(dserver_sanitizers "${dserver_sanitizers},")
	endif()
	set(dserver_sanitizers "${dserver_sanitizers}undefined")
endif()

option(DSERVER_EXTENDED_DEBUG "Build darlingserver with extended debugging support" OFF)

if (DSERVER_EXTENDED_DEBUG)
	add_compile_definitions(
		DSERVER_EXTENDED_DEBUG=1
	)
else()
	add_compile_definitions(
		DSERVER_EXTENDED_DEBUG=0
	)
endif()

option(DSERVER_SINGLE_THREADED "Only use a single thread per workqueue in darlingserver" ON)

if (DSERVER_SINGLE_THREADED)
	add_compile_definitions(
		DSERVER_SINGLE_THREADED=1
	)
else()
	add_compile_definitions(
		DSERVER_SINGLE_THREADED=0
	)
endif()

add_subdirectory(duct-tape)

include_directories(
	include
	internal-include
	${CMAKE_CURRENT_BINARY_DIR}/include
	${CMAKE_CURRENT_BINARY_DIR}/internal-include
	${CMAKE_BINARY_DIR}/src/startup # for `rtsig.h`
)

add_custom_command(
	OUTPUT
		${CMAKE_CURRENT_BINARY_DIR}/include/darlingserver/rpc.h
		${CMAKE_CURRENT_BINARY_DIR}/internal-include/darlingserver/rpc.internal.h
		${CMAKE_CURRENT_BINARY_DIR}/src/rpc.c
	COMMAND
		${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-rpc-wrappers.py
		${CMAKE_CURRENT_BINARY_DIR}/include/darlingserver/rpc.h
		${CMAKE_CURRENT_BINARY_DIR}/internal-include/darlingserver/rpc.internal.h
		${CMAKE_CURRENT_BINARY_DIR}/src/rpc.c
		\"../include/darlingserver/rpc.h\"
	VERBATIM
	MAIN_DEPENDENCY
		${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-rpc-wrappers.py
)

add_custom_target(generate_dserver_rpc_wrappers
	ALL
	DEPENDS
		${CMAKE_CURRENT_BINARY_DIR}/include/darlingserver/rpc.h
		${CMAKE_CURRENT_BINARY_DIR}/internal-include/darlingserver/rpc.internal.h
		${CMAKE_CURRENT_BINARY_DIR}/src/rpc.c
)

add_library(arlinux_mach_core SHARED
	"${ARLINUX_SESSION_CORE_SOURCE}"
	src/server.cpp
	src/message.cpp
	src/call.cpp
	src/registry.cpp
	src/logging.cpp
	src/process.cpp
	src/thread.cpp
	src/utility.cpp
	src/kqchan.cpp
	src/async-writer.cpp
	src/stack-pool.cpp
)

add_dependencies(arlinux_mach_core
	generate_dserver_rpc_wrappers
	rtsig_h
)

target_link_libraries(arlinux_mach_core PRIVATE
	darlingserver_duct_tape
)

find_library(LIBUCONTEXT ucontext)
if (LIBUCONTEXT)
	target_link_libraries(arlinux_mach_core PRIVATE ${LIBUCONTEXT})
	find_path(LIBUCONTEXT_INCLUDE_DIR NAMES libucontext/libucontext.h PATHS "${CMAKE_FIND_ROOT_PATH}/include" "${CMAKE_INSTALL_PREFIX}/include")
	if (LIBUCONTEXT_INCLUDE_DIR)
		target_include_directories(arlinux_mach_core PRIVATE ${LIBUCONTEXT_INCLUDE_DIR})
	elseif (DEFINED CMAKE_FIND_ROOT_PATH)
		target_include_directories(arlinux_mach_core PRIVATE ${CMAKE_FIND_ROOT_PATH}/include)
	endif()
endif()

find_library(LIBSEMAPHORE android-posix-semaphore)
if (LIBSEMAPHORE)
	target_link_libraries(arlinux_mach_core PRIVATE ${LIBSEMAPHORE})
endif()

target_compile_options(arlinux_mach_core PRIVATE
	-pthread
	-std=c++17
)
target_link_options(arlinux_mach_core PRIVATE
	-pthread
	-Wl,--gc-sections
	-Wl,--no-undefined
)

if (DSERVER_ASAN OR DSERVER_UBSAN)
	target_compile_options(arlinux_mach_core PRIVATE
		-fsanitize=${dserver_sanitizers}
		-fsanitize-recover=all
		-fno-omit-frame-pointer
		-g
		-Og
	)
	target_link_options(arlinux_mach_core PRIVATE
		-fsanitize=${dserver_sanitizers}
		-fsanitize-recover=all
	)
endif()

if (DSERVER_EXTENDED_DEBUG)
	target_compile_options(arlinux_mach_core PRIVATE
		-fno-omit-frame-pointer
	)
endif()

install(TARGETS arlinux_mach_core LIBRARY DESTINATION lib)

if (ANDROID)
	set_target_properties(arlinux_mach_core PROPERTIES
		INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
		BUILD_WITH_INSTALL_RPATH TRUE
	)
endif()

