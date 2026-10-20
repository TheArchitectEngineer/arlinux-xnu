project(mig)

cmake_minimum_required(VERSION 3.13)

find_package(BISON)
find_package(FLEX)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
if (NOT DARLING_HOST_MACOS)
	if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR TARGET_ARM64)
		add_definitions(-Du_int=unsigned -D__int64_t=int64_t -D__int32_t=int32_t -D__darwin_natural_t=unsigned -D__uint32_t=uint32_t -D__uint16_t=uint16_t -D__uint64_t=uint64_t -D_BSD_ARM__TYPES_H_ -D__arm64__=1)
	else()
		add_definitions(-Du_int=unsigned -D__int64_t=int64_t -D__int32_t=int32_t -D__darwin_natural_t=unsigned -D__uint32_t=uint32_t -D__uint16_t=uint16_t -D__uint64_t=uint64_t -D_BSD_I386__TYPES_H_)
	endif()
endif()
add_definitions('-DMIG_VERSION="1.0"')
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/darling/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/darling/include/mach)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj)

BISON_TARGET(MyParser migcom.tproj/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.c)
FLEX_TARGET(MyScanner migcom.tproj/lexxer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
ADD_FLEX_BISON_DEPENDENCY(MyScanner MyParser)

# Hide some unimportant warnings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-dangling-else -Wno-implicit-function-declaration")

set(mig_sources
	migcom.tproj/error.c
	migcom.tproj/global.c
	#migcom.tproj/handler.c
	migcom.tproj/header.c
	migcom.tproj/mig.c
	migcom.tproj/routine.c
	migcom.tproj/server.c
	migcom.tproj/statement.c
	migcom.tproj/string.c
	migcom.tproj/type.c
	migcom.tproj/user.c
	migcom.tproj/utils.c
	darling/src/xtracemig.c
	${BISON_MyParser_OUTPUTS}
    ${FLEX_MyScanner_OUTPUTS}
)

if (CMAKE_CROSSCOMPILING)
	if (DEFINED ENV{CC_FOR_BUILD} AND NOT "$ENV{CC_FOR_BUILD}" STREQUAL "")
		set(HOST_C_COMPILER "$ENV{CC_FOR_BUILD}")
	else()
		find_program(HOST_C_COMPILER NAMES clang-18 clang-19 clang-17 clang-16 clang gcc cc PATHS /usr/bin NO_DEFAULT_PATH)
		if (NOT HOST_C_COMPILER)
			find_program(HOST_C_COMPILER NAMES clang-18 clang-19 clang-17 clang-16 clang gcc cc)
		endif()
	endif()
	separate_arguments(HOST_C_COMPILER_CMD NATIVE_COMMAND "${HOST_C_COMPILER}")

	if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
		set(MIG_HOST_ARCH_DEFS -D_BSD_ARM__TYPES_H_ -D__arm64__=1)
	else()
		set(MIG_HOST_ARCH_DEFS -D_BSD_I386__TYPES_H_)
	endif()

	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin/migcom"
		COMMAND ${HOST_C_COMPILER_CMD} -std=c99 -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration
			-DDARLING "-D__private_extern__=" -D__LITTLE_ENDIAN__=1
			-Du_int=unsigned -D__int64_t=int64_t -D__int32_t=int32_t -D__darwin_natural_t=unsigned
			-D__uint32_t=uint32_t -D__uint16_t=uint16_t -D__uint64_t=uint64_t ${MIG_HOST_ARCH_DEFS}
			"-DMIG_VERSION=\\\"1.0\\\""
			-I "${CMAKE_CURRENT_SOURCE_DIR}/darling/include"
			-I "${CMAKE_CURRENT_SOURCE_DIR}/darling/include/mach"
			-I "${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj"
			-I "${CMAKE_CURRENT_BINARY_DIR}"
			-I "${CMAKE_SOURCE_DIR}/basic-headers"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/error.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/global.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/header.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/mig.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/routine.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/server.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/statement.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/string.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/type.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/user.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/utils.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/darling/src/xtracemig.c"
			"${CMAKE_CURRENT_BINARY_DIR}/parser.c"
			"${CMAKE_CURRENT_BINARY_DIR}/lexer.c"
			-o "${CMAKE_CURRENT_BINARY_DIR}/bin/migcom"
		DEPENDS
			${BISON_MyParser_OUTPUTS}
			${FLEX_MyScanner_OUTPUTS}
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/error.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/global.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/header.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/mig.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/routine.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/server.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/statement.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/string.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/type.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/user.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/utils.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/darling/src/xtracemig.c"
		COMMENT "Compiling host migcom for code generation"
	)
	add_custom_target(migcom DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bin/migcom")
	set(MIGCOM_PATH "${CMAKE_CURRENT_BINARY_DIR}/bin/migcom")
else()
	add_executable(migcom ${mig_sources})
	set(MIGCOM_PATH "${CMAKE_CURRENT_BINARY_DIR}/migcom")
endif()

add_custom_command(
	OUTPUT "${CMAKE_BINARY_DIR}/build-mig"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/mig.sh"
	COMMAND ${CMAKE_COMMAND} -E env
		awk
		-v "migcc=${CMAKE_C_COMPILER}"
		-v "migcom=${MIGCOM_PATH}"
		-f "${CMAKE_CURRENT_SOURCE_DIR}/darling/src/mig.awk"
		"${CMAKE_CURRENT_SOURCE_DIR}/migcom.tproj/mig.sh"
		> "${CMAKE_BINARY_DIR}/build-mig"
	COMMAND ${CMAKE_COMMAND} -E env /bin/chmod 0755 "${CMAKE_BINARY_DIR}/build-mig"
	)
add_custom_target(migexe DEPENDS "${CMAKE_BINARY_DIR}/build-mig")

add_dependencies(migexe migcom)


