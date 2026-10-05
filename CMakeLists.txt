cmake_minimum_required(VERSION 3.19)
option(DISCORD_BRIDGE_BUILD_32BIT "Build the 32-bit plugin used by 32-bit SA-MP servers" OFF)
set(DISCORD_BRIDGE_VERSION "1.0.0" CACHE STRING "Discord Bridge release version")

if(NOT DISCORD_BRIDGE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
	message(FATAL_ERROR "DISCORD_BRIDGE_VERSION must use MAJOR.MINOR.PATCH format (got '${DISCORD_BRIDGE_VERSION}')")
endif()

# Set the architecture flags before project() performs compiler ABI
# detection.  Applying -m32 afterwards leaves CMake believing the target is
# 64-bit, which makes architecture-aware dependency packages reject their
# correct 32-bit variants.
if(DISCORD_BRIDGE_BUILD_32BIT AND NOT WIN32)
	set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -m32")
	set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -m32")
endif()

project(discord-bridge LANGUAGES C CXX VERSION ${DISCORD_BRIDGE_VERSION})
if(POLICY CMP0167)
	cmake_policy(SET CMP0167 OLD)
endif()
# The open.mp SDK contains older third-party CMakeLists.  This keeps
# them configurable with newer CMake releases while preserving the project's
# declared minimum version.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(DISCORD_BRIDGE_VERSION ${PROJECT_VERSION})

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
	@ONLY
)

option(DISCORD_BRIDGE_STATIC_DEPS "Prefer static OpenSSL/Boost libraries" OFF)
set(DISCORD_BRIDGE_OMP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/omp-sdk" CACHE PATH
	"Path to the open.mp SDK checkout (contains CMakeLists.txt)")
set(DISCORD_BRIDGE_SAMP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/samp-sdk" CACHE PATH
	"Path to the SA-MP/open.mp AMX SDK root (contains amx/amx.h)")

# Platform-specific settings
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_definitions(
		BOOST_ALL_NO_LIB
		_CRT_SECURE_NO_WARNINGS
		NOMINMAX
		_WIN32_WINNT=0x0601
		_SCL_SECURE_NO_WARNINGS
	)
elseif(UNIX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	if(DISCORD_BRIDGE_STATIC_DEPS)
		set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
		add_link_options("-static-libgcc" "-static-libstdc++")
	endif()
endif()

# Find dependencies
if(DISCORD_BRIDGE_STATIC_DEPS)
	# Static OpenSSL archives do not need the host's pkg-config dependency
	# graph.  Locate the requested headers and archives directly so a host
	# OpenSSL installation cannot reintroduce shared compression libraries.
	find_path(OPENSSL_INCLUDE_DIR
		NAMES openssl/ssl.h
		HINTS "${OPENSSL_ROOT_DIR}"
		PATH_SUFFIXES include
	)
	find_library(OPENSSL_SSL_LIBRARY
		NAMES ssl
		HINTS "${OPENSSL_ROOT_DIR}"
		PATH_SUFFIXES lib
	)
	find_library(OPENSSL_CRYPTO_LIBRARY
		NAMES crypto
		HINTS "${OPENSSL_ROOT_DIR}"
		PATH_SUFFIXES lib
	)
	if(NOT OPENSSL_INCLUDE_DIR OR NOT OPENSSL_SSL_LIBRARY OR NOT OPENSSL_CRYPTO_LIBRARY)
		message(FATAL_ERROR
			"DISCORD_BRIDGE_STATIC_DEPS requires OpenSSL headers and static libssl/libcrypto archives")
	endif()
else()
	set(OPENSSL_USE_STATIC_LIBS OFF)
	set(OPENSSL_MSVC_STATIC_RT ON)
	find_package(OpenSSL REQUIRED)
endif()

set(Boost_USE_STATIC_LIBS ${DISCORD_BRIDGE_STATIC_DEPS})
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.70 REQUIRED COMPONENTS system)

if(NOT DISCORD_BRIDGE_SAMP_SDK_DIR)
	find_path(DISCORD_BRIDGE_SAMP_SDK_DIR amx/amx.h)
endif()
if(NOT DISCORD_BRIDGE_SAMP_SDK_DIR)
	message(FATAL_ERROR "Could not find the AMX SDK. Set DISCORD_BRIDGE_SAMP_SDK_DIR to a directory containing amx/amx.h.")
endif()

if(NOT EXISTS "${DISCORD_BRIDGE_OMP_SDK_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"Could not find the open.mp SDK. Set DISCORD_BRIDGE_OMP_SDK_DIR to an open.mp SDK checkout.")
endif()

# Add OMP SDK
add_subdirectory("${DISCORD_BRIDGE_OMP_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/omp-sdk")

# Include directories
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/third_party
	${CMAKE_CURRENT_BINARY_DIR}
	${DISCORD_BRIDGE_SAMP_SDK_DIR}
	${Boost_INCLUDE_DIRS}
	${OPENSSL_INCLUDE_DIR}
)

# One shared library exports both the open.mp component entry point and the
# classic SA-MP plugin entry points.  The same binary can therefore be copied
# to either server's loader directory.
add_library(discord-bridge SHARED
	src/main.cpp
	src/samp-main.cpp
	src/samp-amx.cpp
	src/samp-config.cpp
	src/samp-pawn.cpp
	src/discord-component.cpp
	src/discord-websocket.cpp
	src/discord-tls.cpp
	src/discord-update-checker.cpp
	src/discord-http.cpp
	src/discord-log.cpp
	src/discord-bot.cpp
	src/discord-channel.cpp
	src/discord-guild.cpp
	src/discord-message.cpp
	src/discord-user.cpp
	src/discord-role.cpp
	src/natives.cpp
	src/natives-interactions.cpp
	src/natives-management.cpp
	src/discord-builders.cpp
	src/utils.cpp
)

