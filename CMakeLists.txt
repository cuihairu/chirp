cmake_minimum_required(VERSION 3.21)
project(chirp_monorepo)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(CHIRP_FETCH_DEPS "Allow CMake to download third-party dependencies when not installed" ON)
# Default ON so bare configure/coverage presets also compile-verify the
# example programs (sdk_example is the --smoke-sdk probe; integration_example
# is the integrator tour). Turn off with -DCHIRP_BUILD_SDK_EXAMPLES=OFF.
option(CHIRP_BUILD_SDK_EXAMPLES "Build the SDK example programs" ON)
option(CHIRP_ENABLE_COVERAGE "Instrument targets with gcov coverage flags" OFF)
option(CHIRP_ENABLE_WARNINGS "Build with -Wall -Wextra -Wpedantic" ON)
option(CHIRP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(CHIRP_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(WARNING "CHIRP_ENABLE_COVERAGE is only supported on GNU/Clang compilers; ignoring")
  else()
    message(STATUS "Coverage instrumentation enabled (gcov)")
    add_compile_options(-fprofile-arcs -ftest-coverage -O0 -g)
    add_link_options(-fprofile-arcs -ftest-coverage)
  endif()
endif()

if(CHIRP_ENABLE_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CHIRP_WARNINGS_AS_ERRORS)
      add_compile_options(-Werror)
    endif()
  else()
    message(WARNING "CHIRP_ENABLE_WARNINGS is only supported on GNU/Clang compilers; ignoring")
  endif()
endif()

if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

# Add cmake module path for custom Find modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# Try to find Protobuf via vcpkg or system
find_package(Protobuf CONFIG QUIET)

# If Protobuf CONFIG is not found, try regular FindProtobuf
if(NOT Protobuf_FOUND)
  find_package(Protobuf QUIET)
endif()

# If still not found, use pre-generated proto files as fallback
if(NOT Protobuf_FOUND)
  message(STATUS "Protobuf not found, using pre-generated proto files from proto/cpp/proto/")

  # Set up paths to pre-generated protobuf files
  set(PROTOBUF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/proto/cpp/proto" CACHE PATH "Proto files include dir")

  message(STATUS "Using pre-generated proto files from: ${PROTOBUF_INCLUDE_DIRS}")
  message(STATUS "Pre-generated proto files will be built from proto/CMakeLists.txt")

  # Skip Abseil requirement since we're using pre-generated files
  set(CHIRP_NEEDS_ABSL OFF)
else()
  message(STATUS "Protobuf found: ${Protobuf_VERSION}")

  # Find Abseil (required for Protobuf 6.x)
  set(CHIRP_NEEDS_ABSL OFF)
  if(NOT Protobuf_VERSION)
    # Older CMake FindProtobuf may not expose a version; keep the previous behavior to be safe.
    set(CHIRP_NEEDS_ABSL ON)
  elseif(Protobuf_VERSION VERSION_GREATER_EQUAL "6.0")
    set(CHIRP_NEEDS_ABSL ON)
  endif()

if(CHIRP_NEEDS_ABSL)
  # Try using PkgConfig for system Abseil first
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(absl_pkg absl_base absl_log)
  endif()

  if(absl_pkg_FOUND)
    # Use the installed Abseil discovered through pkg-config/vcpkg.
    message(STATUS "Using system Abseil from: ${absl_pkg_INCLUDE_DIRS}")
    include_directories(SYSTEM ${absl_pkg_INCLUDE_DIRS})
    link_directories(${absl_pkg_LIBRARY_DIRS})
    add_compile_options(${absl_pkg_CFLAGS_OTHER})
  else()
    # Use FetchContent for Abseil
    include(FetchContent)
    FetchContent_Declare(
      abseil_cpp
      GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
      GIT_TAG 20240116.2
      GIT_SHALLOW TRUE
    )
    set(ABSL_PROPAGATE_CXX_STD ON)
    set(ABSL_ENABLE_INSTALL ON)
    FetchContent_MakeAvailable(abseil_cpp)
  endif()
  endif()
endif()

# Resolve ASIO once at the top level: prefer installed headers, fallback to FetchContent.
if(NOT TARGET chirp_asio)
  find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    PATHS
      "${CMAKE_SOURCE_DIR}/third_party/asio/asio/include"
      "${CMAKE_SOURCE_DIR}/third_party/asio/include"
    PATH_SUFFIXES include asio/include
  )

  if(ASIO_INCLUDE_DIR)
    message(STATUS "Using system ASIO headers from: ${ASIO_INCLUDE_DIR}")
    add_library(chirp_asio INTERFACE)
    target_include_directories(chirp_asio INTERFACE ${ASIO_INCLUDE_DIR})
    target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
  elseif(TARGET asio::asio)
    add_library(chirp_asio INTERFACE)
    target_link_libraries(chirp_asio INTERFACE asio::asio)
    target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
  elseif(NOT CHIRP_FETCH_DEPS)
    message(FATAL_ERROR
      "ASIO headers were not found. Preferred fix: configure CMake with the vcpkg toolchain "
      "(-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake) so the manifest in vcpkg.json "
      "can provide asio. You can also provide CHIRP_ASIO_INCLUDE_DIR/ASIO_INCLUDE_DIR, or enable "
      "CHIRP_FETCH_DEPS to allow FetchContent.")
  else()
    include(FetchContent)
    FetchContent_Declare(
      asio
      GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
      GIT_TAG asio-1-30-2
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(asio)

    add_library(chirp_asio INTERFACE)
    target_include_directories(chirp_asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
  endif()
endif()

# TLS transport: asio::ssl in libs/network compiles against OpenSSL and the
# network headers expose ssl types, so this is a hard, PUBLIC dependency.
find_package(OpenSSL REQUIRED)

# ------------------------------------------------------------------------------
# Global Include Paths
# ------------------------------------------------------------------------------
# Include Abseil headers first to avoid macro conflicts
include_directories(
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
    ${CMAKE_SOURCE_DIR}/proto/cpp/proto
    ${PROTOBUF_INCLUDE_DIRS}
)

# ------------------------------------------------------------------------------
# Subdirectories (Monorepo Modules)
# ------------------------------------------------------------------------------

# 0. Protocol Buffers (must be first)
add_subdirectory(proto)

# 1. Libraries (Shared Code)
add_subdirectory(libs/common)
add_subdirectory(libs/network)

# 2. Services
# Game plane
add_subdirectory(services/game/sdk_gateway)
add_subdirectory(services/game/server_gateway)
add_subdirectory(services/game/npc_dialog)
# App plane
add_subdirectory(services/app/auth)
add_subdirectory(services/app/notification)
add_subdirectory(services/app/sdk_gateway)
# Shared (used by both planes)
add_subdirectory(services/shared/chat)
# Experimental
add_subdirectory(services/social)
add_subdirectory(services/voice)
add_subdirectory(services/party)
add_subdirectory(services/search)


# 3. SDKs
add_subdirectory(sdks/core)

# 4. Apps
add_subdirectory(apps/cli_client)

# 5. Tools / Tests
add_subdirectory(tools/benchmark)

# 6. Unit Tests (optional, requires GTest)
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
  option(ENABLE_TESTS "Enable unit tests" ON)
  if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