target_compile_definitions(discord-bridge PRIVATE
	HAVE_STDINT_H=1
	PAWN_CELL_SIZE=32
)

if(WIN32)
	target_sources(discord-bridge PRIVATE src/plugin.def)
endif()

# Link libraries
target_link_libraries(discord-bridge PRIVATE
	OMP-SDK
	${Boost_LIBRARIES}
)

if(DISCORD_BRIDGE_STATIC_DEPS)
	# The locally built OpenSSL has no compression dependencies.  Link the
	# archives directly instead of inheriting the host OpenSSL pkg-config
	# interface, which would add unrelated shared zlib/brotli/zstd libraries.
	target_link_libraries(discord-bridge PRIVATE
		${OPENSSL_SSL_LIBRARY}
		${OPENSSL_CRYPTO_LIBRARY}
	)
else()
	target_link_libraries(discord-bridge PRIVATE
		OpenSSL::SSL
		OpenSSL::Crypto
	)
endif()

if(UNIX)
	# open.mp's static Linux server exports its own OpenSSL symbols globally.
	# Keep this component and its OpenSSL dependency in one ELF lookup group so
	# TLS objects allocated by the plugin are also freed by that same library.
	target_link_options(discord-bridge PRIVATE "-Wl,-Bgroup")
	if(DISCORD_BRIDGE_STATIC_DEPS)
		# Static OpenSSL/Boost objects become part of this DSO.  Bind their
		# internal references locally and do not re-export them to the server.
		target_link_options(discord-bridge PRIVATE "-Wl,-Bsymbolic" "-Wl,--exclude-libs,ALL")
	endif()
endif()

if(WIN32)
	target_link_libraries(discord-bridge PRIVATE Crypt32)
endif()

if(UNIX AND NOT APPLE)
	target_link_libraries(discord-bridge PRIVATE rt pthread)
endif()

# Compiler warnings
if(MSVC)
	target_compile_options(discord-bridge PRIVATE /W4 /permissive- /MP)
else()
	target_compile_options(discord-bridge PRIVATE -Wall -Wextra -pedantic)
endif()

# Output directory
set_target_properties(discord-bridge PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	OUTPUT_NAME "discord-bridge"
	PREFIX ""
	SKIP_BUILD_RPATH TRUE
	BUILD_WITH_INSTALL_RPATH TRUE
	INSTALL_RPATH "$ORIGIN"
)

# Keep the dynamically linked Boost.System runtime beside the plugin.  The
# component is loaded by 32-bit servers, so relying on a host-wide 64-bit
# Boost installation would make an otherwise correct build unloadable.
# Boost 1.69+ ships Boost.System as a header-only INTERFACE target on some
# distributions; there is no runtime file to copy in that case.
set(DISCORD_BRIDGE_BOOST_SYSTEM_TYPE "")
if(TARGET Boost::system)
	get_target_property(DISCORD_BRIDGE_BOOST_SYSTEM_TYPE Boost::system TYPE)
endif()
if(TARGET Boost::system AND NOT DISCORD_BRIDGE_STATIC_DEPS AND NOT DISCORD_BRIDGE_BOOST_SYSTEM_TYPE STREQUAL "INTERFACE_LIBRARY")
	add_custom_command(TARGET discord-bridge POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:Boost::system>"
			"$<TARGET_FILE_DIR:discord-bridge>"
		VERBATIM
	)
endif()

# Compatibility includes are maintained in omp-Discord-Bridge-compat.
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/discord-bridge.inc"
	"${CMAKE_BINARY_DIR}/pawno/include/discord-bridge.inc"
	@ONLY
)

if(BUILD_TESTING)
	add_executable(discord-bridge-utils-tests
		tests/utils-tests.cpp
		src/utils.cpp
	)
	target_include_directories(discord-bridge-utils-tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	add_test(NAME discord-bridge-utils COMMAND discord-bridge-utils-tests)

	add_executable(discord-bridge-builders-tests
		tests/builders-tests.cpp
		src/discord-builders.cpp
	)
	target_include_directories(discord-bridge-builders-tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/third_party
	)
	add_test(NAME discord-bridge-builders COMMAND discord-bridge-builders-tests)
endif()

if(BUILD_TESTING)
	find_package(Threads REQUIRED)
	add_executable(discord-bridge-rest-tests tests/rest-tests.cpp)
	target_include_directories(discord-bridge-rest-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_link_libraries(discord-bridge-rest-tests PRIVATE Threads::Threads)
	add_test(NAME discord-bridge-rest COMMAND discord-bridge-rest-tests)
	set_tests_properties(discord-bridge-rest PROPERTIES TIMEOUT 15)
	add_executable(discord-bridge-message-batches-tests tests/message-batches-tests.cpp)
	target_include_directories(discord-bridge-message-batches-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_link_libraries(discord-bridge-message-batches-tests PRIVATE Threads::Threads)
	add_test(NAME discord-bridge-message-batches COMMAND discord-bridge-message-batches-tests)
	set_tests_properties(discord-bridge-message-batches PROPERTIES TIMEOUT 45)
endif()

if(BUILD_TESTING)
	add_executable(discord-bridge-mentions-tests tests/mentions-tests.cpp)
	target_include_directories(discord-bridge-mentions-tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/third_party
	)
	add_test(NAME discord-bridge-mentions COMMAND discord-bridge-mentions-tests)
endif()
